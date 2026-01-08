// -----------------------------------------------------------------------------
// tools/level_editor/src/main.cpp
//
// MVP Level Editor:
//   - Reuse Strata engine rendering (Renderer/BasicPass).
//   - Camera: WASD + mouse look (hold RMB to lock cursor).
//   - Picking: LMB selects the box under cursor via ray vs AABB.
// -----------------------------------------------------------------------------

#include "strata/core/action_map.h"
#include "strata/core/application.h"

#include "strata/base/math.h"
#include "strata/gfx/rhi/gpu_device.h"
#include "strata/gfx/rhi/gpu_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <span>
#include <vector>

namespace
{
using strata::base::math::Vec3;

// Matches renderer v1 vertex input: location 0 = vec3 position (12 bytes)
struct VertexP3
{
    float x, y, z;
};
static_assert(sizeof(VertexP3) == 12);

struct Aabb
{
    Vec3 min;
    Vec3 max;
};

struct Ray
{
    Vec3 origin;
    Vec3 dir; // normalized
};

[[maybe_unused]]
static Vec3 to_vec3(VertexP3 v)
{
    return Vec3{v.x, v.y, v.z};
}

static void append_box(std::vector<VertexP3>&      out_v,
                       std::vector<std::uint32_t>& out_i,
                       Vec3                        bmin,
                       Vec3                        bmax)
{
    std::uint32_t const base = static_cast<std::uint32_t>(out_v.size());

    // 8 verts
    out_v.push_back(VertexP3{bmin.x, bmin.y, bmin.z}); // 0
    out_v.push_back(VertexP3{bmax.x, bmin.y, bmin.z}); // 1
    out_v.push_back(VertexP3{bmax.x, bmax.y, bmin.z}); // 2
    out_v.push_back(VertexP3{bmin.x, bmax.y, bmin.z}); // 3

    out_v.push_back(VertexP3{bmin.x, bmin.y, bmax.z}); // 4
    out_v.push_back(VertexP3{bmax.x, bmin.y, bmax.z}); // 5
    out_v.push_back(VertexP3{bmax.x, bmax.y, bmax.z}); // 6
    out_v.push_back(VertexP3{bmin.x, bmax.y, bmax.z}); // 7

    auto add_tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c)
    {
        out_i.push_back(base + a);
        out_i.push_back(base + b);
        out_i.push_back(base + c);
    };

    // Winding chosen so outward normals are correct in RH with typical back-face culling.

    // -Z face
    add_tri(0, 2, 1);
    add_tri(0, 3, 2);

    // +Z face
    add_tri(4, 5, 6);
    add_tri(4, 6, 7);

    // -X face
    add_tri(0, 4, 7);
    add_tri(0, 7, 3);

    // +X face
    add_tri(1, 2, 6);
    add_tri(1, 6, 5);

    // -Y face
    add_tri(0, 1, 5);
    add_tri(0, 5, 4);

    // +Y face
    add_tri(3, 7, 6);
    add_tri(3, 6, 2);
}

static bool ray_intersect_aabb(Ray const& ray, Aabb const& box, float& out_t) noexcept
{
    // Slab test
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::infinity();

    auto step = [&](float ro, float rd, float mn, float mx) -> bool
    {
        constexpr float eps = 1e-8f;

        if (std::abs(rd) < eps)
        {
            // Ray parallel to slab: must be inside
            return (ro >= mn && ro <= mx);
        }

        float const inv = 1.0f / rd;
        float       t1  = (mn - ro) * inv;
        float       t2  = (mx - ro) * inv;

        if (t1 > t2)
            std::swap(t1, t2);

        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);

        return tmin <= tmax;
    };

    if (!step(ray.origin.x, ray.dir.x, box.min.x, box.max.x))
        return false;
    if (!step(ray.origin.y, ray.dir.y, box.min.y, box.max.y))
        return false;
    if (!step(ray.origin.z, ray.dir.z, box.min.z, box.max.z))
        return false;

    if (tmax < 0.0f)
        return false; // entirely behind

    out_t = (tmin >= 0.0f) ? tmin : tmax;
    return true;
}

static Ray make_mouse_ray(strata::gfx::renderer::Camera3D const& cam,
                          std::int32_t                           mouse_x,
                          std::int32_t                           mouse_y,
                          std::int32_t                           width,
                          std::int32_t                           height)
{
    using strata::base::math::normalize;

    float const w      = (width > 0) ? static_cast<float>(width) : 1.0f;
    float const h      = (height > 0) ? static_cast<float>(height) : 1.0f;
    float const aspect = w / h;

    // Convert window coords (origin top-left, y down) to NDC (Vulkan viewport with +height):
    // x: 0 -> -1, w -> +1
    // y: 0 -> -1 (top), h -> +1 (bottom)
    float const px = static_cast<float>(mouse_x) + 0.5f;
    float const py = static_cast<float>(mouse_y) + 0.5f;

    float const ndc_x = (2.0f * (px / w)) - 1.0f;
    float const ndc_y = (2.0f * (py / h)) - 1.0f;

    float const tan_half_fovy = std::tan(cam.fov_y_radians * 0.5f);

    // IMPORTANT:
    // Camera projection in Strata flips Y in the projection matrix for Vulkan (positive viewport
    // height), so view-space y is the *negative* of ndc_y.
    float const x_view = ndc_x * aspect * tan_half_fovy;
    float const y_view = -ndc_y * tan_half_fovy;

    Vec3 const f = cam.forward();
    Vec3 const r = cam.right();
    Vec3 const u = cam.up();

    Ray ray{};
    ray.origin = cam.position;
    ray.dir    = normalize(f + r * x_view + u * y_view);
    return ray;
}

struct GpuMeshOwned
{
    strata::gfx::renderer::GpuMesh mesh{};
    bool                           valid() const noexcept
    {
        return mesh.vertex_buffer && mesh.index_buffer && mesh.index_count > 0;
    }
};

static GpuMeshOwned upload_mesh(strata::gfx::rhi::IGpuDevice&     dev,
                                std::vector<VertexP3> const&      verts,
                                std::vector<std::uint32_t> const& inds)
{
    using namespace strata::gfx::rhi;

    GpuMeshOwned out{};

    if (verts.empty() || inds.empty())
        return out;

    BufferDesc vb{};
    vb.size_bytes   = static_cast<std::uint64_t>(verts.size() * sizeof(VertexP3));
    vb.usage        = BufferUsage::Vertex | BufferUsage::Upload;
    vb.host_visible = true;

    BufferDesc ib{};
    ib.size_bytes   = static_cast<std::uint64_t>(inds.size() * sizeof(std::uint32_t));
    ib.usage        = BufferUsage::Index | BufferUsage::Upload;
    ib.host_visible = true;

    auto vb_bytes = std::as_bytes(std::span{verts.data(), verts.size()});
    auto ib_bytes = std::as_bytes(std::span{inds.data(), inds.size()});

    out.mesh.vertex_buffer = dev.create_buffer(vb, vb_bytes);
    out.mesh.index_buffer  = dev.create_buffer(ib, ib_bytes);
    out.mesh.index_count   = static_cast<std::uint32_t>(inds.size());
    out.mesh.index_type    = IndexType::UInt32;

    if (!out.valid())
    {
        // Best-effort cleanup if partial
        if (out.mesh.vertex_buffer)
            dev.wait_idle();
        dev.destroy_buffer(out.mesh.vertex_buffer);
        if (out.mesh.index_buffer)
            dev.wait_idle();
        dev.destroy_buffer(out.mesh.index_buffer);
        out = {};
    }

    return out;
}

struct EditorState
{
    strata::core::ActionMap         actions{};
    strata::gfx::renderer::Camera3D camera{};
    bool                            initialized{false};

    // Tuning
    float mouse_sensitivity{0.0025f};
    float move_speed{3.0f};
    float sprint_multiplier{3.0f};

    // Scene (CPU)
    std::vector<Aabb> boxes{};
    int               selected{-1};

    // Scene (GPU)
    GpuMeshOwned world_gpu{};
    GpuMeshOwned selected_gpu{};

    // Input edge tracking
    bool prev_lmb{false};
};

} // namespace

int main()
{
    strata::core::ApplicationConfig cfg{};
    cfg.window_desc.size  = {1280, 720};
    cfg.window_desc.title = "Strata - Level Editor (MVP)";

    cfg.device.backend       = strata::gfx::rhi::BackendType::Vulkan;
    cfg.swapchain_desc.vsync = true;

    cfg.throttle_cpu   = true;
    cfg.throttle_sleep = std::chrono::milliseconds{1};

    auto app_exp = strata::core::Application::create(cfg);
    if (!app_exp)
    {
        std::println("Failed to create Application: {}", strata::core::to_string(app_exp.error()));
        return 1;
    }

    auto& app = *app_exp;

    EditorState st{};

    // Run loop
    int const rc = app.run(
        [&st](strata::core::Application& app, strata::core::FrameContext const& ctx)
        {
            using strata::base::math::Vec3;
            using strata::base::math::length;
            using strata::base::math::normalize;

            auto& win = app.window();

            // One-time init
            if (!st.initialized)
            {
                st.camera.position = Vec3{0.0f, 1.5f, 6.0f};
                st.camera.set_yaw_pitch(0.0f, 0.0f);

                // Simple test scene: floor + 3 boxes
                st.boxes.push_back(
                    Aabb{Vec3{-6.0f, -0.1f, -6.0f}, Vec3{+6.0f, 0.0f, +6.0f}}); // floor
                st.boxes.push_back(Aabb{Vec3{-0.5f, 0.0f, -0.5f}, Vec3{+0.5f, 1.0f, +0.5f}});
                st.boxes.push_back(Aabb{Vec3{+1.5f, 0.0f, -0.25f}, Vec3{+2.5f, 0.8f, +0.75f}});
                st.boxes.push_back(Aabb{Vec3{-2.5f, 0.0f, +1.0f}, Vec3{-1.5f, 1.2f, +2.0f}});

                // Build world mesh once
                std::vector<VertexP3>      verts;
                std::vector<std::uint32_t> inds;
                verts.reserve(st.boxes.size() * 8);
                inds.reserve(st.boxes.size() * 36);

                for (auto const& b : st.boxes)
                    append_box(verts, inds, b.min, b.max);

                st.world_gpu = upload_mesh(app.device(), verts, inds);
                if (st.world_gpu.valid())
                    app.renderer().set_world_mesh(st.world_gpu.mesh);

                st.initialized = true;
            }

            // Map raw input to actions (movement/look axes)
            st.actions.update(win.input());

            // Exit on ESC
            if (st.actions.down(strata::core::Action::Exit))
            {
                win.set_cursor_mode(strata::platform::CursorMode::Normal);
                app.request_exit();
                return;
            }

            // RMB = mouse look (lock cursor). Otherwise keep cursor normal for selection.
            bool const rmb = win.input().mouse_down(strata::platform::MouseButton::Right);
            win.set_cursor_mode((win.has_focus() && rmb) ? strata::platform::CursorMode::Locked
                                                         : strata::platform::CursorMode::Normal);

            float const dt = static_cast<float>(ctx.delta_seconds);
            if (dt <= 0.0f)
            {
                app.renderer().set_camera(st.camera);
                return;
            }

            // --- Mouse look (only while RMB held) ---
            if (rmb)
            {
                float const dx = st.actions.look_x();
                float const dy = st.actions.look_y();
                st.camera.add_yaw_pitch(dx * st.mouse_sensitivity, -dy * st.mouse_sensitivity);
            }

            // --- Movement (WASD) ---
            Vec3 forward = st.camera.forward();
            forward.y    = 0.0f;
            forward      = normalize(forward);

            Vec3 right = st.camera.right();
            right.y    = 0.0f;
            right      = normalize(right);

            Vec3 move{};
            if (st.actions.down(strata::core::Action::MoveForward))
                move = move + forward;
            if (st.actions.down(strata::core::Action::MoveBack))
                move = move - forward;
            if (st.actions.down(strata::core::Action::MoveRight))
                move = move + right;
            if (st.actions.down(strata::core::Action::MoveLeft))
                move = move - right;

            float speed = st.move_speed;
            if (st.actions.down(strata::core::Action::Sprint))
                speed *= st.sprint_multiplier;

            if (length(move) > 0.0f)
            {
                move               = normalize(move);
                st.camera.position = st.camera.position + move * (speed * dt);
            }

            // Optional vertical movement
            if (st.actions.down(strata::core::Action::MoveUp))
                st.camera.position.y += speed * dt;
            if (st.actions.down(strata::core::Action::MoveDown))
                st.camera.position.y -= speed * dt;

            // --- Picking (LMB edge) ---
            bool const lmb         = win.input().mouse_down(strata::platform::MouseButton::Left);
            bool const lmb_pressed = (lmb && !st.prev_lmb);
            st.prev_lmb            = lmb;

            if (lmb_pressed && !rmb) // don't pick while in look-mode
            {
                if (win.input().mouse_pos_valid())
                {
                    auto [ww, wh] = win.window_size();
                    Ray const ray = make_mouse_ray(st.camera,
                                                   win.input().mouse_x(),
                                                   win.input().mouse_y(),
                                                   ww,
                                                   wh);

                    int   best_idx = -1;
                    float best_t   = std::numeric_limits<float>::infinity();

                    for (int i = 0; i < static_cast<int>(st.boxes.size()); ++i)
                    {
                        float t = 0.0f;
                        if (ray_intersect_aabb(ray, st.boxes[static_cast<std::size_t>(i)], t))
                        {
                            if (t < best_t)
                            {
                                best_t   = t;
                                best_idx = i;
                            }
                        }
                    }

                    if (best_idx != st.selected)
                    {
                        st.selected = best_idx;

                        if (st.selected >= 0)
                        {
                            // Rebuild selected mesh (one box)
                            auto const& b = st.boxes[static_cast<std::size_t>(st.selected)];

                            std::vector<VertexP3>      sel_v;
                            std::vector<std::uint32_t> sel_i;
                            sel_v.reserve(8);
                            sel_i.reserve(36);
                            append_box(sel_v, sel_i, b.min, b.max);

                            // --- SAFE selected-mesh buffer replacement
                            // 
                            // Vulkan rule: must not destroy buffers still referenced by 
                            // in-flight cmd buffers. 
                            // 
                            // MVP fix: stall the GPU before freeing the old selection
                            // buffers.
                            // 
                            // Later: replace this with a deferred-destruction queue
                            // keyed by per-frame fences.

                            auto const old_vb = st.selected_gpu.mesh.vertex_buffer;
                            auto const old_ib = st.selected_gpu.mesh.index_buffer;

                            // (Re)build st.selected_gpu.mesh.* here (create_buffer for new VB/IB,
                            // set index_count, etc.) 
                            // 
                            // IMPORTANT: set st.selected_gpu.mesh to the NEW
                            // buffers before destroying old ones.

                            // Now that we've swapped selection to the new buffers, safely free the
                            // old ones.
                            if (old_vb || old_ib)
                            {
                                // Prevents vkDestroyBuffer-in-use.
                                app.device().wait_idle();

                                if (old_vb)
                                    app.device().destroy_buffer(old_vb);
                                if (old_ib)
                                    app.device().destroy_buffer(old_ib);
                            }

                            st.selected_gpu = upload_mesh(app.device(), sel_v, sel_i);

                            if (st.selected_gpu.valid())
                                app.renderer().set_selected_mesh(st.selected_gpu.mesh);
                            else
                                app.renderer().clear_selected_mesh();
                        }
                        else
                        {
                            app.renderer().clear_selected_mesh();
                        }
                    }
                }
            }

            // Feed camera to renderer
            app.renderer().set_camera(st.camera);
        });

    // Cleanup (best-effort)
    {
        auto& dev = app.device();
        dev.wait_idle();
        if (st.selected_gpu.mesh.vertex_buffer)
            dev.destroy_buffer(st.selected_gpu.mesh.vertex_buffer);
        if (st.selected_gpu.mesh.index_buffer)
            dev.destroy_buffer(st.selected_gpu.mesh.index_buffer);
        if (st.world_gpu.mesh.vertex_buffer)
            dev.destroy_buffer(st.world_gpu.mesh.vertex_buffer);
        if (st.world_gpu.mesh.index_buffer)
            dev.destroy_buffer(st.world_gpu.mesh.index_buffer);
    }

    return rc;
}
