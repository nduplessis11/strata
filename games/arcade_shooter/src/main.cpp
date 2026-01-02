// -----------------------------------------------------------------------------
// games/arcade_shooter/src/main.cpp
//
// Purpose:
//   Entry point for the arcade shooter sample application.
//
// PR9:
//   - Input-driven camera controls (mouse look + WASD).
//   - Clean layering:
//       platform::Window owns raw InputState
//       core::ActionMap maps raw input to actions
//       game updates Camera3D and passes it to renderer
// -----------------------------------------------------------------------------

#include "strata/core/action_map.h"
#include "strata/core/application.h"

#include "strata/base/math.h"

#include <print>

namespace
{

struct GameState
{
    strata::core::ActionMap         actions{};
    strata::gfx::renderer::Camera3D camera{};
    bool                            initialized{false};

    // Tuning
    float mouse_sensitivity{0.0025f}; // radians per pixel
    float move_speed{3.0f};           // units/sec
    float sprint_multiplier{3.0f};
};

} // namespace

int main()
{
    strata::core::ApplicationConfig cfg{};
    cfg.window_desc.size  = {1280, 720};
    cfg.window_desc.title = "Strata - Spinning Cube";

    cfg.device.backend       = strata::gfx::rhi::BackendType::Vulkan;
    cfg.swapchain_desc.vsync = true;

    cfg.throttle_cpu   = true;
    cfg.throttle_sleep = std::chrono::milliseconds{1};

    auto app = strata::core::Application::create(cfg);
    if (!app)
    {
        std::println("Failed to create Application: {}", strata::core::to_string(app.error()));
        return 1;
    }

    GameState state;

    return app->run(
        [&state](strata::core::Application& app, strata::core::FrameContext const& ctx)
        {
            using strata::base::math::Vec3;
            using strata::base::math::length;
            using strata::base::math::normalize;

            // One-time camera init (game-owned).
            if (!state.initialized)
            {
                state.camera.position = Vec3{0.0f, 0.0f, 3.0f};
                state.camera.set_yaw_pitch(0.0f, 0.0f);
                state.initialized = true;
            }

            // Update actions from raw input.
            state.actions.update(app.window().input());

            // Exit on ESC.
            if (state.actions.down(strata::core::Action::Exit))
            {
                app.request_exit();
                return;
            }

            float const dt = static_cast<float>(ctx.delta_seconds);
            // First frames may have zero dt.
            // Throttling may also produce zero dt.
            // Minimized/unfocused windows may have zero dt.
            if (dt <= 0.0f)
            {
                app.renderer().set_camera(state.camera);
                return;
            }

            // --- Mouse look (yaw/pitch) -----------------------------------------
            //
            // Win32 and X11 both report mouse Y increasing downward in window coords.
            // We invert Y so "move mouse up" => "look up".
            float const dx = state.actions.look_x();
            float const dy = state.actions.look_y();

            state.camera.add_yaw_pitch(dx * state.mouse_sensitivity, -dy * state.mouse_sensitivity);

            // --- WASD movement --------------------------------------------------
            //
            // "Walk" style: movement is planar in XZ (ignores pitch).
            Vec3 forward = state.camera.forward();
            forward.y    = 0.0f;
            forward      = normalize(forward);

            Vec3 right = state.camera.right();
            right.y    = 0.0f;
            right      = normalize(right);

            Vec3 move{};
            if (state.actions.down(strata::core::Action::MoveForward))
                move = move + forward;
            if (state.actions.down(strata::core::Action::MoveBack))
                move = move - forward;
            if (state.actions.down(strata::core::Action::MoveRight))
                move = move + right;
            if (state.actions.down(strata::core::Action::MoveLeft))
                move = move - right;

            float speed = state.move_speed;
            if (state.actions.down(strata::core::Action::Sprint))
                speed *= state.sprint_multiplier;

            if (length(move) > 0.0f)
            {
                move                  = normalize(move);
                state.camera.position = state.camera.position + move * (speed * dt);
            }

            // Optional vertical movement (fly up/down) using Space/Ctrl.
            if (state.actions.down(strata::core::Action::MoveUp))
                state.camera.position.y += speed * dt;
            if (state.actions.down(strata::core::Action::MoveDown))
                state.camera.position.y -= speed * dt;

            // Feed camera into renderer (renderer consumes; game owns control logic).
            app.renderer().set_camera(state.camera);
        });
}
