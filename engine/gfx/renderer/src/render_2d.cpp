// -----------------------------------------------------------------------------
// engine/gfx/renderer/src/render_2d.cpp
//
// Purpose:
//   Implements the Render2D frontend on top of the RHI IGpuDevice interface.
//   Responsible for owning a basic graphics pipeline and cooperating with the
//   device for swapchain recreation on resize.
//
// Camera3D Cube:
//   - Animated 3D cube demo using Camera3D + base::math
//   - Per-swapchain-image UBO buffers + descriptor sets
//   - Depth test/write enabled
// -----------------------------------------------------------------------------

#include "strata/gfx/renderer/render_2d.h"

#include <cmath> // std::sin, std::cos
#include <cstdint>
#include <span>

#include "strata/base/diagnostics.h"

namespace strata::gfx::renderer
{

using namespace strata::gfx::rhi;

namespace
{

// Scene UBO layout must match GLSL (std140):
// layout(set=0,binding=0) uniform SceneUbo { mat4 view_proj; mat4 model; vec4 tint; };
struct alignas(16) UboScene
{
    strata::base::math::Mat4 view_proj;
    strata::base::math::Mat4 model;
    strata::base::math::Vec4 tint;
};

static_assert(sizeof(UboScene) % 16 == 0);

[[nodiscard]]
inline strata::base::math::Mat4 rotation_x(float radians) noexcept
{
    using strata::base::math::Mat4;

    float const c = std::cos(radians);
    float const s = std::sin(radians);

    Mat4 out{Mat4::identity()};

    // Row-major rotation X:
    // [1 0  0 1]
    // [0 c -s 0]
    // [0 s  c 0]
    // [0 0  0 1]
    // Stored column-major: m[col][row]
    out.m[1][1] = c;
    out.m[1][2] = s;
    out.m[2][1] = -s;
    out.m[2][2] = c;

    return out;
}

[[nodiscard]]
inline strata::base::math::Mat4 rotation_y(float radians) noexcept
{
    using strata::base::math::Mat4;

    float const c = std::cos(radians);
    float const s = std::sin(radians);

    Mat4 out{Mat4::identity()};

    // Row-major rotation Y (RH):
    // [ c 0 s 0]
    // [ 0 1 0 0]
    // [-s 0 c 0]
    // [ 0 0 0 1]
    // Stored column-major: m[col][row]
    out.m[0][0] = c;
    out.m[0][2] = -s;

    out.m[2][0] = s;
    out.m[2][2] = c;

    return out;
}

} // namespace

// -------------------------------------------------------------------------
// Render2D
// -------------------------------------------------------------------------

std::expected<Render2D, Render2DError> Render2D::create(base::Diagnostics& diagnostics,
                                                        IGpuDevice&        device,
                                                        SwapchainHandle    swapchain)
{
    using namespace strata::gfx::rhi;

    if (!swapchain)
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "renderer", "Render2D::create: invalid swapchain");
        return std::unexpected(Render2DError::InvalidSwapchain);
    }

    Render2D out{};
    out.diagnostics_ = &diagnostics;
    out.device_      = &device;
    out.swapchain_   = swapchain;

    // Camera defaults (same as your current ctor)
    out.camera_.position = base::math::Vec3(0.0f, 0.0f, 3.0f);
    out.camera_.set_yaw_pitch(0.0f, 0.0f);

    // 1) Descriptor set layout
    DescriptorBinding binding{};
    binding.binding = 0;
    binding.type    = DescriptorType::UniformBuffer;
    binding.count   = 1;
    binding.stages  = ShaderStage::Vertex | ShaderStage::Fragment;

    DescriptorSetLayoutDesc layout_desc{};
    layout_desc.bindings = std::span{&binding, 1};

    out.ubo_layout_ = device.create_descriptor_set_layout(layout_desc);
    if (!out.ubo_layout_)
    {
        STRATA_LOG_ERROR(diagnostics.logger(),
                         "renderer",
                         "Render2D::create: create_descriptor_set_layout failed");
        return std::unexpected(Render2DError::CreateDescriptorSetLayoutFailed);
        // NOTE: out will be destroyed here; ~Render2D() will call release() (safe).
    }

    // 2) Pipeline
    PipelineDesc desc{};
    desc.vertex_shader_path   = "shaders/fullscreen_triangle.vert.spv";
    desc.fragment_shader_path = "shaders/flat_color.frag.spv";
    desc.alpha_blend          = false;

    desc.depth_format = out.depth_format_;
    desc.depth_test   = true;
    desc.depth_write  = true;

    DescriptorSetLayoutHandle const set_layouts[] = {out.ubo_layout_};
    desc.set_layouts                              = set_layouts;

    out.pipeline_ = device.create_pipeline(desc);
    if (!out.pipeline_)
    {
        STRATA_LOG_ERROR(diagnostics.logger(),
                         "renderer",
                         "Render2D::create: create_pipeline failed");
        return std::unexpected(Render2DError::CreatePipelineFailed);
    }

    STRATA_ASSERT(diagnostics, out.is_valid());
    STRATA_LOG_INFO(diagnostics.logger(), "renderer", "Render2D initialized: 3D cube demo");

    // IMPORTANT: Render2D is move-only, so force move into expected.
    return {std::move(out)};
}

bool Render2D::is_valid() const noexcept
{
    return diagnostics_ != nullptr && device_ != nullptr && swapchain_ && pipeline_ && ubo_layout_;
}

// V1 Camera Input: external camera control
void Render2D::set_camera(Camera3D const& camera) noexcept
{
    camera_ = camera;
}

Camera3D const& Render2D::camera() const noexcept
{
    return camera_;
}

void Render2D::destroy_depth_textures() noexcept
{
    if (!device_)
    {
        depth_textures_.clear();
        depth_extent_ = {};
        return;
    }

    for (auto const h : depth_textures_)
    {
        if (h)
        {
            device_->destroy_texture(h);
        }
    }

    depth_textures_.clear();
    depth_extent_ = {};
}

FrameResult Render2D::ensure_depth_texture(std::uint32_t image_index, Extent2D extent)
{
    if (!is_valid())
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "Render2D::ensure_depth_texture called while invalid");
        }
        return FrameResult::Error;
    }

    if (extent.width == 0 || extent.height == 0)
    {
        return FrameResult::Ok;
    }

    // If the swapchain extent changed, the depth attachment must be recreated.
    if (depth_extent_.width != extent.width || depth_extent_.height != extent.height)
    {
        destroy_depth_textures();
        depth_extent_ = extent;
    }

    if (image_index >= depth_textures_.size())
    {
        depth_textures_.resize(static_cast<std::size_t>(image_index) + 1);
    }

    if (depth_textures_[image_index])
    {
        return FrameResult::Ok;
    }

    TextureDesc depth_desc{};
    depth_desc.size       = extent;
    depth_desc.format     = depth_format_;
    depth_desc.usage      = TextureUsage::DepthStencil;
    depth_desc.mip_levels = 1;

    depth_textures_[image_index] = device_->create_texture(depth_desc);
    if (!depth_textures_[image_index])
    {
        STRATA_LOG_ERROR(
            diagnostics_->logger(),
            "renderer",
            "Render2D::ensure_depth_texture: create_texture (depth) failed (image_index {}, {}x{})",
            image_index,
            extent.width,
            extent.height);

        return FrameResult::Error;
    }

    return FrameResult::Ok;
}

void Render2D::destroy_ubo_resources() noexcept
{
    if (!device_)
    {
        ubo_sets_.clear();
        ubo_buffers_.clear();
        return;
    }

    // Free sets first (they reference the buffers).
    for (auto const s : ubo_sets_)
    {
        if (s)
        {
            device_->free_descriptor_set(s);
        }
    }

    for (auto const b : ubo_buffers_)
    {
        if (b)
        {
            device_->destroy_buffer(b);
        }
    }

    ubo_sets_.clear();
    ubo_buffers_.clear();
}

FrameResult Render2D::ensure_ubo_resources(std::uint32_t image_index)
{
    if (!is_valid())
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "Render2D::ensure_ubo_resources called while invalid");
        }
        return FrameResult::Error;
    }

    if (image_index >= ubo_sets_.size())
    {
        ubo_sets_.resize(static_cast<std::size_t>(image_index) + 1);
        ubo_buffers_.resize(static_cast<std::size_t>(image_index) + 1);
    }

    // Already created?
    if (ubo_sets_[image_index] && ubo_buffers_[image_index])
        return FrameResult::Ok;

    // Clean up any partial state (defensive).
    if (ubo_sets_[image_index])
    {
        device_->free_descriptor_set(ubo_sets_[image_index]);
        ubo_sets_[image_index] = {};
    }
    if (ubo_buffers_[image_index])
    {
        device_->destroy_buffer(ubo_buffers_[image_index]);
        ubo_buffers_[image_index] = {};
    }

    // Create initial UBO contents (identity matrices, white tint).
    UboScene init{};
    init.view_proj = base::math::Mat4::identity();
    init.model     = base::math::Mat4::identity();
    init.tint      = base::math::Vec4{1.0f, 1.0f, 1.0f, 1.0f};

    BufferDesc buf_desc{};
    buf_desc.size_bytes   = sizeof(UboScene);
    buf_desc.usage        = BufferUsage::Uniform | BufferUsage::Upload;
    buf_desc.host_visible = true;

    auto init_bytes           = std::as_bytes(std::span{&init, 1});
    ubo_buffers_[image_index] = device_->create_buffer(buf_desc, init_bytes);
    if (!ubo_buffers_[image_index])
    {
        STRATA_LOG_ERROR(diagnostics_->logger(),
                         "renderer",
                         "Render2D: create_buffer (per-image UBO) failed (image_index {})",
                         image_index);
        return FrameResult::Error;
    }

    ubo_sets_[image_index] = device_->allocate_descriptor_set(ubo_layout_);
    if (!ubo_sets_[image_index])
    {
        STRATA_LOG_ERROR(diagnostics_->logger(),
                         "renderer",
                         "Render2D: allocate_descriptor_set failed (image_index {})",
                         image_index);

        device_->destroy_buffer(ubo_buffers_[image_index]);
        ubo_buffers_[image_index] = {};
        return FrameResult::Error;
    }

    DescriptorWrite write{};
    write.binding             = 0;
    write.type                = DescriptorType::UniformBuffer;
    write.buffer.buffer       = ubo_buffers_[image_index];
    write.buffer.offset_bytes = 0;
    write.buffer.range_bytes  = sizeof(UboScene);

    FrameResult const upd =
        device_->update_descriptor_set(ubo_sets_[image_index], std::span{&write, 1});
    if (upd != FrameResult::Ok)
    {
        STRATA_LOG_ERROR(diagnostics_->logger(),
                         "renderer",
                         "Render2D: update_descriptor_set failed (image_index {})",
                         image_index);

        device_->free_descriptor_set(ubo_sets_[image_index]);
        ubo_sets_[image_index] = {};

        device_->destroy_buffer(ubo_buffers_[image_index]);
        ubo_buffers_[image_index] = {};

        return FrameResult::Error;
    }

    return FrameResult::Ok;
}

void Render2D::release() noexcept
{
    if (device_)
    {
        if (pipeline_)
            device_->destroy_pipeline(pipeline_);

        destroy_depth_textures();
        destroy_ubo_resources();

        if (ubo_layout_)
            device_->destroy_descriptor_set_layout(ubo_layout_);
    }

    pipeline_   = {};
    ubo_layout_ = {};
    swapchain_  = {};

    depth_format_ = rhi::Format::D24_UNorm_S8_UInt;
    depth_extent_ = {};
    depth_textures_.clear();

    ubo_sets_.clear();
    ubo_buffers_.clear();

    camera_        = {};
    frame_counter_ = 0;

    device_      = nullptr;
    diagnostics_ = nullptr;
}

Render2D::~Render2D()
{
    release();
}

Render2D::Render2D(Render2D&& other) noexcept
      : diagnostics_(other.diagnostics_)
      , device_(other.device_)
      , swapchain_(other.swapchain_)
      , pipeline_(other.pipeline_)
      , ubo_layout_(other.ubo_layout_)
      , ubo_sets_(std::move(other.ubo_sets_))
      , ubo_buffers_(std::move(other.ubo_buffers_))
      , depth_format_(other.depth_format_)
      , depth_extent_(other.depth_extent_)
      , depth_textures_(std::move(other.depth_textures_))
      , camera_(other.camera_)
      , frame_counter_(other.frame_counter_)
{
    other.diagnostics_ = nullptr;
    other.device_      = nullptr;
    other.swapchain_   = {};
    other.pipeline_    = {};
    other.ubo_layout_  = {};

    other.ubo_sets_.clear();
    other.ubo_buffers_.clear();

    other.depth_extent_ = {};
    other.depth_textures_.clear();

    other.camera_        = {};
    other.frame_counter_ = 0;
}

Render2D& Render2D::operator=(Render2D&& other) noexcept
{
    if (this != &other)
    {
        release();

        diagnostics_ = other.diagnostics_;
        device_      = other.device_;
        swapchain_   = other.swapchain_;
        pipeline_    = other.pipeline_;
        ubo_layout_  = other.ubo_layout_;

        ubo_sets_    = std::move(other.ubo_sets_);
        ubo_buffers_ = std::move(other.ubo_buffers_);

        depth_format_   = other.depth_format_;
        depth_extent_   = other.depth_extent_;
        depth_textures_ = std::move(other.depth_textures_);

        camera_        = other.camera_;
        frame_counter_ = other.frame_counter_;

        other.diagnostics_ = nullptr;
        other.device_      = nullptr;
        other.swapchain_   = {};
        other.pipeline_    = {};
        other.ubo_layout_  = {};

        other.ubo_sets_.clear();
        other.ubo_buffers_.clear();

        other.depth_extent_ = {};
        other.depth_textures_.clear();

        other.camera_        = {};
        other.frame_counter_ = 0;
    }
    return *this;
}

FrameResult Render2D::draw_frame()
{
    if (!is_valid())
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "Render2D::draw_frame called while invalid");
        }
        return FrameResult::Error;
    }

    rhi::AcquiredImage img{};
    FrameResult const  acquire = device_->acquire_next_image(swapchain_, img);

    FrameResult hint = FrameResult::Ok;

    if (acquire == FrameResult::Error || acquire == FrameResult::ResizeNeeded)
    {
        return acquire;
    }
    if (acquire == FrameResult::Suboptimal)
    {
        hint = FrameResult::Suboptimal;
    }

    // Ensure we have a depth texture corresponding to this swapchain image index.
    if (ensure_depth_texture(img.image_index, img.extent) != FrameResult::Ok)
    {
        return FrameResult::Error;
    }

    // Ensure per-image UBO resources exist (buffers + descriptor sets).
    if (ensure_ubo_resources(img.image_index) != FrameResult::Ok)
    {
        return FrameResult::Error;
    }

    rhi::TextureHandle const depth = depth_textures_[img.image_index];
    STRATA_ASSERT(*diagnostics_, depth);

    rhi::DescriptorSetHandle const ubo_set    = ubo_sets_[img.image_index];
    rhi::BufferHandle const        ubo_buffer = ubo_buffers_[img.image_index];
    STRATA_ASSERT(*diagnostics_, ubo_set);
    STRATA_ASSERT(*diagnostics_, ubo_buffer);

    // --- Update scene UBO (Camera 3D Cube) -----------------------------------
    {
        float const aspect = (img.extent.height != 0)
            ? (static_cast<float>(img.extent.width) / static_cast<float>(img.extent.height))
            : 1.0f;

        // Simple animation: rotate cube in place.
        float const t  = static_cast<float>(frame_counter_) * 0.015f;
        float const t2 = static_cast<float>(frame_counter_) * 0.010f;
        frame_counter_++;

        UboScene ubo{};
        ubo.view_proj = camera_.view_proj(aspect, true);
        ubo.model     = base::math::mul(rotation_y(t), rotation_x(t2));
        ubo.tint      = base::math::Vec4{1.0f, 1.0f, 1.0f, 1.0f};

        auto bytes = std::as_bytes(std::span{&ubo, 1});
        if (device_->write_buffer(ubo_buffer, bytes, 0) != FrameResult::Ok)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "Render2D: write_buffer(UBO) failed");
            return FrameResult::Error;
        }
    }

    rhi::CommandBufferHandle const cmd = device_->begin_commands();
    if (!cmd)
        return FrameResult::Error;

    bool        pass_open = false;
    FrameResult result    = FrameResult::Error;

    rhi::ClearColor const clear{0.08f, 0.08f, 0.10f, 1.0f};

    // --- Record -------------------------------------------------------------
    if (device_
            ->cmd_begin_swapchain_pass(cmd, swapchain_, img.image_index, clear, depth, 1.0f, 0) !=
        FrameResult::Ok)
    {
        goto cleanup;
    }
    pass_open = true;

    if (device_->cmd_bind_pipeline(cmd, pipeline_) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (device_->cmd_bind_descriptor_set(cmd, pipeline_, 0, ubo_set) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (device_->cmd_set_viewport_scissor(cmd, img.extent) != FrameResult::Ok)
    {
        goto cleanup;
    }

    // 36 vertices = 12 triangles = 1 cube
    if (device_->cmd_draw(cmd, 36, 1, 0, 0) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (device_->cmd_end_swapchain_pass(cmd, swapchain_, img.image_index) != FrameResult::Ok)
    {
        goto cleanup;
    }
    pass_open = false;

    if (device_->end_commands(cmd) != FrameResult::Ok)
    {
        result = FrameResult::Error;
        goto cleanup_after_end;
    }

    // --- Submit -------------------------------------------------------------
    {
        rhi::IGpuDevice::SubmitDesc sd{};
        sd.command_buffer = cmd;
        sd.swapchain      = swapchain_;
        sd.image_index    = img.image_index;
        sd.frame_index    = img.frame_index;

        FrameResult const sub = device_->submit(sd);
        if (sub != FrameResult::Ok)
        {
            result = sub;
            goto cleanup_after_end;
        }
    }

    // --- Present ------------------------------------------------------------
    {
        FrameResult const pres = device_->present(swapchain_, img.image_index);
        if (pres == FrameResult::Ok)
        {
            result = hint;
        }
        else
        {
            result = pres;
        }
        return result;
    }

cleanup:
    if (pass_open)
    {
        device_->cmd_end_swapchain_pass(cmd, swapchain_, img.image_index);
        pass_open = false;
    }

    // End command buffer.
    if (device_->end_commands(cmd) == FrameResult::Ok)
    {
        // Best-effort: drain the acquire semaphore and release the image.
        rhi::IGpuDevice::SubmitDesc sd{};
        sd.command_buffer = cmd;
        sd.swapchain      = swapchain_;
        sd.image_index    = img.image_index;
        sd.frame_index    = img.frame_index;

        FrameResult const sub = device_->submit(sd);
        if (sub == FrameResult::Ok)
        {
            (void)device_->present(swapchain_, img.image_index);
        }
        // else: do NOT present - render_finished is not guaranteed signaled.
    }

cleanup_after_end:
    return result;
}

FrameResult Render2D::recreate_pipeline()
{
    if (!device_ || !swapchain_ || !ubo_layout_)
        return FrameResult::Error;

    if (pipeline_)
        device_->destroy_pipeline(pipeline_);

    PipelineDesc desc{};
    desc.vertex_shader_path   = "shaders/fullscreen_triangle.vert.spv";
    desc.fragment_shader_path = "shaders/flat_color.frag.spv";
    desc.alpha_blend          = false;

    desc.depth_format = depth_format_;
    desc.depth_test   = true;
    desc.depth_write  = true;

    DescriptorSetLayoutHandle const set_layouts[] = {ubo_layout_};
    desc.set_layouts                              = set_layouts;

    pipeline_ = device_->create_pipeline(desc);
    return pipeline_ ? FrameResult::Ok : FrameResult::Error;
}

void Render2D::on_before_swapchain_resize() noexcept
{
    // Depth images are swapchain-extent dependent.
    destroy_depth_textures();

    // Per-image UBO sets/buffers are swapchain-image-count dependent.
    // Keeping them is *allowed*, but destroying here prevents "max-ever image_count" growth.
    destroy_ubo_resources();
}

} // namespace strata::gfx::renderer
