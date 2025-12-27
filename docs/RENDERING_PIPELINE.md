# Rendering Pipeline

This document describes **how Strata renders a frame**, from the game loop down to Vulkan calls.

It is intentionally scoped to the **current** implementation (single pass, fullscreen triangle). As the engine grows (descriptor sets, resource registries, etc.), this doc should evolve alongside it.

---

## TL;DR

The frame path looks like this:

1. `core::Application::run()` drives the loop and calls a helper:
   - `gfx::renderer::draw_frame_and_handle_resize(device, swapchain, renderer, framebuffer_size, diagnostics)`
2. `Render2D::draw_frame()` now owns the frame:
   - **acquire → begin cmd → record pass → end cmd → submit → present**
3. The Vulkan backend (`vk::VkGpuDevice`) provides the building blocks:
   - `acquire_next_image`, `begin_commands`, `cmd_*`, `end_commands`, `submit`, `present`
4. If the swapchain is suboptimal/out-of-date:
   - the engine calls `device.wait_idle()`,
   - recreates the swapchain,
   - asks the existing renderer to recreate its pipeline (via `renderer.recreate_pipeline()`, keeping UBO + descriptor resources).

---

## Key design choices

- **Vulkan is contained** in `engine/gfx/backend/vk/*` (`namespace strata::gfx::vk`).
- The renderer (`Render2D`) depends only on the **RHI** (`IGpuDevice`), `base::Diagnostics`, and opaque handles.
- The backend uses a **frames-in-flight ring** (per-frame command buffers + fences + image-available semaphores).
- Rendering uses **Vulkan 1.3 dynamic rendering** (`vkCmdBeginRendering`) and **dynamic viewport/scissor**.
- Barriers use **Synchronization2** (`vkCmdPipelineBarrier2`).
- The first “render pass” is a **fullscreen triangle** with a solid clear + draw.
- `Render2D` now **records commands explicitly** via the RHI command interface.

---

## Frame driver: `core::Application`

### Startup (`Application::create`)

`Application::create(config)` performs:

1. Create `base::Diagnostics`
   - `auto diagnostics = std::make_unique<base::Diagnostics>();`
2. Create `platform::Window` using `config.window_desc`
   - `platform::Window window{ *diagnostics, config.window_desc }`
3. Create a `platform::WsiHandle` from the window: `window.native_wsi()`
4. Create device via RHI factory:
   - `gfx::rhi::create_device(*diagnostics, config.device, wsi_handle)`
5. Choose initial framebuffer size and build a `SwapchainDesc`
6. Create swapchain:
   - `swapchain = device->create_swapchain(sc_desc, wsi_handle)`
7. Create renderer:
   - `gfx::renderer::Render2D renderer{ *diagnostics, *device, swapchain }`


Ownership is explicit (PImpl inside `Application`):
- `Application::Impl` owns the diagnostics, the window, the `WsiHandle` value, the device, the swapchain handle, and the renderer.
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
   - `draw_frame_and_handle_resize(*device, swapchain, renderer, framebuffer, *diagnostics)`
6. Optional CPU throttle sleep

On exit, `device->wait_idle()` is called once more.

---

## Renderer frontend: `gfx::renderer::Render2D`

`Render2D` is the “frontend” renderer object. It owns:
- `rhi::SwapchainHandle swapchain_` (opaque)
- `rhi::PipelineHandle pipeline_` (opaque; currently functions as a “valid/created” token)
- `rhi::DescriptorSetLayoutHandle ubo_layout_` (opaque)
- `rhi::DescriptorSetHandle ubo_set_` (opaque)
- `rhi::BufferHandle ubo_buffer_` (opaque; host-visible uniform buffer)
- a non-owning `base::Diagnostics* diagnostics_`
- a non-owning `rhi::IGpuDevice* device_`

### Pipeline setup (`Render2D::Render2D`)
Constructor (given a `base::Diagnostics&`, `IGpuDevice&`, and `SwapchainHandle`) creates a simple pipeline:
- vertex shader: `shaders/fullscreen_triangle.vert.spv`
- fragment shader: `shaders/flat_color.frag.spv`
- no blending
- descriptor set layout: set 0, binding 0 = uniform buffer (fragment-visible)

and calls:
- `ubo_layout_ = device_->create_descriptor_set_layout(layout_desc);`
- `ubo_set_    = device_->allocate_descriptor_set(ubo_layout_);`
- `ubo_buffer_ = device_->create_buffer(buf_desc, init_bytes);`
- `device_->update_descriptor_set(ubo_set_, writes);`
- `pipeline_ = device_->create_pipeline(desc);` (with `desc.set_layouts = { ubo_layout_ }`)

### Frame rendering (`Render2D::draw_frame`)
`draw_frame()` now drives the full frame:
- validates `diagnostics_`, `device_`, `swapchain_`, `pipeline_`
- `device_->acquire_next_image(...)`
- `cmd = device_->begin_commands()`
- record a swapchain pass via:
  - `cmd_begin_swapchain_pass(...)`
  - `cmd_bind_pipeline(...)`
  - `cmd_set_viewport_scissor(...)`
  - `cmd_draw(...)`
  - `cmd_end_swapchain_pass(...)`
- `device_->end_commands(cmd)`
- `device_->submit({ cmd, swapchain, image_index, frame_index })`
- `device_->present(swapchain, image_index)`

In other words: **the renderer owns command recording**, and the backend owns the Vulkan plumbing.

### Resize helper: `draw_frame_and_handle_resize`
This helper implements the current resize policy:
- If framebuffer is zero-area: return `Ok` (minimized window)
- `result = renderer.draw_frame()`
  - `Ok` → done
  - `Error` → fatal render error
  - anything else (`Suboptimal` / `ResizeNeeded`) → resize path

Resize path:
1. `device.wait_idle()` (simple + safe, but stalls)
2. Build `SwapchainDesc` from the framebuffer (**vsync hard-coded true**)
3. `device.resize_swapchain(swapchain, sc_desc)`
4. Ask the existing renderer to recreate its pipeline (swapchain-independent resources persist):
   - `renderer.recreate_pipeline()` rebuilds the pipeline for the new swapchain format while keeping UBO + descriptor resources.
   - If `recreate_pipeline()` fails (non-`Ok`), it is treated as non-fatal: the frame is skipped, but the application keeps running.

---

## RHI contract: `gfx::rhi::IGpuDevice`

Swapchain-related:
- `create_swapchain(desc, wsi) -> SwapchainHandle`
- `resize_swapchain(swapchain, desc) -> FrameResult`
- `acquire_next_image(swapchain, out) -> FrameResult`
- `present(swapchain, image_index) -> FrameResult`

Pipeline-related:
- `create_pipeline(desc) -> PipelineHandle`
- `destroy_pipeline(handle)`

Buffers/textures:
- `create_buffer(desc, initial_data) -> BufferHandle`
- `destroy_buffer(handle)`
- `create_texture(desc) -> TextureHandle`
- `destroy_texture(handle)`

Descriptor sets:
- `create_descriptor_set_layout(desc) -> DescriptorSetLayoutHandle`
- `destroy_descriptor_set_layout(handle)`
- `allocate_descriptor_set(layout) -> DescriptorSetHandle`
- `free_descriptor_set(set)`
- `update_descriptor_set(set, writes) -> FrameResult`
- `cmd_bind_descriptor_set(...)`

Commands/submission:
- `begin_commands() -> CommandBufferHandle`
- `end_commands(cmd)`
- `submit(SubmitDesc{ command_buffer, swapchain, image_index, frame_index })`
- `cmd_begin_swapchain_pass(...)`, `cmd_bind_pipeline(...)`, `cmd_bind_descriptor_set(...)`, `cmd_draw(...)`, ...

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
- `FrameSlot` ring (one per frame-in-flight)
  - `VkCommandBuffer cmd`
  - `image_available` semaphore
  - `in_flight` fence (signaled initially)
- `SwapchainSync render_finished_per_image` (one per swapchain image)
- `images_in_flight_` (per-image fence tracking)
  - `render_finished_per_image` semaphores (one per swapchain image)
- `BasicPipeline basic_pipeline_` (owns `VkPipelineLayout` + `VkPipeline`)
- `pipeline_set_layout_handles_` (stores the `rhi::DescriptorSetLayoutHandle`s needed to rebuild `basic_pipeline_`)
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
6. return `SwapchainHandle{1}` (only one swapchain exists currently)

### Swapchain resize (`resize_swapchain`)
Resize mirrors creation:
- `wait_idle()`
- cleanup + recreate swapchain wrapper + views
- recreate per-image render-finished semaphores
- invalidate `basic_pipeline_` so it will be rebuilt for the new swapchain format

---

## The actual frame: renderer + backend split

`Render2D::draw_frame()` drives the frame while `VkGpuDevice` supplies the Vulkan-backed primitives.

### 0) Ensure pipeline exists
When `Render2D` is constructed (or rebuilt after resize), it calls:
- `device_->create_pipeline(desc)`

The Vulkan backend rebuilds its `basic_pipeline_` when this is called, and also invalidates it on resize.

### 1) Wait previous frame + acquire swapchain image
`VkGpuDevice::acquire_next_image(...)`:
- waits for the **current frame slot** `in_flight` fence
- calls `vkAcquireNextImageKHR(..., frame.image_available, ..., &image_index)`
- waits on `images_in_flight_[image_index]` if that swapchain image is still in flight
- maps `VK_ERROR_OUT_OF_DATE_KHR` → `ResizeNeeded`
- returns `Suboptimal` but allows rendering

### 2) Begin + record commands
- `begin_commands()` resets/begins the **current frame slot** command buffer
- `cmd_begin_swapchain_pass(...)`:
  - transitions swapchain image to `COLOR_ATTACHMENT_OPTIMAL`
  - begins dynamic rendering with clear color
- `cmd_bind_pipeline(...)`
- `cmd_set_viewport_scissor(...)`
- `cmd_draw(..., 3, 1, 0, 0)` (fullscreen triangle)
- `cmd_end_swapchain_pass(...)`:
  - ends dynamic rendering
  - transitions swapchain image to `PRESENT_SRC_KHR`
- `end_commands(cmd)` ends the command buffer

### 3) Submit
`submit({ cmd, swapchain, image_index, frame_index })`:
- waits on the frame slot `image_available`
- signals `render_finished_per_image[image_index]`
- uses the frame slot `in_flight` fence
- advances the frame slot index for the next frame

### 4) Present
`present(swapchain, image_index)`:
- `vkQueuePresentKHR(..., wait = render_finished)`
- map results:
  - `VK_ERROR_OUT_OF_DATE_KHR` → `ResizeNeeded`
  - `VK_SUBOPTIMAL_KHR` → `Suboptimal`
  - otherwise error → `Error`
  - success → `Ok`

---

## Basic pipeline: `vk_pipeline_basic.*`

Strata’s initial pipeline is built by:
- `create_basic_pipeline(VkDevice device, VkFormat color_format, std::span<VkDescriptorSetLayout const> set_layouts = {})`

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

## Synchronization model

### CPU/GPU pacing
- Frames-in-flight ring (currently 2 slots).
- CPU waits on the **next frame slot fence** before recording new work for that slot.
- Swapchain images also track `images_in_flight_` fences so we never re-use an image still being presented.

### GPU-GPU ordering
- `image_available` semaphore ensures rendering waits until the image is available.
- `render_finished[image_index]` semaphore ensures presentation waits until rendering finishes.

### Swapchain image layouts
- Tracked in `swapchain_image_layouts_` to choose correct `oldLayout` in barriers.
- Current model assumes images are either `UNDEFINED` (first use) or `PRESENT_SRC_KHR` (after present).

---

## Current simplifications (intentional)
- **Fixed frames-in-flight count** (currently 2).
- **One graphics pipeline** (fullscreen triangle).
- **No vertex/index buffers** in the frame loop.
- **No depth**, no MSAA, no post-processing.
- **Renderer records commands explicitly**, but only for a single basic pass.
- Swapchain recreation is done with `wait_idle()` (safe, not optimal).

---

## Planned evolution points

1. **Descriptor sets / resource binding**
   - (v1) descriptor set layout + allocate/update APIs exist in the RHI (uniform buffers only)
   - Vulkan backend owns a descriptor pool and allocates descriptor sets from it
   - Next: expand descriptor types (images/samplers, etc.) and grow renderer usage beyond the current UBO

2. **Command recording API ergonomics**
   - Replace `cmd_*` functions with a command encoder/list object.
   - Support secondary command buffers or parallel recording.

3. **Pipeline lifetime rules**
   - Now: pipeline is recreated on resize via `renderer.recreate_pipeline()` (swapchain-independent UBO + descriptor resources persist), and the backend can lazily rebuild.
   - Later: pipeline cache, expanded descriptor sets, shader hot-reload, etc.

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
| Present | `IGpuDevice::present()` | queue present (waits on render-finished) |