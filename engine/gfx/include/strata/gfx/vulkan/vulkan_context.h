// engine/gfx/include/strata/gfx/vulkan/vulkan_context.h
#pragma once

#include "strata/platform/wsi_handle.h"
#include "strata/gfx/vulkan/wsi_bridge.h"

#include <vector>

struct VkInstance_T;
struct VkSurfaceKHR_T;
struct VkDevice_T;
using VkInstance = VkInstance_T*;
using VkSurfaceKHR = VkSurfaceKHR_T*;
using VkDevice = VkDevice_T*;

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
		[[nodiscard]] bool valid() const noexcept { return instance_.valid(); }

		[[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_.get(); }
		[[nodiscard]] bool has_surface() const noexcept { return surface_.valid(); }

	private:
		// Small RAII type that owns a VkInstance.
		// All the "destroy" logic lives here; VulkanContext just holds one.
		struct InstanceHandle {
			InstanceHandle() = default;
			explicit InstanceHandle(VkInstance h) : handle(h) {}

			~InstanceHandle();
			InstanceHandle(const InstanceHandle&) = delete;
			InstanceHandle& operator=(const InstanceHandle&) = delete;

			InstanceHandle(InstanceHandle&& other) noexcept;
			InstanceHandle& operator=(InstanceHandle&& other) noexcept;

			[[nodiscard]] VkInstance get() const noexcept { return handle; }
			[[nodiscard]] bool valid() const noexcept { return handle != nullptr; }

		private:
			VkInstance handle{ nullptr };
		};

		struct SurfaceHandle {
			SurfaceHandle() = default;
			SurfaceHandle(VkInstance inst, VkSurfaceKHR h) : instance(inst), handle(h) {}

			~SurfaceHandle();

			SurfaceHandle(const SurfaceHandle&) = delete;
			SurfaceHandle& operator=(const SurfaceHandle&) = delete;

			SurfaceHandle(SurfaceHandle&& other) noexcept;
			SurfaceHandle& operator=(SurfaceHandle&& other) noexcept;

			[[nodiscard]] VkSurfaceKHR get() const noexcept { return handle; }
			[[nodiscard]] bool valid() const noexcept { return handle != nullptr; }

		private:
			VkInstance instance{ nullptr };
			VkSurfaceKHR handle{ nullptr };
		};

		struct DeviceHandle {
			DeviceHandle() = default;

			DeviceHandle(const DeviceHandle&) = delete;
			DeviceHandle& operator=(const DeviceHandle&) = delete;

			DeviceHandle(DeviceHandle&& other) noexcept;
			DeviceHandle& operator=(DeviceHandle&& other) noexcept;

			[[nodiscard]] VkDevice get() const noexcept { return handle; }
			[[nodiscard]] bool valid() const noexcept { return handle != nullptr; }
		private:
			VkDevice handle{ nullptr };
		};

		InstanceHandle instance_{};
		SurfaceHandle surface_{};
		DeviceHandle handle_{};
	};
}