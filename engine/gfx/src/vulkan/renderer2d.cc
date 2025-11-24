// -----------------------------------------------------------------------------
// engine/gfx/src/vulkan/renderer2d.cc
//
// pImpl implementation of Renderer2d.
// Owns per-frame Vulkan resources (command pool, command buffer, sync objects)
// and uses the existing VulkanContext + Swapchain to drive rendering.
//
// For now, draw_frame() will be a skeleton we can expand with dynamic
// rendering (vkCmdBeginRendering / vkCmdEndRendering) in the next step.
// -----------------------------------------------------------------------------

#include "strata/gfx/renderer2d.h"

#include <vulkan/vulkan.h>
#include <print>

namespace strata::gfx {

	struct Renderer2d::Impl {
		const VulkanContext* ctx{ nullptr };   // non-owning
		const Swapchain* swapchain{ nullptr }; // non-owning

		VkDevice        device{ VK_NULL_HANDLE };
		VkCommandPool   command_pool{ VK_NULL_HANDLE };
		VkCommandBuffer cmd{ VK_NULL_HANDLE };

		VkSemaphore image_available{ VK_NULL_HANDLE };
		VkSemaphore render_finished{ VK_NULL_HANDLE };
		VkFence     in_flight{ VK_NULL_HANDLE };

		Impl(const VulkanContext& c, const Swapchain& sc)
			: ctx(&c)
			, swapchain(&sc)
			, device(c.device()) {

			// VkCommandBuffer
			//   - A recorded list of GPU commands (draws, clears, pipeline binds, etc.).
			//
			// VkCommandPool
			//   - Allocates and manages the memory backing VkCommandBuffer objects.
			//   - Typically created per-thread (command buffers from a pool must be used
			//     from the same thread that owns the pool).
			//
			// Conceptually:
			//   VkCommandPool (per-thread bucket of command memory)
			//     ├── VkCommandBuffer A (records commands)
			//     ├── VkCommandBuffer B (records commands)
			//     └── VkCommandBuffer C (records commands)

			// --- Command pool ---
			VkCommandPoolCreateInfo pool_ci{};
			pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			pool_ci.queueFamilyIndex = ctx->graphics_family_index();

			if (vkCreateCommandPool(device, &pool_ci, nullptr, &command_pool) != VK_SUCCESS) {
				std::println(stderr, "Renderer2d: failed to create command pool");
				command_pool = VK_NULL_HANDLE;
				return;
			}

			// --- Command buffer ---
			VkCommandBufferAllocateInfo alloc{};
			alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			alloc.commandPool = command_pool;
			alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			alloc.commandBufferCount = 1;

			if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS) {
				std::println(stderr, "Renderer2d: failed to allocate command buffer");
				cmd = VK_NULL_HANDLE;
				// we keep going; destructor will handle what was created
			}

			// --- Synchronization objects ---
			//
			// VkSemaphore
			//   - GPU → GPU synchronization.
			//   - Used to order GPU operations across queues.
			//   - The CPU does NOT wait on semaphores.
			//   - Typical usage in a frame:
			//       • image_available:  GPU waits until the swapchain image is ready.
			//       • render_finished:  GPU waits until drawing is done before presenting.
			//
			//   Conceptually: "Don't start this GPU work until that GPU work is finished."
			//
			// VkFence
			//   - GPU → CPU synchronization.
			//   - The CPU *waits* on a fence (vkWaitForFences) to know when the GPU is done.
			//   - We use this so we can safely reuse command buffers each frame.
			//
			//   Conceptually: "CPU, don't continue until the GPU raises this completion flag."
			//
			// Summary:
			//   Semaphore → GPU waits
			//   Fence     → CPU waits

			VkSemaphoreCreateInfo sem_info{};
			sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkFenceCreateInfo fence_info{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled

			if (vkCreateSemaphore(device, &sem_info, nullptr, &image_available) != VK_SUCCESS ||
				vkCreateSemaphore(device, &sem_info, nullptr, &render_finished) != VK_SUCCESS ||
				vkCreateFence(device, &fence_info, nullptr, &in_flight) != VK_SUCCESS) {
				std::println(stderr, "Renderer2d: failed to create sync objects");
				// In a robust engine, we would handle partial failure more carefully.
			}
		}

		~Impl() {
			if (!device) return;

			if (in_flight)        vkDestroyFence(device, in_flight, nullptr);
			if (image_available)  vkDestroySemaphore(device, image_available, nullptr);
			if (render_finished)  vkDestroySemaphore(device, render_finished, nullptr);

			if (command_pool) {
				vkDestroyCommandPool(device, command_pool, nullptr);
			}
		}

		void draw_frame();
	};

	// ----- Renderer2d forwarding -----------------------------------------------

	Renderer2d::Renderer2d(const VulkanContext& ctx, const Swapchain& swapchain)
		: p_(std::make_unique<Impl>(ctx, swapchain)) {
	}

	Renderer2d::~Renderer2d() = default;

	Renderer2d::Renderer2d(Renderer2d&&) noexcept = default;
	Renderer2d& Renderer2d::operator=(Renderer2d&&) noexcept = default;

	void Renderer2d::draw_frame() {
		if (p_) {
			p_->draw_frame();
		}
	}

	// ----- Impl::draw_frame skeleton -------------------------------------------

	void Renderer2d::Impl::draw_frame() {
		if (!device || !cmd || !ctx || !swapchain) {
			return;
		}

		// This is where we will:
		//  1) wait on in_flight fence
		//  2) acquire a swapchain image
		//  3) reset + record cmd buffer with vkCmdBeginRendering / vkCmdEndRendering
		//  4) submit and present
		//
		// We'll fill this in next, once Swapchain exposes:
		//   - swapchain_handle()   → VkSwapchainKHR
		//   - extent()
		//   - image_views()
	}

} // namespace strata::gfx
