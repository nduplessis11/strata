// engine/gfx/src/vulkan/vulkan_context.cc
#include "strata/gfx/vulkan/vulkan_context.h"

#include <vulkan/vulkan.h>
#include <print>
#include <vector>

namespace strata::gfx {
	VulkanContext::InstanceHandle::~InstanceHandle() {
		if (handle) {
			vkDestroyInstance(handle, nullptr);
			handle = VK_NULL_HANDLE;
		}
	}

	VulkanContext::InstanceHandle::InstanceHandle(InstanceHandle&& other) noexcept
		: handle(other.handle) {
		other.handle = VK_NULL_HANDLE;
	}

	VulkanContext::InstanceHandle& VulkanContext::InstanceHandle::operator=(InstanceHandle&& other) noexcept {
		if (this != &other) {
			if (handle) {
				vkDestroyInstance(handle, nullptr);
			}
			handle = other.handle;
			other.handle = VK_NULL_HANDLE;
		}
		return *this;
	}

	VulkanContext::SurfaceHandle::~SurfaceHandle() {
		if (instance != VK_NULL_HANDLE && handle != VK_NULL_HANDLE) {
			vkDestroySurfaceKHR(instance, handle, nullptr);
			handle = VK_NULL_HANDLE;
			instance = VK_NULL_HANDLE;
		}
	}

	VulkanContext::SurfaceHandle::SurfaceHandle(SurfaceHandle&& other) noexcept
		: instance(other.instance), handle(other.handle) {
		other.instance = VK_NULL_HANDLE;
		other.handle = VK_NULL_HANDLE;
	}

	VulkanContext::SurfaceHandle&
		VulkanContext::SurfaceHandle::operator=(SurfaceHandle&& other) noexcept {
		if (this != &other) {
			if (instance != VK_NULL_HANDLE && handle != VK_NULL_HANDLE) {
				vkDestroySurfaceKHR(instance, handle, nullptr);
			}
			instance = other.instance;
			handle = other.handle;
			other.instance = VK_NULL_HANDLE;
			other.handle = VK_NULL_HANDLE;
		}
		return *this;
	}

	VulkanContext::DeviceHandle::~DeviceHandle() {
		if (handle) {
			vkDestroyDevice(handle, nullptr);
			handle = VK_NULL_HANDLE;
		}
	}

	VulkanContext::DeviceHandle::DeviceHandle(DeviceHandle&& other) noexcept
		: handle(other.handle) {
		other.handle = VK_NULL_HANDLE;
	}

	VulkanContext::DeviceHandle&
		VulkanContext::DeviceHandle::operator=(DeviceHandle&& other) noexcept {
		if (this != &other) {
			if (handle) {
				vkDestroyDevice(handle, nullptr);
			}
			handle = other.handle;
			other.handle = VK_NULL_HANDLE;
		}
		return *this;
	}

	VulkanContext VulkanContext::create(const strata::platform::WsiHandle& wsi,
		const VulkanContextDesc& desc) {
		VulkanContext ctx{};

		// 1) Require WSI instance extensions (Win32: surface + win32_surface)
		auto span_exts = vk::required_instance_extensions(wsi);

		// We expose them as std::string_view; Vulkan wants const char* const*.
		// Here all views refer to static string literals (from Vulkan headers),
		// so .data() is null-terminated and has static lifetime.
		std::vector<const char*> exts;
		exts.reserve(span_exts.size());
		for (auto sv : span_exts) {
			exts.push_back(sv.data());
		}

		// Later: if (desc.enable_validation) { add VK_EXT_debug_utils, layers, etc. }
		(void)desc;

		VkApplicationInfo app{};
		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.pApplicationName = "strata";
		app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		app.pEngineName = "strata";
		app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		app.apiVersion = VK_API_VERSION_1_2;

		VkInstanceCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		ci.pApplicationInfo = &app;
		ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
		ci.ppEnabledExtensionNames = exts.data();

		VkInstance instance = VK_NULL_HANDLE;
		const VkResult res = vkCreateInstance(&ci, nullptr, &instance);
		if (res != VK_SUCCESS) {
			std::println(stderr, "VkCreateInstance failed");
			return ctx; // ctx.instance_ stays empty -> valid() == false
		}

		// Wrap in RAII handle; VulkanContext stays Rule of Zero.
		ctx.instance_ = InstanceHandle{ instance };

		VkSurfaceKHR surface = vk::create_surface(instance, wsi);
		if (!surface) {
			std::println(stderr, "vk::create_surface failed");
			// Depending on policy:
			// - either leave ctx.surface_ invalid, but ctx.valid() is still true (instance only)
			// - or consider that a fatal error and return an "invalid" context
			return ctx;
		}

		ctx.surface_ = SurfaceHandle{instance, surface };
		return ctx;
	}
}