# Strata Naming Conventions

This document captures the **current naming patterns** in Strata and codifies them into a set of conventions to keep the codebase consistent, readable, and low-friction as it grows.

Strata aims to be:
- **Learning-focused but production-leaning**
- **C++23, safe-by-default**
- **Clear boundaries** between platform, RHI, renderer, and backend implementations

> This is a living document. If a rule conflicts with readability or correctness, prefer readability/correctness and update the rule afterwards.

---

## Principles

1. **Consistency beats cleverness**
   - Choose one style and apply it everywhere.
2. **Names reflect ownership and layer**
   - Backend-specific details stay in backend namespaces and folders.
3. **Avoid leaking heavy dependencies**
   - Public headers should not pull in OS headers or `<vulkan/vulkan.h>` unless the header is explicitly backend-only.
4. **Prefer explicitness**
   - Avoid ambiguous abbreviations unless they are well-established (e.g., WSI, RHI, GPU).

---

## Namespaces and module boundaries

### Root namespace
All engine code lives under:

- `strata::...`

### Layered namespaces
Use namespaces to communicate architecture and dependency boundaries:

- `strata::platform` — windowing, input, OS-specific implementations
- `strata::core` — application wrapper / main loop orchestration
- `strata::gfx` — graphics layer root
  - `strata::gfx::rhi` — backend-agnostic API (interfaces + typed handles + descriptors)
  - `strata::gfx::renderer` — higher-level rendering built on the RHI
  - `strata::gfx::vk` — Vulkan backend implementation (backend-only)

### Platform-specific sub-namespace
WSI handle alternatives live in:

- `strata::platform::wsi` (Win32/X11/Wayland variants)

### Rule of thumb: "where does this code belong?"
- If it contains `Vk*` types, it belongs in **`strata::gfx::vk`** and `engine/gfx/backend/vk/*`.
- If it contains OS types (`HWND`, `Display*`, `wl_display*`), it belongs in **platform source files** (pImpl) and should not leak into public headers.
- If it must be visible to renderer code, it belongs in **`gfx/rhi`** or **`gfx/renderer`** (with Vulkan hidden).

---

## File and folder naming

### Folder names
- Use **lowercase** folder names.
- Prefer **short, meaningful** names that match the architecture:
  - `engine/core`
  - `engine/platform`
  - `engine/gfx/rhi`
  - `engine/gfx/renderer`
  - `engine/gfx/backend/vk`

### File names
- Use **lower_snake_case** for filenames:
  - `application.cpp`, `gpu_device.h`, `vk_swapchain.cpp`, `render_2d.cpp`
- Platform/backend specializations use suffixes:
  - `window_win32.cpp`, `window_x11.cpp`
  - `vk_wsi_bridge_win32.cpp`, `vk_wsi_bridge_x11.cpp`
- “Concept files” (not a single type) still use lower_snake_case:
  - `gpu_types.h`, `vk_wsi_bridge.h`

### Public headers vs private sources
Public headers are under an `include/strata/...` tree:
- `engine/core/include/strata/core/application.h`
- `engine/gfx/include/strata/gfx/rhi/gpu_device.h`
- `engine/platform/include/strata/platform/wsi_handle.h`

Private/internal implementation lives in `src/` or backend folders:
- `engine/core/src/application.cpp`
- `engine/gfx/backend/vk/*.cpp`
- `engine/platform/src/win32/*.cpp`

### Header hygiene
- Use `#pragma once`.
- Avoid including OS/Vulkan headers in public headers.
- Prefer forward declarations of Vulkan handles and platform handles when possible.
- Put heavy includes in `.cpp` or backend-only headers.

---

## Type naming

### Classes and structs
- Use **PascalCase** (UpperCamelCase):
  - `Application`, `Render2D`, `VkGpuDevice`, `VkSwapchainWrapper`
  - `FrameContext`, `ApplicationConfig`, `SwapchainDesc`

### Interfaces
- Interfaces use an `I` prefix:
  - `IGpuDevice`

### Backend types
- Vulkan backend types use a `Vk` prefix and live in `strata::gfx::vk`:
  - `VkGpuDevice`, `VkInstanceWrapper`, `VkDeviceWrapper`, `VkSwapchainWrapper`, `VkCommandBufferPool`

### "Wrapper" naming
Use `*Wrapper` for RAII wrappers that own raw Vulkan objects:
- `VkInstanceWrapper` owns `VkInstance`, `VkSurfaceKHR`, optional debug messenger
- `VkDeviceWrapper` owns `VkDevice`
- `VkSwapchainWrapper` owns `VkSwapchainKHR` + image views

> If a type is not RAII-owning but “adapts” behavior, avoid the `Wrapper` suffix.

### Common suffixes (semantic)
Use suffixes to communicate the role of a type:

- `*Desc` — descriptor/config used to create a resource
  - `SwapchainDesc`, `PipelineDesc`, `BufferDesc`, `TextureDesc`
- `*Config` — higher-level configuration input
  - `ApplicationConfig`
- `*Context` — per-frame or per-operation context
  - `FrameContext`
- `*Handle` — typed opaque handle / ID
  - `SwapchainHandle`, `PipelineHandle`, `BufferHandle`, etc.
- `*Error` — error enums
  - `ApplicationError`
- `*CreateInfo` — backend selection or creation policy structs
  - `DeviceCreateInfo`

### Platform WSI types
Use platform name + concept:
- `Win32Instance`, `Win32Window`, `Win32`
- `X11Display`, `X11Window`, `X11`
- `WaylandDisplay`, `WaylandSurface`, `Wayland`

---

## Function and method naming

Strata currently uses **lower_snake_case** for both free functions and methods.

Examples:
- `Application::create(...)`
- `Application::request_exit()`
- `IGpuDevice::create_swapchain(...)`
- `Window::should_close()`
- `create_basic_pipeline(...)`
- `draw_frame_and_handle_resize(...)`

### Guidance
- **Factories** should follow `create_*`:
  - `create_device`, `create_swapchain`, `create_pipeline`, `create_basic_pipeline`
- **Destructors** should be `destroy_*` in APIs that operate on handles:
  - `destroy_pipeline(PipelineHandle)`
- **Predicates/queries** should read like questions:
  - `should_close()`, `is_visible()`, `valid()`
- **Mutating actions** should be verbs:
  - `request_close()`, `poll_events()`, `wait_idle()`, `resize_swapchain()`

---

## Member, local, and parameter naming

### Member variables

#### Private / internal data members
- Use **lower_snake_case** with a **trailing underscore**:
  - `device_`, `swapchain_`, `pipeline_`, `exit_requested_`, `frame_index_`, `last_frame_`
  - (backend) `instance_`, `device_`, `swapchain_`, `command_pool_`, `primary_cmd_`
- Applies to:
  - `private:` and `protected:` data members
  - pImpl `Impl` structs/classes that are not part of the public API

#### Public data members (struct fields)
- Use **lower_snake_case** **without** a trailing underscore:
  - `width`, `height`, `format`, `image_count`, `present_mode`
- Applies to:
  - `public:` data members, especially in “data carrier” structs like `*Desc`, `*Config`, `*CreateInfo`, `*Context`, and handle/ID structs.

> Rationale: public fields read like plain data (POD-style), while private fields visually signal encapsulation/ownership.

Example:
```cpp
struct SwapchainDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    Format format{Format::B8G8R8A8Unorm};
    bool vsync{true};
};

class VkSwapchainWrapper {
public:
    SwapchainDesc desc() const noexcept;

private:
    VkDevice device_{};
    VkSwapchainKHR swapchain_{};
    SwapchainDesc desc_{};
};
```

### Locals and parameters
- Use **lower_snake_case**:
  - `framebuffer_size`, `image_index`, `render_finished`

### Boolean naming
- Use descriptive predicate-like names:
  - `exit_requested`, `closing`, `minimized`, `visible`, `throttle_cpu`
- Avoid negative booleans like `no_validation`—prefer `enable_validation`.

---

## Constants and macros

### Constants
- Use `k` prefix + PascalCase:
  - `kEnableValidation`, `kValidationLayers`, `kFenceTimeout`, `kDeviceExtensions`, `kInvalidIndex`, `kExtViews`
- Prefer `constexpr`, `inline constexpr`, or `static constexpr` depending on scope.

### Macros
- Macros should be **rare** and use `STRATA_*` uppercase naming:
  - `STRATA_VK_VALIDATION`
- Prefer build-system configuration + `constexpr` where possible.
- Avoid macro “function-like” utilities in favor of inline functions.

---

## Enums and flags

### Enum types
- Use `enum class` with **PascalCase** names:
  - `ApplicationError`, `FrameResult`, `BackendType`, `Format`

### Enum values
- Use **PascalCase** enumerators:
  - `WindowCreateFailed`, `ResizeNeeded`, `Suboptimal`, `Error`, `Ok`

### Bitflags
- Use strongly typed enums with explicit underlying types:
  - `enum class BufferUsage : std::uint32_t`
- Provide operators like `operator|` where needed.
- Prefer explicit naming for bits:
  - `Vertex`, `Index`, `Uniform`, `Upload`

---

## Handles

### Typed handle pattern
Resource handles are “thin typed IDs”:

```cpp
struct PipelineHandle {
    std::uint32_t value{0};
    explicit constexpr operator bool() const noexcept { return value != 0; }
};
```

Guidelines:
- `0` means **invalid / null handle**
- Provide `explicit operator bool()` for easy validity checks
- Keep them trivially copyable and small (value types)

---

## Vulkan-specific naming rules

### Vulkan object naming
- Raw Vulkan types: `Vk*` (from Vulkan)
- Strata’s Vulkan classes: `Vk*` prefix and namespace `strata::gfx::vk`

Examples:
- `VkSwapchainWrapper` wraps `VkSwapchainKHR`
- `BasicPipeline` wraps `VkPipelineLayout` and `VkPipeline`

### WSI bridge naming
Files that bridge platform handles to Vulkan WSI should follow:
- `vk_wsi_bridge.h`
- `vk_wsi_bridge_<platform>.cpp`

Functions:
- `required_instance_extensions(wsi)`
- `create_surface(instance, wsi)`

---

## Comments and section headers

Strata source files commonly use:
- A header comment block with `Purpose:` (and sometimes `Design Notes:`)
- Section dividers using `// --- ... ---`

Example pattern:
```cpp
// -----------------------------------------------------------------------------
// Purpose:
//   ...
// -----------------------------------------------------------------------------
```

Guidelines:
- Prefer **why** over **what** in comments (code already shows what).
- Document invariants and ordering requirements (especially lifetimes and synchronization).
- Keep comments updated when behavior changes.

---

## Naming "smells" to avoid

- Abbreviations that aren’t standard (prefer `framebuffer_size` over `fb_sz`)
- Overloaded names across layers (e.g., a platform `Device` vs GPU `Device` without qualification)
- "Manager" or "Util" without a more specific name
- Names that don’t match ownership (e.g., something named `*Handle` that actually owns resources)

---

## Appendix: Quick reference

### Conventions table

| Concept | Convention | Examples |
|---|---|---|
| Namespace | `strata::<layer>` | `strata::core`, `strata::gfx::rhi`, `strata::gfx::vk` |
| Class/struct | PascalCase | `Application`, `Render2D`, `VkGpuDevice` |
| Interface | `I` + PascalCase | `IGpuDevice` |
| Methods/functions | lower_snake_case | `create_swapchain`, `wait_idle`, `draw_frame` |
| Private/protected members | lower_snake_case + `_` | `device_`, `swapchain_`, `pipeline_` |
| Public struct fields | lower_snake_case | `width`, `image_count`, `present_mode` |
| Files | lower_snake_case | `vk_swapchain.cpp`, `render_2d.h` |
| Platform file suffix | `_win32`, `_x11`, `_wayland` | `window_win32.cpp` |
| Constant | `k` + PascalCase | `kFenceTimeout`, `kDeviceExtensions` |
| Macro | `STRATA_*` | `STRATA_VK_VALIDATION` |

---

## Open questions (optional future refinements)

These are not required today, but could be clarified later:
- Do we want a firm rule for value types vs heap allocations (beyond “prefer explicit ownership”)?
- Should `Render2D` eventually own command recording responsibilities (changing naming around “present”)?
- Should backend classes use a stricter suffix scheme (`*Backend`, `*Impl`) or keep `Vk*` only?
