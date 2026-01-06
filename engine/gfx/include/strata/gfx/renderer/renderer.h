// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/renderer/renderer.h
//
// Purpose:
//   Public renderer facade owned by core::Application.
//   Owns a RenderScene (what to draw) and a RenderGraph (how to draw).
// -----------------------------------------------------------------------------

#pragma once

#include <expected>

#include "strata/gfx/renderer/render_graph.h"
#include "strata/gfx/renderer/render_scene.h"

namespace strata::base
{
class Diagnostics;
}

namespace strata::gfx::renderer
{

using RendererError = RenderGraphError;

class Renderer
{
  public:
    [[nodiscard]]
    static std::expected<Renderer, RendererError> create(base::Diagnostics&   diagnostics,
                                                         rhi::IGpuDevice&     device,
                                                         rhi::SwapchainHandle swapchain);

    Renderer(Renderer&&) noexcept            = default;
    Renderer& operator=(Renderer&&) noexcept = default;

    Renderer(Renderer const&)            = delete;
    Renderer& operator=(Renderer const&) = delete;

    ~Renderer() = default;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return graph_.is_valid();
    }

    // Scene setters (game/editor layer uses these).
    void set_camera(Camera3D const& c) noexcept
    {
        scene_.set_camera(c);
    }
    [[nodiscard]] Camera3D const& camera() const noexcept
    {
        return scene_.camera();
    }

    void set_world_mesh(GpuMesh m) noexcept
    {
        scene_.set_world_mesh(m);
    }
    void set_selected_mesh(GpuMesh m) noexcept
    {
        scene_.set_selected_mesh(m);
    }
    void clear_selected_mesh() noexcept
    {
        scene_.clear_selected_mesh();
    }

    // Frame driving (core::Application calls these).
    rhi::FrameResult draw_frame()
    {
        return graph_.draw_frame(scene_);
    }

    rhi::FrameResult recreate_pipeline()
    {
        return graph_.recreate_pipeline();
    }

    void on_before_swapchain_resize() noexcept
    {
        graph_.on_before_swapchain_resize();
    }

  private:
    explicit Renderer(RenderGraph&& g) noexcept
          : graph_(std::move(g))
    {
    }

    RenderScene scene_{};
    RenderGraph graph_;
};

} // namespace strata::gfx::renderer
