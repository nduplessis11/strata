// engine/gfx/include/strata/gfx/vulkan/vulkan_context.h
#pragma once

#include "strata/platform/wsi_handle.h"
#include "strata/gfx/vulkan/wsi_bridge.h"

#include <vector>

struct VkInstance_T;
using VkInstance = VkInstance_T*;

namespace strata::gfx {
	struct VulkanContextDesc {
		bool enable_validation{ false };
	};

	// VulkanContext owns a VkInstance with a small RAII handle type.
	// VulkanContext itself follows the Rule of Zero: it declares no destructor
	// or special member functions; it just aggregates RAII members.
	class VulkanContext {
	public:
		VulkanContext() = default;
		
		// Factory: creates an instance using the active WSI
		static VulkanContext create(const strata::platform::WsiHandle& wsi,
									const VulkanContextDesc& desc = {});

		[[nodiscard]] VkInstance instance() const noexcept { return instance_.get(); }
		[[nodiscard]] bool valid() const noexcept { return static_cast<bool>(instance_); }

	private:
		// Small RAII type that owns a VkInstance.
		// All the "destroy" logic lives here; VulkanContext just holds one.
		struct InstanceHandle {
			VkInstance handle{ nullptr };

			InstanceHandle() = default;
			explicit InstanceHandle(VkInstance h) : handle(h) {}

			~InstanceHandle();
			InstanceHandle(const InstanceHandle&) = delete;
			InstanceHandle& operator=(const InstanceHandle&) = delete;

			InstanceHandle(InstanceHandle&& other) noexcept;
			InstanceHandle& operator=(InstanceHandle&& other) noexcept;

			[[nodiscard]] VkInstance get() const noexcept { return handle; }
			explicit operator bool() const noexcept { return handle != nullptr; }
		};

		InstanceHandle instance_{};
	};
}