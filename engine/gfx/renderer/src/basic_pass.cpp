// path: engine/gfx/renderer/src/basic_pass.cpp
// -----------------------------------------------------------------------------
// engine/gfx/renderer/src/basic_pass.cpp
//
// Purpose:
//   Implements the BasicPass frontend on top of the RHI IGpuDevice interface.
//   Responsible for owning a basic graphics pipeline and cooperating with the
//   device for swapchain recreation on resize.
//
// Current behavior:
//   - If RenderScene::world_mesh() is valid: draw it (indexed).
//   - Else: draw the demo cube (non-indexed) using a small internal vertex buffer.
//   - If RenderScene::selected_mesh() is valid: draw it again with a different tint.
// -----------------------------------------------------------------------------

#include "strata/gfx/renderer/basic_pass.h"

#include <array>
#include <cmath> // std::sin, std::cos
#include <cstdint>
#include <span>

#include "strata/base/diagnostics.h"
#include "strata/gfx/renderer/render_scene.h"

namespace strata::gfx::renderer
{

using namespace strata::gfx::rhi;

namespace
{

// Vertex layout for v1 meshes:
//   layout(location=0) in vec3 in_pos;
// Bound as:
//   binding=0, stride=12 bytes, per-vertex
struct VertexP3
{
    float x, y, z;
};
static_assert(sizeof(VertexP3) == sizeof(float) * 3);

// Demo cube vertex order matches the historical shader's 36-vertex cube ordering.
// We draw it non-indexed so gl_VertexIndex runs 0..35 (useful for face coloring in shader).
static constexpr VertexP3 demo_cube_verts[36] = {
    // back face (-Z)
    {-0.5f, -0.5f, -0.5f},
    {0.5f, 0.5f, -0.5f},
    {0.5f, -0.5f, -0.5f},
    {0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f, -0.5f},
    {-0.5f, 0.5f, -0.5f},

    // front face (+Z)
    {-0.5f, -0.5f, 0.5f},
    {0.5f, -0.5f, 0.5f},
    {0.5f, 0.5f, 0.5f},
    {0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f, 0.5f},
    {-0.5f, -0.5f, 0.5f},

    // left face (-X)
    {-0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f, -0.5f},
    {-0.5f, -0.5f, -0.5f},
    {-0.5f, -0.5f, 0.5f},
    {-0.5f, 0.5f, 0.5f},

    // right face (+X)
    {0.5f, 0.5f, 0.5f},
    {0.5f, -0.5f, -0.5f},
    {0.5f, 0.5f, -0.5f},
    {0.5f, -0.5f, -0.5f},
    {0.5f, 0.5f, 0.5f},
    {0.5f, -0.5f, 0.5f},

    // bottom face (-Y)
    {-0.5f, -0.5f, -0.5f},
    {0.5f, -0.5f, -0.5f},
    {0.5f, -0.5f, 0.5f},
    {0.5f, -0.5f, 0.5f},
    {-0.5f, -0.5f, 0.5f},
    {-0.5f, -0.5f, -0.5f},

    // top face (+Y)
    {-0.5f, 0.5f, -0.5f},
    {0.5f, 0.5f, 0.5f},
    {0.5f, 0.5f, -0.5f},
    {0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f, -0.5f},
    {-0.5f, 0.5f, 0.5f},
};

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
// BasicPass
// -------------------------------------------------------------------------

std::expected<BasicPass, BasicPassError> BasicPass::create(base::Diagnostics& diagnostics,
                                                           IGpuDevice&        device,
                                                           SwapchainHandle    swapchain)
{
    using namespace strata::gfx::rhi;

    if (!swapchain)
    {
        STRATA_LOG_ERROR(diagnostics.logger(), "renderer", "BasicPass::create: invalid swapchain");
        return std::unexpected(BasicPassError::InvalidSwapchain);
    }

    BasicPass out{};
    out.diagnostics_ = &diagnostics;
    out.device_      = &device;
    out.swapchain_   = swapchain;

    // Camera defaults
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
                         "BasicPass::create: create_descriptor_set_layout failed");
        return std::unexpected(BasicPassError::CreateDescriptorSetLayoutFailed);
    }

    // 2) Pipeline
    //
    // v1 mesh contract:
    //   binding 0: position (vec3), stride 12 bytes
    VertexBindingDesc vb{};
    vb.binding = 0;
    vb.stride  = static_cast<std::uint32_t>(sizeof(VertexP3));
    vb.rate    = VertexInputRate::Vertex;

    VertexAttributeDesc va{};
    va.location = 0;
    va.binding  = 0;
    va.format   = VertexFormat::Float3;
    va.offset   = 0;

    PipelineDesc desc{};
    desc.vertex_shader_path   = "shaders/procedural_cube.vert.spv";
    desc.fragment_shader_path = "shaders/vertex_color.frag.spv";
    desc.alpha_blend          = false;

    desc.depth_format = out.depth_format_;
    desc.depth_test   = true;
    desc.depth_write  = true;

    desc.vertex_bindings   = std::span{&vb, 1};
    desc.vertex_attributes = std::span{&va, 1};

    DescriptorSetLayoutHandle const set_layouts[] = {out.ubo_layout_};
    desc.set_layouts                              = set_layouts;

    out.pipeline_ = device.create_pipeline(desc);
    if (!out.pipeline_)
    {
        STRATA_LOG_ERROR(diagnostics.logger(),
                         "renderer",
                         "BasicPass::create: create_pipeline failed");
        return std::unexpected(BasicPassError::CreatePipelineFailed);
    }

    // 3) Demo cube vertex buffer (fallback when no world mesh is supplied)
    {
        BufferDesc vb_desc{};
        vb_desc.size_bytes   = sizeof(demo_cube_verts);
        vb_desc.usage        = BufferUsage::Vertex | BufferUsage::Upload;
        vb_desc.host_visible = true;

        auto vb_bytes     = std::as_bytes(std::span{demo_cube_verts});
        out.demo_cube_vb_ = device.create_buffer(vb_desc, vb_bytes);
        if (!out.demo_cube_vb_)
        {
            STRATA_LOG_ERROR(diagnostics.logger(),
                             "renderer",
                             "BasicPass::create: create_buffer(demo cube VB) failed");
            return std::unexpected(BasicPassError::CreateDemoCubeVertexBufferFailed);
        }

        out.demo_cube_vertex_count_ = static_cast<std::uint32_t>(std::size(demo_cube_verts));
    }

    STRATA_ASSERT(diagnostics, out.is_valid());
    STRATA_LOG_INFO(diagnostics.logger(), "renderer", "BasicPass initialized: mesh + demo cube");

    return {std::move(out)};
}

bool BasicPass::is_valid() const noexcept
{
    return diagnostics_ != nullptr &&
        device_ != nullptr &&
        swapchain_ &&
        pipeline_ &&
        ubo_layout_ &&
        demo_cube_vb_ &&
        demo_cube_vertex_count_ > 0;
}

void BasicPass::destroy_depth_textures() noexcept
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

FrameResult BasicPass::ensure_depth_texture(std::uint32_t image_index, Extent2D extent)
{
    if (!is_valid())
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "BasicPass::ensure_depth_texture called while invalid");
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
        STRATA_LOG_ERROR(diagnostics_->logger(),
                         "renderer",
                         "BasicPass::ensure_depth_texture: create_texture (depth) failed "
                         "(image_index {}, {}x{})",
                         image_index,
                         extent.width,
                         extent.height);

        return FrameResult::Error;
    }

    return FrameResult::Ok;
}

void BasicPass::destroy_ubo_resources() noexcept
{
    if (!device_)
    {
        ubo_sets_.clear();
        ubo_buffers_.clear();
        return;
    }

    // Free sets first (they reference the buffers).
    for (auto const& per_image_sets : ubo_sets_)
    {
        for (auto const s : per_image_sets)
        {
            if (s)
            {
                device_->free_descriptor_set(s);
            }
        }
    }

    for (auto const& per_image_bufs : ubo_buffers_)
    {
        for (auto const b : per_image_bufs)
        {
            if (b)
            {
                device_->destroy_buffer(b);
            }
        }
    }

    ubo_sets_.clear();
    ubo_buffers_.clear();
}

FrameResult BasicPass::ensure_ubo_resources(std::uint32_t image_index)
{
    if (!is_valid())
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "BasicPass::ensure_ubo_resources called while invalid");
        }
        return FrameResult::Error;
    }

    if (image_index >= ubo_sets_.size())
    {
        ubo_sets_.resize(static_cast<std::size_t>(image_index) + 1);
        ubo_buffers_.resize(static_cast<std::size_t>(image_index) + 1);
    }

    auto& per_image_sets = ubo_sets_[image_index];
    auto& per_image_bufs = ubo_buffers_[image_index];

    bool ok = true;
    for (std::size_t i = 0; i < ubo_slots_per_image; ++i)
    {
        ok = ok && static_cast<bool>(per_image_sets[i]) && static_cast<bool>(per_image_bufs[i]);
    }

    if (ok)
        return FrameResult::Ok;

    auto cleanup_image = [&]()
    {
        for (std::size_t i = 0; i < ubo_slots_per_image; ++i)
        {
            if (per_image_sets[i])
            {
                device_->free_descriptor_set(per_image_sets[i]);
                per_image_sets[i] = {};
            }
        }

        for (std::size_t i = 0; i < ubo_slots_per_image; ++i)
        {
            if (per_image_bufs[i])
            {
                device_->destroy_buffer(per_image_bufs[i]);
                per_image_bufs[i] = {};
            }
        }
    };

    // Clean up any partial state (defensive).
    cleanup_image();

    // Create initial UBO contents (identity matrices, white tint).
    UboScene init{};
    init.view_proj = base::math::Mat4::identity();
    init.model     = base::math::Mat4::identity();
    init.tint      = base::math::Vec4{1.0f, 1.0f, 1.0f, 1.0f};

    BufferDesc buf_desc{};
    buf_desc.size_bytes   = sizeof(UboScene);
    buf_desc.usage        = BufferUsage::Uniform | BufferUsage::Upload;
    buf_desc.host_visible = true;

    auto init_bytes = std::as_bytes(std::span{&init, 1});

    // Allocate + update one descriptor set per UBO slot, each using its own buffer at offset 0.
    for (std::size_t slot = 0; slot < ubo_slots_per_image; ++slot)
    {
        per_image_bufs[slot] = device_->create_buffer(buf_desc, init_bytes);
        if (!per_image_bufs[slot])
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "BasicPass: create_buffer (UBO slot buffer) failed (image_index {}, "
                             "slot {})",
                             image_index,
                             slot);
            cleanup_image();
            return FrameResult::Error;
        }

        per_image_sets[slot] = device_->allocate_descriptor_set(ubo_layout_);
        if (!per_image_sets[slot])
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "BasicPass: allocate_descriptor_set failed (image_index {}, slot {})",
                             image_index,
                             slot);
            cleanup_image();
            return FrameResult::Error;
        }

        DescriptorWrite write{};
        write.binding             = 0;
        write.type                = DescriptorType::UniformBuffer;
        write.buffer.buffer       = per_image_bufs[slot];
        write.buffer.offset_bytes = 0;
        write.buffer.range_bytes  = sizeof(UboScene);

        FrameResult const upd =
            device_->update_descriptor_set(per_image_sets[slot], std::span{&write, 1});
        if (upd != FrameResult::Ok)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "BasicPass: update_descriptor_set failed (image_index {}, slot {})",
                             image_index,
                             slot);
            cleanup_image();
            return FrameResult::Error;
        }
    }

    return FrameResult::Ok;
}

void BasicPass::release() noexcept
{
    if (device_)
    {
        if (pipeline_)
            device_->destroy_pipeline(pipeline_);

        destroy_depth_textures();
        destroy_ubo_resources();

        if (demo_cube_vb_)
            device_->destroy_buffer(demo_cube_vb_);

        if (ubo_layout_)
            device_->destroy_descriptor_set_layout(ubo_layout_);
    }

    pipeline_   = {};
    ubo_layout_ = {};
    swapchain_  = {};

    demo_cube_vb_           = {};
    demo_cube_vertex_count_ = 0;

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

BasicPass::~BasicPass()
{
    release();
}

BasicPass::BasicPass(BasicPass&& other) noexcept
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
      , demo_cube_vb_(other.demo_cube_vb_)
      , demo_cube_vertex_count_(other.demo_cube_vertex_count_)
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

    other.demo_cube_vb_           = {};
    other.demo_cube_vertex_count_ = 0;

    other.camera_        = {};
    other.frame_counter_ = 0;
}

BasicPass& BasicPass::operator=(BasicPass&& other) noexcept
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

        demo_cube_vb_           = other.demo_cube_vb_;
        demo_cube_vertex_count_ = other.demo_cube_vertex_count_;

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

        other.demo_cube_vb_           = {};
        other.demo_cube_vertex_count_ = 0;

        other.camera_        = {};
        other.frame_counter_ = 0;
    }
    return *this;
}

FrameResult BasicPass::draw_frame(RenderScene const& scene)
{
    if (!is_valid())
    {
        if (diagnostics_)
        {
            STRATA_LOG_ERROR(diagnostics_->logger(),
                             "renderer",
                             "BasicPass::draw_frame called while invalid");
        }
        return FrameResult::Error;
    }

    // Consume camera from scene.
    camera_ = scene.camera();

    // Determine what we can draw this frame.
    GpuMesh const& world    = scene.world_mesh();
    GpuMesh const& selected = scene.selected_mesh();

    bool const has_world_mesh =
        world.vertex_buffer && world.index_buffer && (world.index_count > 0);

    bool const has_selected_mesh =
        selected.vertex_buffer && selected.index_buffer && (selected.index_count > 0);

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

    // Ensure per-image UBO resources exist (per-slot buffers + per-slot descriptor sets).
    if (ensure_ubo_resources(img.image_index) != FrameResult::Ok)
    {
        return FrameResult::Error;
    }

    rhi::TextureHandle const depth = depth_textures_[img.image_index];
    STRATA_ASSERT(*diagnostics_, depth);

    auto const& per_image_sets = ubo_sets_[img.image_index];
    auto const& per_image_bufs = ubo_buffers_[img.image_index];

    STRATA_ASSERT(*diagnostics_, per_image_sets[0]);
    STRATA_ASSERT(*diagnostics_, per_image_sets[1]);
    STRATA_ASSERT(*diagnostics_, per_image_bufs[0]);
    STRATA_ASSERT(*diagnostics_, per_image_bufs[1]);

    // --- Build per-draw UBO data --------------------------------------------
    float const aspect = (img.extent.height != 0)
        ? (static_cast<float>(img.extent.width) / static_cast<float>(img.extent.height))
        : 1.0f;

    strata::base::math::Mat4 const view_proj = camera_.view_proj(aspect, true);

    // Slot 0: base draw (world mesh if present, else demo cube)
    UboScene ubo0{};
    ubo0.view_proj = view_proj;

    if (has_world_mesh)
    {
        ubo0.model = strata::base::math::Mat4::identity();

        // tint.a is used as a simple shader-side "mode":
        //   >= 0.5 -> demo face colors
        //   <  0.5 -> solid tint
        ubo0.tint = strata::base::math::Vec4{0.90f, 0.90f, 0.90f, 0.0f};
    }
    else
    {
        // Animate demo cube in place.
        float const t  = static_cast<float>(frame_counter_) * 0.015f;
        float const t2 = static_cast<float>(frame_counter_) * 0.010f;
        frame_counter_++;

        ubo0.model = base::math::mul(rotation_y(t), rotation_x(t2));
        ubo0.tint  = strata::base::math::Vec4{1.0f, 1.0f, 1.0f, 1.0f}; // enable face colors
    }

    // Slot 1: selected draw (if any). If none, just mirror slot 0.
    UboScene ubo1 = ubo0;
    if (has_selected_mesh)
    {
        ubo1.model = strata::base::math::Mat4::identity();
        ubo1.tint  = strata::base::math::Vec4{1.0f, 0.55f, 0.10f, 0.0f}; // solid highlight
    }

    auto write_ubo_slot = [&](std::size_t slot, UboScene const& ubo) -> FrameResult
    {
        STRATA_ASSERT(*diagnostics_, slot < ubo_slots_per_image);
        auto bytes = std::as_bytes(std::span{&ubo, 1});
        return device_->write_buffer(per_image_bufs[slot], bytes, 0);
    };

    if (write_ubo_slot(0, ubo0) != FrameResult::Ok)
    {
        STRATA_LOG_ERROR(diagnostics_->logger(),
                         "renderer",
                         "BasicPass: write_buffer(UBO slot 0) failed");
        return FrameResult::Error;
    }

    if (write_ubo_slot(1, ubo1) != FrameResult::Ok)
    {
        STRATA_LOG_ERROR(diagnostics_->logger(),
                         "renderer",
                         "BasicPass: write_buffer(UBO slot 1) failed");
        return FrameResult::Error;
    }

    // --- Record -------------------------------------------------------------
    rhi::CommandBufferHandle const cmd = device_->begin_commands();
    if (!cmd)
        return FrameResult::Error;

    bool        pass_open = false;
    FrameResult result    = FrameResult::Error;

    rhi::ClearColor const clear{0.08f, 0.08f, 0.10f, 1.0f};

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

    if (device_->cmd_set_viewport_scissor(cmd, img.extent) != FrameResult::Ok)
    {
        goto cleanup;
    }

    // ---- Base draw (slot 0) -------------------------------------------------
    if (device_->cmd_bind_descriptor_set(cmd, pipeline_, 0, per_image_sets[0]) != FrameResult::Ok)
    {
        goto cleanup;
    }

    if (has_world_mesh)
    {
        if (device_->cmd_bind_vertex_buffer(cmd, 0, world.vertex_buffer, 0) != FrameResult::Ok)
        {
            goto cleanup;
        }
        if (device_->cmd_bind_index_buffer(cmd, world.index_buffer, world.index_type, 0) !=
            FrameResult::Ok)
        {
            goto cleanup;
        }
        if (device_->cmd_draw_indexed(cmd, world.index_count, 1, 0, 0, 0) != FrameResult::Ok)
        {
            goto cleanup;
        }
    }
    else
    {
        // Demo cube fallback
        if (device_->cmd_bind_vertex_buffer(cmd, 0, demo_cube_vb_, 0) != FrameResult::Ok)
        {
            goto cleanup;
        }
        if (device_->cmd_draw(cmd, demo_cube_vertex_count_, 1, 0, 0) != FrameResult::Ok)
        {
            goto cleanup;
        }
    }

    // ---- Selected draw (slot 1) --------------------------------------------
    if (has_selected_mesh)
    {
        if (device_->cmd_bind_descriptor_set(cmd, pipeline_, 0, per_image_sets[1]) !=
            FrameResult::Ok)
        {
            goto cleanup;
        }

        if (device_->cmd_bind_vertex_buffer(cmd, 0, selected.vertex_buffer, 0) != FrameResult::Ok)
        {
            goto cleanup;
        }
        if (device_->cmd_bind_index_buffer(cmd, selected.index_buffer, selected.index_type, 0) !=
            FrameResult::Ok)
        {
            goto cleanup;
        }
        if (device_->cmd_draw_indexed(cmd, selected.index_count, 1, 0, 0, 0) != FrameResult::Ok)
        {
            goto cleanup;
        }
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

FrameResult BasicPass::recreate_pipeline()
{
    if (!device_ || !swapchain_ || !ubo_layout_)
        return FrameResult::Error;

    if (pipeline_)
        device_->destroy_pipeline(pipeline_);

    // Must match create()'s pipeline recipe.
    VertexBindingDesc vb{};
    vb.binding = 0;
    vb.stride  = static_cast<std::uint32_t>(sizeof(VertexP3));
    vb.rate    = VertexInputRate::Vertex;

    VertexAttributeDesc va{};
    va.location = 0;
    va.binding  = 0;
    va.format   = VertexFormat::Float3;
    va.offset   = 0;

    PipelineDesc desc{};
    desc.vertex_shader_path   = "shaders/procedural_cube.vert.spv";
    desc.fragment_shader_path = "shaders/vertex_color.frag.spv";
    desc.alpha_blend          = false;

    desc.depth_format = depth_format_;
    desc.depth_test   = true;
    desc.depth_write  = true;

    desc.vertex_bindings   = std::span{&vb, 1};
    desc.vertex_attributes = std::span{&va, 1};

    DescriptorSetLayoutHandle const set_layouts[] = {ubo_layout_};
    desc.set_layouts                              = set_layouts;

    pipeline_ = device_->create_pipeline(desc);
    return pipeline_ ? FrameResult::Ok : FrameResult::Error;
}

void BasicPass::on_before_swapchain_resize() noexcept
{
    // Depth images are swapchain-extent dependent.
    destroy_depth_textures();

    // Per-image UBO sets/buffers are swapchain-image-count dependent.
    // Keeping them is *allowed*, but destroying here prevents "max-ever image_count" growth.
    destroy_ubo_resources();
}

} // namespace strata::gfx::renderer
