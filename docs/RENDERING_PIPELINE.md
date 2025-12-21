# Rendering Pipeline

This document describes **how Strata renders a frame today**, from the game loop down to Vulkan calls.

It is intentionally scoped to the **current** implementation (single pass, fullscreen triangle). As the engine grows (command recording in the renderer, multiple frames-in-flight, etc.), this doc should evolve alongside it.

---

## TL;DR

Today’s frame path looks like this:

1. `core::Application::run()` drives the loop and calls a helper:
   - `gfx::renderer::draw_frame_and_handle_resize(device, swapchain, renderer, framebuffer_size)`
2. `Render2D::draw_frame()` is currently **“present-only”**:
   - it delegates the entire frame to `IGpuDevice::present(swapchain)`
3. The Vulkan backend (`vk::VkGpuDevice::present`) does everything:
   - **wait fence → acquire image → record commands → submit → present**
4. If the swapchain is suboptimal/out-of-date:
   - the engine calls `device.wait_idle()`,
   - recreates the swapchain,
   - rebuilds the pipeline (via a fresh `Render2D`).

---

## Key design choices (today)

- **Vulkan is contained** in `engine/gfx/backend/vk/*` (`namespace strata::gfx::vk`).
- The renderer (`Render2D`) depends only on the **RHI** (`IGpuDevice`) and opaque handles.
- The backend currently uses a **single primary command buffer** and a **single in-flight fence**.
- Rendering uses **Vulkan 1.3 dynamic rendering** (`vkCmdBeginRendering`) and **dynamic viewport/scissor**.
- Barriers use **Synchronization2** (`vkCmdPipelineBarrier2`).
- The first “render pass” is a **fullscreen triangle** with a solid clear + draw.

---

## Frame driver: `core::Application`

### Startup (`Application::create`)

`Application::create(config)` performs:

1. Create `platform::Window` using `config.window_desc`
2. Create a `platform::WsiHandle` from the window: `window.native_wsi()`
3. Create device via RHI factory:
   - `gfx::rhi::create_device(config.device, wsi_handle)`
4. Choose initial framebuffer size and build a `SwapchainDesc`
5. Create swapchain:
   - `swapchain = device->create_swapchain(sc_desc, wsi_handle)`
6. Create renderer:
   - `gfx::renderer::Render2D renderer{ *device, swapchain }`

Ownership is explicit (PImpl inside `Application`):
- `Application::Impl` owns the window, the `WsiHandle` value, the device, the swapchain handle, and the renderer.
- `~Application::Impl` calls `device->wait_idle()` before member destruction so backend resources are safe to destroy.

> Note: `platform::WsiHandle` is not a Vulkan surface. The Vulkan backend creates and owns `VkSurfaceKHR`.

### Main loop (`Application::run`)

Per iteration:

1. `window.poll_events()`
2. Compute `FrameContext { frame_index, delta_seconds }`
3. Optional user tick callback: `tick(*this, ctx)`
4. Query framebuffer size and clamp:
   - `auto framebuffer = clamp_framebuffer(fbw, fbh)`
5. Render + resize handling:
   - `draw_frame_and_handle_resize(*device, swapchain, renderer, framebuffer)`
6. Optional CPU throttle sleep

On exit, `device->wait_idle()` is called once more.

---

## Renderer frontend: `gfx::renderer::Render2D`

`Render2D` is the “frontend” renderer object. Today it owns:
- `rhi::SwapchainHandle swapchain_` (opaque)
- `rhi::PipelineHandle pipeline_` (opaque; currently functions as a “valid/created” token)
- a non-owning `rhi::IGpuDevice* device_`

### Pipeline setup (`Render2D::Render2D`)
Constructor creates a simple pipeline:
- vertex shader: `shaders/fullscreen_triangle.vert.spv`
- fragment shader: `shaders/flat_color.frag.spv`
- no blending

and calls:
- `pipeline_ = device_->create_pipeline(desc);`

### Frame rendering (`Render2D::draw_frame`)
Today, `draw_frame()` is intentionally simple:
- validates `device_`, `swapchain_`, `pipeline_`
- delegates frame work to the backend:
  - `return device_->present(swapchain_);`

In other words: **the backend owns command recording for now.**

### Resize helper: `draw_frame_and_handle_resize`
This helper implements the current resize policy:
- If framebuffer is zero-area: return `Ok` (minimized window)
- `result = renderer.draw_frame()`
  - `Ok` → done
  - `Error` → fatal render error
  - anything else (`Suboptimal` / `ResizeNeeded`) → resize path

Resize path:
1. `device.wait_idle()` (simple + safe, but stalls)
2. Build `SwapchainDesc` from the framebuffer (**vsync hard-coded true today**)
3. `device.resize_swapchain(swapchain, sc_desc)`
4. Rebuild renderer (recreates pipeline):
   - `renderer = Render2D{ device, swapchain };`

---

## RHI contract: `gfx::rhi::IGpuDevice`

Swapchain-related:
- `create_swapchain(desc, wsi) -> SwapchainHandle`
- `present(swapchain) -> FrameResult`
- `resize_swapchain(swapchain, desc) -> FrameResult`

Pipeline-related:
- `create_pipeline(desc) -> PipelineHandle`
- `destroy_pipeline(handle)`

Commands/submission (not used yet):
- `begin_commands() -> CommandBufferHandle`
- `end_commands(cmd)`
- `submit(SubmitDesc{ command_buffer })`

---

## Vulkan backend bring-up: instance, surface, and device

This section is “pre-frame” but it matters because it defines the constraints the frame code relies on.

### WSI bridge: instance extensions + surface creation
Strata uses a platform-neutral `platform::WsiHandle` (`std::variant`) and a small Vulkan WSI bridge:

- `vk_wsi_bridge::required_instance_extensions(wsi)` returns a static list of platform-required instance extensions:
  - Win32: `VK_KHR_surface`, `VK_KHR_win32_surface`
  - X11: `VK_KHR_surface`, `VK_KHR_xlib_surface`
- `vk_wsi_bridge::create_surface(instance, wsi)` uses `std::visit` to call:
  - `vkCreateWin32SurfaceKHR` on Win32
  - `vkCreateXlibSurfaceKHR` on X11
  - (Wayland is planned)

If the compiled bridge TU doesn’t match the variant alternative, it returns `VK_NULL_HANDLE`.

### `VkInstanceWrapper`
The Vulkan backend creates:
- `VkInstance` (API version 1.3)
- optional debug messenger (`VK_EXT_debug_utils`) when validation is enabled (`STRATA_VK_VALIDATION`)
- `VkSurfaceKHR` (via `vk_wsi_bridge::create_surface`)

### `VkDeviceWrapper`: selection + required features
Device selection requires:
- queue families: graphics + present
- device extension: `VK_KHR_swapchain`

It also requires Vulkan 1.3 features:
- `dynamicRendering`
- `synchronization2`

Those requirements match the frame code:
- dynamic rendering: `vkCmdBeginRendering`
- sync2 barriers: `vkCmdPipelineBarrier2` + `VkImageMemoryBarrier2`

---

## Vulkan backend: `vk::VkGpuDevice`

### Backend state (relevant pieces)
- `VkSwapchainWrapper swapchain_` (owns `VkSwapchainKHR` + image views)
- `VkCommandBufferPool command_pool_`
- `VkCommandBuffer primary_cmd_` (single primary command buffer)
- `FrameSync frame_sync_`
  - `image_available` semaphore (single)
  - `in_flight` fence (single, signaled initially)
  - `render_finished_per_image` semaphores (one per swapchain image)
- `BasicPipeline basic_pipeline_` (owns `VkPipelineLayout` + `VkPipeline`)
- `swapchain_image_layouts_` (tracks per-image layout state)

### Swapchain creation (`create_swapchain`)
`VkGpuDevice::create_swapchain(desc, ...)`:
1. `wait_idle()`
2. destroy existing swapchain + views
3. `swapchain_.init(...)`
   - chooses surface format (prefers `VK_FORMAT_B8G8R8A8_UNORM` + `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`)
   - chooses present mode:
     - FIFO (vsync on)
     - mailbox if vsync off and supported (future use)
   - creates swapchain and image views
4. initialize `swapchain_image_layouts_` to `VK_IMAGE_LAYOUT_UNDEFINED`
5. create per-image render-finished semaphores
6. return `SwapchainHandle{1}` (only one swapchain exists today)

### Swapchain resize (`resize_swapchain`)
Resize mirrors creation:
- `wait_idle()`
- cleanup + recreate swapchain wrapper + views
- recreate per-image render-finished semaphores
- invalidate `basic_pipeline_` so it will be rebuilt for the new swapchain format

---

## The actual frame: `VkGpuDevice::present`

`VkGpuDevice::present(...)` implements the current frame pipeline.

### 0) Ensure pipeline exists
If `basic_pipeline_` is invalid (e.g., after resize):
- `basic_pipeline_ = create_basic_pipeline(device, swapchain_.image_format());`

This is also created when the renderer calls `IGpuDevice::create_pipeline(...)`, but `present()` is allowed to rebuild lazily if needed.

### 1) Wait previous frame
Single fence model:
- `vkWaitForFences(device, 1, &in_flight, VK_TRUE, timeout)`
- `vkResetFences(device, 1, &in_flight)`

### 2) Acquire swapchain image
- `vkAcquireNextImageKHR(..., image_available, ..., &image_index)`
- `VK_ERROR_OUT_OF_DATE_KHR` → `FrameResult::ResizeNeeded`
- `VK_SUBOPTIMAL_KHR` → continue rendering and let caller decide

### 3) Record commands into `primary_cmd_`
- `vkResetCommandBuffer(primary_cmd_, 0)`
- `vkBeginCommandBuffer(primary_cmd_, ...)`

### 4) Transition swapchain image to color attachment (Synchronization2)
Strata tracks per-image layout state in `swapchain_image_layouts_`.
- old layout: `UNDEFINED` (first use) or `PRESENT_SRC_KHR` (normal)
- barrier: `old_layout → COLOR_ATTACHMENT_OPTIMAL`
- `vkCmdPipelineBarrier2(... VkImageMemoryBarrier2 ...)`

### 5) Dynamic rendering pass (single color attachment)
- clear color: `{ 0.6f, 0.4f, 0.8f, 1.0f }`
- `vkCmdBeginRendering(...)` with one `VkRenderingAttachmentInfo`
- bind `basic_pipeline_`
- set dynamic viewport/scissor to swapchain extent
- `vkCmdDraw(..., 3, 1, 0, 0)` (fullscreen triangle)

### 6) Transition to present
- barrier: `COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR`
- update tracking:
  - `swapchain_image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`

### 7) Submit + present
Submit:
- wait semaphore: `image_available`
- wait stage: `COLOR_ATTACHMENT_OUTPUT`
- signal semaphore: `render_finished_per_image[image_index]`
- fence: `in_flight`

Present:
- `vkQueuePresentKHR(..., wait = render_finished)`
- map results:
  - `VK_ERROR_OUT_OF_DATE_KHR` → `ResizeNeeded`
  - `VK_SUBOPTIMAL_KHR` → `Suboptimal`
  - otherwise error → `Error`
  - success → `Ok`

---

## Basic pipeline: `vk_pipeline_basic.*`

Strata’s initial pipeline is built by:
- `create_basic_pipeline(VkDevice device, VkFormat color_format)`

Key details:
- Loads SPIR-V from disk:
  - `shaders/fullscreen_triangle.vert.spv`
  - `shaders/flat_color.frag.spv`
- Creates shader modules, builds pipeline, then destroys shader modules
- No vertex buffers:
  - vertex positions are generated using `gl_VertexIndex`
- Uses dynamic viewport + scissor
- Uses dynamic rendering (`VkPipelineRenderingCreateInfo` specifies the swapchain color format)
- `renderPass = VK_NULL_HANDLE`

Because the pipeline depends on swapchain format, it is recreated when the swapchain is recreated/invalidated.

---

## Synchronization model (today)

### CPU/GPU pacing
- Single fence `in_flight` → CPU blocks each frame until GPU completes last submission.
- This is simple and good for early development, but limits throughput.

### GPU-GPU ordering
- `image_available` semaphore ensures rendering waits until the image is available.
- `render_finished[image_index]` semaphore ensures presentation waits until rendering finishes.

### Swapchain image layouts
- Tracked in `swapchain_image_layouts_` to choose correct `oldLayout` in barriers.
- Current model assumes images are either `UNDEFINED` (first use) or `PRESENT_SRC_KHR` (after present).

---

## Current simplifications (intentional)
- **One command buffer** reused every frame.
- **One frame in flight** (single fence).
- **One graphics pipeline** (fullscreen triangle).
- **No vertex/index buffers** in the frame loop.
- **No depth**, no MSAA, no post-processing.
- **Renderer does not own command recording** (backend does).
- Swapchain recreation is done with `wait_idle()` (safe, not optimal).

---

## Planned evolution points

1. **Move command recording out of `present()`**
   - `Render2D` (or a frame-graph) should:
     - `begin_commands()`
     - record draw passes
     - `end_commands()`
     - `submit()`
     - `present()`

2. **Multiple frames-in-flight**
   - Replace single fence with N-frame ring:
     - per-frame command buffers
     - per-frame fences
     - per-frame image-available semaphores
     - optional timeline semaphore

3. **Pipeline lifetime rules**
   - Today: pipeline recreated on resize via a fresh `Render2D`, and backend can lazily rebuild.
   - Later: pipeline cache, descriptor sets, shader hot-reload, etc.

4. **Swapchain recreation policy**
   - Avoid `wait_idle()` by tracking in-flight resources (requires multi-frame model).

---

## Debugging tips

- If you see black frames:
  - confirm pipeline is valid (`basic_pipeline_.valid()`)
  - confirm swapchain format matches pipeline format
  - confirm dynamic rendering is enabled and pipeline is created with the correct format
- If resize loops:
  - ensure framebuffer size isn’t constantly changing (HiDPI scale, etc.)
  - ensure you don’t call resize when minimized (Strata already skips when size is 0)
- If validation complains about layouts:
  - ensure `swapchain_image_layouts_` is initialized on create/resize
  - ensure barriers use correct `oldLayout` and `newLayout`
- If instance creation fails on a platform:
  - verify `vk_wsi_bridge::required_instance_extensions` includes the correct WSI extensions for that platform

---

## Appendix: Type mapping (Engine ↔ RHI ↔ Vulkan)

| Concept | Engine / RHI type | Vulkan implementation |
|---|---|---|
| Window | `platform::Window` | OS window handles (pImpl) |
| WSI handle | `platform::WsiHandle` | consumed by `vk_wsi_bridge::create_surface` |
| Surface | *(backend-owned)* | `VkSurfaceKHR` inside `VkInstanceWrapper` |
| Device interface | `rhi::IGpuDevice` | `vk::VkGpuDevice` |
| Swapchain handle | `rhi::SwapchainHandle` | `vk::VkSwapchainWrapper` |
| Pipeline handle | `rhi::PipelineHandle` | `vk::BasicPipeline` |
| Present | `IGpuDevice::present()` | acquire → record → submit → present |
