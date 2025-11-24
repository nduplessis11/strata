// engine/gfx/include/strata/gfx/vulkan/vulkan_context.h
#pragma once

#include "strata/platform/wsi_handle.h"
#include "strata/gfx/vulkan/wsi_bridge.h"

#include <cstdint>
#include <limits>

struct VkInstance_T;
struct VkSurfaceKHR_T;
struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;

using VkInstance = VkInstance_T*;
using VkSurfaceKHR = VkSurfaceKHR_T*;
using VkDevice = VkDevice_T*;
using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkQueue = VkQueue_T*;

namespace strata::gfx {
	struct VulkanContextDesc {
		bool enable_validation{ false };
	};

	class VulkanContext {
	public:
		// Factory: creates an instance using the active WSI
		[[nodiscard]] static VulkanContext create(const strata::platform::WsiHandle& wsi, const VulkanContextDesc& desc = {});

		[[nodiscard]] VkInstance		   instance()			   const noexcept { return instance_.get(); }
		[[nodiscard]] bool			       valid()				   const noexcept { return instance_.valid(); }

		[[nodiscard]] VkSurfaceKHR	       surface()			   const noexcept { return surface_.get(); }
		[[nodiscard]] bool			       has_surface()		   const noexcept { return surface_.valid(); }

		[[nodiscard]] VkDevice             device()				   const noexcept { return device_.get(); }
		[[nodiscard]] bool			       has_device()			   const noexcept { return device_.valid(); }

		[[nodiscard]] VkPhysicalDevice     physical_device()	   const noexcept { return physical_; }
		[[nodiscard]] std::uint32_t        graphics_family_index() const noexcept { return graphics_family_; }
		[[nodiscard]] std::uint32_t        present_family_index()  const noexcept { return present_family_; }

		[[nodiscard]] VkQueue			   graphics_queue()		   const noexcept { return graphics_queue_; }
		[[nodiscard]] VkQueue			   present_queue()		   const noexcept { return present_queue_; }

	private:
		VulkanContext() = default;
		// Small RAII type that owns a VkInstance.
		// All the "destroy" logic lives here; VulkanContext just holds one.
		struct InstanceHandle {
			InstanceHandle() = default;
			explicit InstanceHandle(VkInstance h) : handle_(h) {}

			~InstanceHandle();
			InstanceHandle(const InstanceHandle&) = delete;
			InstanceHandle& operator=(const InstanceHandle&) = delete;

			InstanceHandle(InstanceHandle&& other) noexcept;
			InstanceHandle& operator=(InstanceHandle&& other) noexcept;

			[[nodiscard]] VkInstance get() const noexcept { return handle_; }
			[[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }

		private:
			VkInstance handle_{ nullptr };
		};

		struct SurfaceHandle {
			SurfaceHandle() = default;
			SurfaceHandle(VkInstance inst, VkSurfaceKHR h) : instance_(inst), handle_(h) {}

			~SurfaceHandle();

			SurfaceHandle(const SurfaceHandle&) = delete;
			SurfaceHandle& operator=(const SurfaceHandle&) = delete;

			SurfaceHandle(SurfaceHandle&& other) noexcept;
			SurfaceHandle& operator=(SurfaceHandle&& other) noexcept;

			[[nodiscard]] VkSurfaceKHR get() const noexcept { return handle_; }
			[[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }

		private:
			VkInstance instance_{ nullptr };
			VkSurfaceKHR handle_{ nullptr };
		};

		struct DeviceHandle {
			DeviceHandle() = default;
			explicit DeviceHandle(VkDevice d) : handle_(d) {}

			~DeviceHandle();

			DeviceHandle(const DeviceHandle&) = delete;
			DeviceHandle& operator=(const DeviceHandle&) = delete;

			DeviceHandle(DeviceHandle&& other) noexcept;
			DeviceHandle& operator=(DeviceHandle&& other) noexcept;

			[[nodiscard]] VkDevice get() const noexcept { return handle_; }
			[[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
		private:
			VkDevice handle_{ nullptr };
		};

		InstanceHandle instance_{};
		SurfaceHandle surface_{};
		DeviceHandle device_{};

		// Non-owning info about the chosen GPU + queues
		VkPhysicalDevice physical_{ nullptr };
		std::uint32_t graphics_family_{ std::numeric_limits<std::uint32_t>::max() };
		std::uint32_t present_family_{ std::numeric_limits<std::uint32_t>::max() };
		VkQueue graphics_queue_{ nullptr };
		VkQueue present_queue_{ nullptr };
	};
}