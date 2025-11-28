#pragma once

#include <cstdint>
#include <span>

namespace strata::gfx::rhi {

struct Extent2D {
    std::uint32_t width{};
    std::uint32_t height{};
};

enum class Format {
    Unknown,
    R8G8B8A8_UNorm,
    B8G8R8A8_UNorm,
    D24_UNorm_S8_UInt,
    D32_SFloat,
};

enum class BufferUsage : std::uint32_t {
    None    = 0,
    Vertex  = 1 << 0,
    Index   = 1 << 1,
    Uniform = 1 << 2,
    Upload  = 1 << 3,
};
inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

enum class TextureUsage : std::uint32_t {
    None            = 0,
    Sampled         = 1 << 0,
    ColorAttachment = 1 << 1,
    DepthStencil    = 1 << 2,
};

struct BufferDesc {
    std::uint64_t size_bytes{};
    BufferUsage   usage{ BufferUsage::None };
    bool          host_visible{ false };
};

struct TextureDesc {
    Extent2D     size{};
    Format       format{ Format::Unknown };
    TextureUsage usage{ TextureUsage::None };
    std::uint32_t mip_levels{ 1 };
};

struct PipelineDesc {
    const char* vertex_shader_path{};
    const char* fragment_shader_path{};
    bool        alpha_blend{ false };
};

enum class FrameResult {
    Ok,
    Suboptimal,
    ResizeNeeded,
    Error,
};

struct BufferHandle {
    std::uint32_t value{ 0 };
    explicit constexpr operator bool() const noexcept { return value != 0; }
};
struct TextureHandle {
    std::uint32_t value{ 0 };
    explicit constexpr operator bool() const noexcept { return value != 0; }
};
struct PipelineHandle {
    std::uint32_t value{ 0 };
    explicit constexpr operator bool() const noexcept { return value != 0; }
};
struct CommandBufferHandle {
    std::uint32_t value{ 0 };
    explicit constexpr operator bool() const noexcept { return value != 0; }
};

struct SwapchainDesc {
    Extent2D size{};
    Format   format{ Format::B8G8R8A8_UNorm };
    bool     vsync{ true };
};

struct SwapchainHandle {
    std::uint32_t value{ 0 };
    explicit constexpr operator bool() const noexcept { return value != 0; }
};

} // namespace strata::gfx::rhi
