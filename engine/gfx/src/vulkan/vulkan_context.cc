// engine/gfx/src/vulkan/vulkan_context.cc
#include "strata/gfx/vulkan/vulkan_context.h"

#include <vulkan/vulkan.h>
#include <print>
#include <vector>

namespace strata::gfx {
	VulkanContext::InstanceHandle::~InstanceHandle() {
		if (handle) {
			vkDestroyInstance(handle, nullptr);
			handle = nullptr;
		}
	}

	VulkanContext::InstanceHandle::InstanceHandle(InstanceHandle&& other) noexcept
		: handle(other.handle) {
		other.handle = nullptr;
	}

	VulkanContext::InstanceHandle& VulkanContext::InstanceHandle::operator=(InstanceHandle&& other) noexcept {
		if (this != &other) {
			if (handle) {
				vkDestroyInstance(handle, nullptr);
			}
			handle = other.handle;
			other.handle = nullptr;
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

		VkInstance inst = VK_NULL_HANDLE;
		const VkResult res = vkCreateInstance(&ci, nullptr, &inst);
		if (res != VK_SUCCESS) {
			std::println(stderr, "VkCreateInstance failed");
			return ctx; // ctx.instance_ stays empty -> valid() == false
		}

		// Wrap in RAII handle; VulkanContext stays Rule of Zero.
		ctx.instance_ = InstanceHandle{ inst };
		return ctx;
	}
}