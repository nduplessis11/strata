// -----------------------------------------------------------------------------
// tools/level_editor/src/main.cpp
//
// MVP Level Editor:
//   - Reuse Strata engine rendering (Renderer/BasicPass).
//   - Camera: WASD + mouse look (hold RMB to lock cursor).
//   - Picking: LMB selects the box under cursor via ray vs AABB.
// -----------------------------------------------------------------------------
//
// NOTE (DIAGNOSTIC BUILD):
// This adds additional logging around:
//   - RMB transitions (Locked/Normal)
//   - Camera yaw/pitch + basis vectors
//   - Mouse->NDC->view-space ray construction
//   - AABB hit results + "project ray point back to screen" sanity check
//
// All non-logging behavior is kept the same.
//

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

// ----------------------------- DIAGNOSTIC TOGGLES ----------------------------
//
// Flip to 0 to silence all added logging.
#ifndef STRATA_LEVEL_EDITOR_PICK_DIAG
#define STRATA_LEVEL_EDITOR_PICK_DIAG 1
#endif

// Print full per-pick details (ray, camera, per-box hits).
#ifndef STRATA_LEVEL_EDITOR_PICK_DIAG_VERBOSE
#define STRATA_LEVEL_EDITOR_PICK_DIAG_VERBOSE 1
#endif

// Perform a sanity check: project a point along the computed ray back to screen
// and compare against the originating mouse pixel.
#ifndef STRATA_LEVEL_EDITOR_PICK_DIAG_PROJECT_CHECK
#define STRATA_LEVEL_EDITOR_PICK_DIAG_PROJECT_CHECK 1
#endif

#if STRATA_LEVEL_EDITOR_PICK_DIAG
static std::int64_t g_pick_seq = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static bool is_finite(Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static void log_vec3(char const* name, Vec3 v)
{
    std::println("  {} = ({:.6f}, {:.6f}, {:.6f})", name, v.x, v.y, v.z);
}

[[maybe_unused]] static void log_mat4_compact(char const* name, strata::base::math::Mat4 const& m)
{
    // Mat4 is stored column-major as m[col][row]. Print as rows for readability.
    std::println("  {} =", name);
    for (int r = 0; r < 4; ++r)
    {
        std::println("    [{: .6f} {: .6f} {: .6f} {: .6f}]",
                     m.m[0][r],
                     m.m[1][r],
                     m.m[2][r],
                     m.m[3][r]);
    }
}

static bool project_world_to_screen(strata::gfx::renderer::Camera3D const& cam,
                                    Vec3                                   world,
                                    std::int32_t                           width,
                                    std::int32_t                           height,
                                    float&                                 out_sx,
                                    float&                                 out_sy,
                                    float&                                 out_ndc_x,
                                    float&                                 out_ndc_y,
                                    float&                                 out_ndc_z)
{
    using strata::base::math::Mat4;
    using strata::base::math::mul;
    using strata::base::math::Vec4;

    float const w = (width > 0) ? static_cast<float>(width) : 1.0f;
    float const h = (height > 0) ? static_cast<float>(height) : 1.0f;

    float const aspect = w / h;

    Mat4 const vp = cam.view_proj(aspect, true);

    Vec4 const clip = mul(vp, Vec4{world, 1.0f});
    if (!std::isfinite(clip.x) ||
        !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) ||
        !std::isfinite(clip.w))
    {
        return false;
    }

    if (std::abs(clip.w) < 1e-6f)
        return false;

    float const invw = 1.0f / clip.w;

    out_ndc_x = clip.x * invw;
    out_ndc_y = clip.y * invw;
    out_ndc_z = clip.z * invw;

    // Vulkan viewport with positive height:
    // screen_x = (ndc_x * 0.5 + 0.5) * w
    // screen_y = (ndc_y * 0.5 + 0.5) * h
    out_sx = (out_ndc_x * 0.5f + 0.5f) * w;
    out_sy = (out_ndc_y * 0.5f + 0.5f) * h;

    return std::isfinite(out_sx) && std::isfinite(out_sy);
}
#endif

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

#if STRATA_LEVEL_EDITOR_PICK_DIAG
    bool prev_rmb{false};
#endif
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

#if STRATA_LEVEL_EDITOR_PICK_DIAG
            {
                bool const rmb_pressed  = (rmb && !st.prev_rmb);
                bool const rmb_released = (!rmb && st.prev_rmb);
                st.prev_rmb             = rmb;

                if (rmb_pressed || rmb_released)
                {
                    auto [ww, wh] = win.window_size();
                    auto [fw, fh] = win.framebuffer_size();

                    std::println("[pickdiag] RMB {}  focus={}  cursor_mode->{}",
                                 rmb_pressed ? "PRESSED" : "RELEASED",
                                 win.has_focus(),
                                 (win.has_focus() && rmb) ? "Locked" : "Normal");
                    std::println(
                        "  window_size=({},{}) framebuffer_size=({},{}) mouse_valid={} mouse=({},"
                        "{})",
                        ww,
                        wh,
                        fw,
                        fh,
                        win.input().mouse_pos_valid(),
                        win.input().mouse_x(),
                        win.input().mouse_y());
                    std::println("  camera yaw={:.6f} pitch={:.6f}",
                                 st.camera.yaw_radians,
                                 st.camera.pitch_radians);
                }
            }
#endif

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

#if STRATA_LEVEL_EDITOR_PICK_DIAG
                    {
                        ++g_pick_seq;

                        auto [fw, fh] = win.framebuffer_size();

                        std::println("\n[pickdiag] PICK #{}  mouse=({},{})  window=({},{}) "
                                     "framebuffer=({},{})",
                                     g_pick_seq,
                                     win.input().mouse_x(),
                                     win.input().mouse_y(),
                                     ww,
                                     wh,
                                     fw,
                                     fh);

                        // Recompute NDC/view-space scalars to print alongside the ray.
                        float const w      = (ww > 0) ? static_cast<float>(ww) : 1.0f;
                        float const h      = (wh > 0) ? static_cast<float>(wh) : 1.0f;
                        float const aspect = w / h;

                        float const px = static_cast<float>(win.input().mouse_x()) + 0.5f;
                        float const py = static_cast<float>(win.input().mouse_y()) + 0.5f;

                        float const ndc_x = (2.0f * (px / w)) - 1.0f;
                        float const ndc_y = (2.0f * (py / h)) - 1.0f;

                        float const tan_half_fovy = std::tan(st.camera.fov_y_radians * 0.5f);
                        float const x_view        = ndc_x * aspect * tan_half_fovy;
                        float const y_view        = -ndc_y * tan_half_fovy;

                        std::println("  ndc=({:.6f},{:.6f})  aspect={:.6f}  "
                                     "tan_half_fovy={:.6f}  view_xy=({:.6f},{:.6f})",
                                     ndc_x,
                                     ndc_y,
                                     aspect,
                                     tan_half_fovy,
                                     x_view,
                                     y_view);

                        std::println("  camera pos=({:.6f},{:.6f},{:.6f}) yaw={:.6f} pitch={:.6f}",
                                     st.camera.position.x,
                                     st.camera.position.y,
                                     st.camera.position.z,
                                     st.camera.yaw_radians,
                                     st.camera.pitch_radians);

                        Vec3 const f = st.camera.forward();
                        Vec3 const r = st.camera.right();
                        Vec3 const u = st.camera.up();

                        log_vec3("cam.forward", f);
                        log_vec3("cam.right  ", r);
                        log_vec3("cam.up     ", u);

                        log_vec3("ray.origin ", ray.origin);
                        log_vec3("ray.dir    ", ray.dir);

                        if (!is_finite(ray.dir) || (length(ray.dir) < 0.5f))
                        {
                            std::println("  [WARN] ray.dir is not finite or suspiciously small!");
                        }

#if STRATA_LEVEL_EDITOR_PICK_DIAG_PROJECT_CHECK
                        {
                            // Project a point along the ray and ensure it maps back to the mouse
                            // pixel. If this fails, the issue is in ray construction / convention
                            // mismatch.
                            Vec3 const p_test = ray.origin + ray.dir * 10.0f;

                            float sx = 0, sy = 0, nx = 0, ny = 0, nz = 0;
                            if (project_world_to_screen(st.camera,
                                                        p_test,
                                                        ww,
                                                        wh,
                                                        sx,
                                                        sy,
                                                        nx,
                                                        ny,
                                                        nz))
                            {
                                float const dx =
                                    sx - (static_cast<float>(win.input().mouse_x()) + 0.5f);
                                float const dy =
                                    sy - (static_cast<float>(win.input().mouse_y()) + 0.5f);

                                std::println("  ray->screen check: P=origin+dir*10 => "
                                             "ndc=({:.6f},{:.6f},{:.6f}) screen=({:.3f},{:.3f}) "
                                             "delta=({:.3f},{:.3f})",
                                             nx,
                                             ny,
                                             nz,
                                             sx,
                                             sy,
                                             dx,
                                             dy);
                            }
                            else
                            {
                                std::println("  [WARN] ray->screen check failed (clip.w ~ 0 or "
                                             "non-finite).");
                            }

                        // Also project the centers of all boxes so you can see where the engine
                        // thinks they land in screen space (helps validate camera math).
#if STRATA_LEVEL_EDITOR_PICK_DIAG_VERBOSE
                            {
                                for (int i = 0; i < static_cast<int>(st.boxes.size()); ++i)
                                {
                                    auto const& b = st.boxes[static_cast<std::size_t>(i)];
                                    Vec3 const  c = (b.min + b.max) * 0.5f;

                                    float csx = 0, csy = 0, cnx = 0, cny = 0, cnz = 0;
                                    if (project_world_to_screen(st.camera,
                                                                c,
                                                                ww,
                                                                wh,
                                                                csx,
                                                                csy,
                                                                cnx,
                                                                cny,
                                                                cnz))
                                    {
                                        std::println(
                                            "  box[{}] center world=({:.3f},{:.3f},{:.3f}) "
                                            "-> ndc=({:.3f},{:.3f},{:.3f}) screen=({:.1f},{:.1f})",
                                            i,
                                            c.x,
                                            c.y,
                                            c.z,
                                            cnx,
                                            cny,
                                            cnz,
                                            csx,
                                            csy);
                                    }
                                }
                            }
#endif
                        }
#endif // STRATA_LEVEL_EDITOR_PICK_DIAG_PROJECT_CHECK

                        // Optional: plane intersection with y=0 for intuition
                        if (std::abs(ray.dir.y) > 1e-6f)
                        {
                            float const t = (0.0f - ray.origin.y) / ray.dir.y;
                            if (t > 0.0f && std::isfinite(t))
                            {
                                Vec3 const p = ray.origin + ray.dir * t;
                                std::println(
                                    "  ray hits plane y=0 at t={:.6f} -> ({:.6f},{:.6f},{:.6f})",
                                    t,
                                    p.x,
                                    p.y,
                                    p.z);
                            }
                        }
                    }
#endif // STRATA_LEVEL_EDITOR_PICK_DIAG

                    int   best_idx = -1;
                    float best_t   = std::numeric_limits<float>::infinity();

                    for (int i = 0; i < static_cast<int>(st.boxes.size()); ++i)
                    {
                        float t = 0.0f;
                        if (ray_intersect_aabb(ray, st.boxes[static_cast<std::size_t>(i)], t))
                        {
#if STRATA_LEVEL_EDITOR_PICK_DIAG && STRATA_LEVEL_EDITOR_PICK_DIAG_VERBOSE
                            {
                                auto const& b = st.boxes[static_cast<std::size_t>(i)];
                                std::println(
                                    "  hit box[{}] t={:.6f}  aabb.min=({:.3f},{:.3f},{:.3f}) "
                                    "aabb.max=({:.3f},{:.3f},{:.3f})",
                                    i,
                                    t,
                                    b.min.x,
                                    b.min.y,
                                    b.min.z,
                                    b.max.x,
                                    b.max.y,
                                    b.max.z);
                            }
#endif

                            if (t < best_t)
                            {
                                best_t   = t;
                                best_idx = i;
                            }
                        }
                    }

#if STRATA_LEVEL_EDITOR_PICK_DIAG
                    std::println("  best hit idx={} t={}", best_idx, best_t);
#endif

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
#if STRATA_LEVEL_EDITOR_PICK_DIAG
                else
                {
                    std::println("[pickdiag] PICK attempted but mouse_pos_valid() == false");
                }
#endif
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
