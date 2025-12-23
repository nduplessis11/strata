# Strata Coding Conventions

This document captures the **current coding patterns** in Strata and codifies them into a set of conventions to keep the codebase consistent, readable, and low-friction as it grows.

Strata aims to be:
- **Learning-focused but production-leaning**
- **C++23, safe-by-default**
- **Clear boundaries** between platform, RHI, renderer, and backend implementations**

> This is a living document. If a rule conflicts with readability or correctness, prefer readability/correctness and update the rule afterwards.

---

## Principles

1. **Consistency beats cleverness**
   - Choose one style and apply it everywhere.

2. **Names reflect meaning, not storage**
   - Don't encode storage or lifetime in names (`kPrefix`, `g_`, `m_`, `_s`, etc.).
   - The language already encodes `static`, `constexpr`, `inline`, `const`, and access (`public` / `private`).

3. **Ownership is signaled by `_`**
   - A **trailing underscore** (`lower_snake_case_`) is reserved for **private/protected instance data members only**.
   - Public data never uses a trailing underscore.

4. **Prefer explicitness**
   - Avoid ambiguous abbreviations unless they are well-established (e.g., WSI, RHI, GPU).

5. **Avoid leaking heavy dependencies**
   - Public headers should not pull in OS headers or `<vulkan/vulkan.h>` unless the header is explicitly backend-only.

6. **Use east const**
   - Prefer `Type const&` / `Type const*` over `const Type&` / `const Type*`.

---

## Namespaces and module boundaries

### Root namespace
All engine code lives under:

- `strata::...`

### Namespace casing
- Use **lower_snake_case** for namespaces:
  - `strata::core`
  - `strata::platform`
  - `strata::gfx::renderer`

### Layered namespaces
Namespaces communicate architecture and dependency boundaries:

- `strata::platform` -- windowing, input, OS-specific implementations
- `strata::core` -- application wrapper / main loop orchestration
- `strata::gfx` -- graphics layer root
  - `strata::gfx::rhi` -- backend-agnostic API (interfaces + handles + descriptors)
  - `strata::gfx::renderer` -- higher-level rendering built on the RHI
  - `strata::gfx::vk` -- Vulkan backend implementation (backend-only)

### Platform-specific sub-namespaces
WSI handle variants live in:

- `strata::platform::wsi` (Win32 / X11 / Wayland)

### Rule of thumb: where does this code belong?
- `Vk*` types → `strata::gfx::vk`
- OS types (`HWND`, `Display*`, `wl_display*`) → platform `.cpp` (pImpl)
- Renderer-visible abstractions → `gfx::rhi` or `gfx::renderer`

---

## File and folder naming

### Folder names
- Lowercase only
- Reflect architecture:
  - `engine/core`
  - `engine/platform`
  - `engine/gfx/rhi`
  - `engine/gfx/renderer`
  - `engine/gfx/backend/vk`

### File names


### Splitting large implementations

If a class implementation grows large, it is OK to split its method definitions across
multiple `.cpp` files, keeping a single public header.

Naming: `<base>_<area>.cpp`
Example: `vk_gpu_device_swapchain.cpp`, `vk_gpu_device_recording.cpp`, ...
- **lower_snake_case**
  - `application.cpp`, `gpu_device.h`, `render_2d.cpp`
- Platform/backend suffixes:
  - `window_win32.cpp`, `window_x11.cpp`
  - `vk_wsi_bridge_win32.cpp`

### Public headers vs private sources
Public headers:
- `engine/*/include/strata/...`

Private implementation:
- `engine/*/src/...`
- `engine/gfx/backend/vk/*.cpp`
- `engine/gfx/backend/vk/vk_gpu_device/*.cpp`

### Header hygiene
- Use `#pragma once`
- No OS/Vulkan headers in public headers
- Prefer forward declarations
- Heavy includes belong in `.cpp` or backend-only headers

---

## Type naming

### Classes and structs
- **PascalCase**
  - `Application`, `Render2D`, `SwapchainDesc`, `FrameContext`

### Interfaces
- Interfaces use an `I` prefix + PascalCase:
  - `IGpuDevice`, `IWindowSystem`

> This is an intentional Strata exception for instant recognizability.

### Backend types
- Vulkan backend types use `Vk` prefix and live in `strata::gfx::vk`

### Wrapper naming
- Use `*Wrapper` only for RAII-owning Vulkan wrappers
- Do not use for adapters or views

### Common semantic suffixes
- `*Desc` -- resource descriptors
- `*Config` -- higher-level configuration
- `*Context` -- per-frame or per-operation context
- `*Handle` -- typed opaque IDs
- `*Error` -- error enums
- `*CreateInfo` -- creation policies

---

## Function and method naming

- Use **lower_snake_case** everywhere

Examples:
- `create_device`
- `request_exit`
- `draw_frame_and_handle_resize`

### Guidance
- **Factories**: `create_*`
- **Destruction APIs (handle-based)**: `destroy_*`
- **Predicates**: `is_*`, `has_*`, `should_*`
- **Actions**: verbs (`poll_events`, `wait_idle`, `resize_swapchain`)

---

## Member, local, and parameter naming

### Private / protected instance members
- `lower_snake_case_`

### Static data members
- `lower_snake_case`
- No suffixes

### Public struct members
- `lower_snake_case`
- No trailing underscore

### Locals and parameters
- `lower_snake_case`

### Boolean naming
- Prefer positive, predicate-style names
- Avoid negatives like `no_validation`

---

## Constants and macros

### Constants
- **lower_snake_case**
- Prefer:
  - `constexpr`
  - `inline constexpr`
  - `constinit`

Examples:

```cpp
inline constexpr std::uint64_t fence_timeout_ns = 1'000'000'000;
```

### Macros
- Rare
- `STRATA_*`
- Prefer constexpr or build-system config instead

---

## Enums and flags

### Enum types
- `enum class`
- **PascalCase**

### Enum values
- **PascalCase**

Example:

```cpp
enum class FrameResult {
    Ok,
    Suboptimal,
    ResizeNeeded,
    Error,
};
```

### Bitflags
- Strongly typed enums
- Explicit underlying types
- Provide operators if needed

---

## Handles

Typed handle pattern:

```cpp
struct PipelineHandle {
    std::uint32_t value{0};
    explicit constexpr operator bool() const noexcept { return value != 0; }
};
```

Rules:
- `0` = invalid
- Trivially copyable
- No ownership

---

## Vulkan-specific naming rules

### Vulkan object naming
- Raw Vulkan: `Vk*`
- Strata Vulkan types: `Vk*` in `strata::gfx::vk`

### WSI bridge naming
Files:
- `vk_wsi_bridge.h`
- `vk_wsi_bridge_<platform>.cpp`

Functions:
- `required_instance_extensions(wsi)`
- `create_surface(instance, wsi)`

---

## Comments and section headers

Common file layout:

```cpp
// -----------------------------------------------------------------------------
// Purpose:
//   ...
// -----------------------------------------------------------------------------
```

Guidelines:
- Explain **why**, not what
- Document lifetimes and ordering
- Keep comments accurate

---

## Naming smells to avoid

- Hungarian notation
- Storage-encoded names
- `Manager` / `Util`
- Public members with `_`
- `*Handle` types that own resources

---

## Appendix: Quick reference

| Concept | Convention | Example |
|-------|-----------|--------|
| Namespace | lower_snake_case | `strata::gfx::rhi` |
| Class / struct | PascalCase | `Render2D` |
| Interface | `I` + PascalCase | `IGpuDevice` |
| Function | lower_snake_case | `create_swapchain` |
| Private member | lower_snake_case_ | `device_` |
| Static member | lower_snake_case | `next_id` |
| Public struct field | lower_snake_case | `width` |
| Constant | lower_snake_case | `fence_timeout_ns` |
| Macro | `STRATA_*` | `STRATA_ASSERT` |

---

## Open questions (future)

- Do we want stricter rules for value vs owning types?
- Should backends adopt `*Impl` in addition to `Vk*`?