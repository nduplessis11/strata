// -----------------------------------------------------------------------------
// engine/gfx/include/strata/gfx/rhi/gpu_types.h
//
// Purpose:
//   Define common RHI types, handles, and resource descriptors.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <span>

namespace strata::gfx::rhi
{

struct Extent2D
{
    std::uint32_t width{};
    std::uint32_t height{};
};

enum class Format
{
    Unknown,
    R8G8B8A8_UNorm,
    B8G8R8A8_UNorm,
    D24_UNorm_S8_UInt,
    D32_SFloat,
};

enum class BufferUsage : std::uint32_t
{
    NoFlags = 0,
    Vertex  = 1 << 0,
    Index   = 1 << 1,
    Uniform = 1 << 2,
    Upload  = 1 << 3,
};
inline BufferUsage operator|(BufferUsage a, BufferUsage b)
{
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

enum class TextureUsage : std::uint32_t
{
    NoFlags         = 0,
    Sampled         = 1 << 0,
    ColorAttachment = 1 << 1,
    DepthStencil    = 1 << 2,
};
inline TextureUsage operator|(TextureUsage a, TextureUsage b)
{
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

struct BufferDesc
{
    std::uint64_t size_bytes{};
    BufferUsage   usage{BufferUsage::NoFlags};
    bool          host_visible{false}; // true -> mapped/UPLOAD heap
};

struct TextureDesc
{
    Extent2D      size{};
    Format        format{Format::Unknown};
    TextureUsage  usage{TextureUsage::NoFlags};
    std::uint32_t mip_levels{1};
};

// --- Vertex input ------------------------------------------------------------
// Minimal explicit vertex input description. Backends map this to API-specific
// vertex binding/attribute descriptions.

enum class VertexInputRate : std::uint8_t
{
    Vertex,
    Instance,
};

enum class VertexFormat : std::uint8_t
{
    Float3, // vec3
    Float4, // vec4
};

struct VertexBindingDesc
{
    std::uint32_t   binding{0};
    std::uint32_t   stride{0};
    VertexInputRate rate{VertexInputRate::Vertex};
};

struct VertexAttributeDesc
{
    std::uint32_t location{0};
    std::uint32_t binding{0};
    VertexFormat  format{VertexFormat::Float3};
    std::uint32_t offset{0};
};

enum class IndexType : std::uint8_t
{
    UInt16,
    UInt32,
};

struct DescriptorSetLayoutHandle; // forward declare

struct PipelineDesc
{
    // We can evolve this later with real shader reflection data, etc.
    char const* vertex_shader_path{};
    char const* fragment_shader_path{};
    bool        alpha_blend{false};

    // Optional depth state (for dynamic rendering).
    // If depth_format == Format::Unknown, backends should treat this pipeline as
    // "no depth attachment"
    Format depth_format{Format::Unknown};
    bool   depth_test{false};
    bool   depth_write{false};

    // Optional vertex input. If empty, pipeline is created with no vertex input.
    std::span<VertexBindingDesc const>   vertex_bindings{};
    std::span<VertexAttributeDesc const> vertex_attributes{};

    std::span<DescriptorSetLayoutHandle const> set_layouts{};
};

enum class FrameResult
{
    Ok,
    Suboptimal,   // e.g., swapchain still works but wants resize
    ResizeNeeded, // e.g., OUT_OF_DATE
    Error,
};

// Thin typed handles instead of raw integers:
struct BufferHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};
struct TextureHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};
struct PipelineHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};
struct CommandBufferHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};

struct SwapchainDesc
{
    Extent2D size{};
    Format   format{Format::B8G8R8A8_UNorm};
    bool     vsync{true};
};

struct SwapchainHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};

struct ClearColor
{
    float r{0.f}, g{0.f}, b{0.f}, a{1.f};
};

struct AcquiredImage
{
    std::uint32_t image_index{0};
    Extent2D      extent{};
    std::uint32_t frame_index{0};
};

enum class ShaderStage : std::uint32_t
{
    NoFlags  = 0,
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b)
{
    return static_cast<ShaderStage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline ShaderStage operator&(ShaderStage a, ShaderStage b)
{
    return static_cast<ShaderStage>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

enum class DescriptorType
{
    UniformBuffer,
    // Future:
    // CombinedImageSampler,
    // StorageBuffer,
};

struct DescriptorBinding
{
    std::uint32_t  binding{0};
    DescriptorType type{DescriptorType::UniformBuffer};
    std::uint32_t  count{1};
    ShaderStage    stages{ShaderStage::NoFlags};
};

struct DescriptorSetLayoutDesc
{
    std::span<DescriptorBinding const> bindings{};
};

struct DescriptorSetLayoutHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};

struct DescriptorSetHandle
{
    std::uint32_t      value{0};
    explicit constexpr operator bool() const noexcept
    {
        return value != 0;
    }
};

// Minimal update model (uniform buffers only, for now)
struct DescriptorBufferInfo
{
    BufferHandle  buffer{};
    std::uint64_t offset_bytes{0};
    std::uint64_t range_bytes{0}; // 0 = "whole buffer" (backend can expand)
};

struct DescriptorWrite
{
    std::uint32_t        binding{0};
    DescriptorType       type{DescriptorType::UniformBuffer};
    DescriptorBufferInfo buffer{};
};

} // namespace strata::gfx::rhi
