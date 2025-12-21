# Strata

Strata is a **learning-focused, production-leaning Vulkan engine** written in **C++23**.

It’s built around a simple, explicit layering model:

> **platform** (OS/window/WSI) → **core** (app orchestration) → **gfx** (RHI + renderer + Vulkan backend)

Strata is intentionally small and pragmatic right now: the current renderer clears the swapchain and draws a **fullscreen triangle** via **Vulkan 1.3 dynamic rendering**.

---

## Documentation

Start here:

- **Architecture:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- **Rendering flow (frame pipeline):** [`docs/RENDERING_PIPELINE.md`](docs/RENDERING_PIPELINE.md)
- **Ownership / lifetime rules:** [`docs/LIFETIME_MODEL.md`](docs/LIFETIME_MODEL.md)
- **Naming & style conventions:** [`docs/NAMING_CONVENTIONS.md`](docs/NAMING_CONVENTIONS.md)

Diagrams:

- **Class / ownership diagram:** [`docs/diagrams/strata_core.puml`](docs/diagrams/strata_core.puml)

---

## Current status (what it does today)

- A minimal “smoke test” renderer:
  - **Acquire → record → submit → present**
  - **One command buffer** reused every frame
  - **One frame in flight** (single fence)
  - **One graphics pipeline** (fullscreen triangle)
- Resize handling:
  - swapchain resize triggers a **`wait_idle()`** + swapchain recreation
  - pipeline is recreated after resize
- Vulkan backend requirements:
  - Vulkan 1.3 **dynamic rendering**
  - Vulkan 1.3 **synchronization2**

If you’re looking for a “read the code top-to-bottom” entry point, follow the call graph:

1. `strata::core::Application::create(...)`
2. `strata::core::Application::run(...)`
3. `strata::gfx::renderer::Render2D::draw_frame()`
4. `strata::gfx::rhi::IGpuDevice::present(...)`
5. `strata::gfx::vk::VkGpuDevice::present(...)`

---

## Repo layout (high level)

```text
engine/platform      OS + windowing + event polling + WSI handle production
engine/core          Application wrapper, main loop, runtime helpers
engine/gfx           RHI + renderer + backend/vk + shaders
games/arcade_shooter  Example game / executable
docs/                Architecture + pipeline + lifetime + conventions
cmake/               CMake helper modules (warnings, etc.)
```

---

## Building

### Prerequisites

- **CMake 3.27+** (the repo uses `cmake_minimum_required(VERSION 3.27)`)
- A C++ toolchain with **C++23** support
- **Vulkan 1.3** development files (headers + loader library)
  - On Windows, this is typically the Vulkan SDK.
  - On Linux, this is typically your distro’s Vulkan dev packages.
- Platform dependencies:
  - **Windows**: Win32 (no extra packages; uses system libs like `user32`, `gdi32`, `winmm`)
  - **Linux/X11** (default): Xlib dev packages (and `Threads`, `dl`)
  - **Linux/Wayland** (optional): `wayland-client` dev packages via `pkg-config`

> Tip: the project enables `CMAKE_EXPORT_COMPILE_COMMANDS ON`, so you’ll get a `compile_commands.json` in your build directory (nice for clangd).

### Build with CMake Presets (recommended)

This repo ships with `CMakePresets.json` and expects the **Ninja** generator.

#### Debug (warnings-as-errors on)
```bash
cmake --preset ninja-debug
cmake --build --preset debug
```

#### RelWithDebInfo
```bash
cmake --preset ninja-rel
cmake --build --preset rel
```

#### Release
```bash
cmake --preset ninja-release
cmake --build --preset release
```

### Running

The example executable target is:

- `strata_shooter` (from `games/arcade_shooter`)

After building, you can usually run it from the build tree, e.g.:

```bash
# Linux/macOS-like shells
./build/debug/games/arcade_shooter/strata_shooter
```

On Windows (PowerShell), it will typically be:

```powershell
.\build\debug\games\arcade_shooter\strata_shooter.exe
```

#### Shader assets location
The Vulkan backend loads SPIR-V binaries from paths like:

- `shaders/fullscreen_triangle.vert.spv`
- `shaders/flat_color.frag.spv`

The `strata_shooter` target has a **post-build step** that copies the built shader outputs to:

- `<exe_dir>/shaders/`

So the safest workflow is: **run the executable from its output directory** (or ensure the `shaders/` folder is next to it).

---

## Useful CMake options

Top-level options:

- `STRATA_WARNINGS_AS_ERRORS` (default: `OFF`)
  - When `ON`, warnings are treated as errors (via `cmake/warnings.cmake`).
- `STRATA_USE_X11` (Linux only; default: `ON`)
  - When `ON`, builds against X11.
  - When `OFF`, uses the Wayland dependency path (Wayland window backend may be WIP depending on your repo state).
- `STRATA_ENABLE_ASAN` (default: `OFF`)
  - Enables AddressSanitizer on supported compilers (Clang/GCC).

Example (manual configure without presets):

```bash
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSTRATA_WARNINGS_AS_ERRORS=ON \
  -DSTRATA_USE_X11=ON
cmake --build build/debug
```

---

## Targets (how the build is structured)

At a high level, the codebase is split into layered targets:

- `engine_options` (INTERFACE): shared compile options/definitions
- `strata_platform` (STATIC): windowing/event loop + `WsiHandle` production
- `strata_gfx_rhi` (INTERFACE): header-only RHI surface (interfaces + typed handles)
- `strata_gfx_renderer` (STATIC): renderer layer built on the RHI
- `strata_core` (STATIC): `Application` wrapper and orchestration
- `strata_shooter` (EXECUTABLE): example “game” that links `strata_core`

---

## Contributing / style

If you’re adding code, please follow:

- [`docs/NAMING_CONVENTIONS.md`](docs/NAMING_CONVENTIONS.md)

Also keep Vulkan containment intact:
- **No raw `Vk*` types outside** `engine/gfx/backend/vk/*` (`namespace strata::gfx::vk`).

---

## Roadmap (high level)

Some natural next steps (also described in the docs):

- Move command recording out of `present()` into the renderer (or a frame-graph)
- Add **multiple frames-in-flight** (per-frame fences/semaphores/command buffers)
- Introduce real resource tables/registries for buffers/textures/pipelines
- Grow beyond the single-pass fullscreen triangle (depth, MSAA, post, etc.)
- ECS/data-driven systems (future)
