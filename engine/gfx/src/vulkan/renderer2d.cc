// -----------------------------------------------------------------------------
// engine/gfx/src/vulkan/renderer2d.cc
//
// pImpl implementation of Renderer2d.
// Owns per-frame Vulkan resources (command pool, primary command buffer, and
// synchronization objects) and uses the existing VulkanContext + Swapchain to
// drive rendering.
//
// draw_frame() records and submits a minimal frame using Vulkan dynamic
// rendering:
//   - waits for the previous frame to finish (fence)
//   - acquires a swapchain image (image_available semaphore)
//   - clears it to a solid color with vkCmdBeginRendering / vkCmdEndRendering
//   - transitions the image to PRESENT_SRC_KHR and queues it for presentation
// -----------------------------------------------------------------------------


#include "strata/gfx/renderer2d.h"

#include <vulkan/vulkan.h>
#include <print>
#include <fstream>
#include <vector>
#include <cstdint>
#include <limits>

namespace {
	std::vector<char> read_binary_file(const char* path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			return {};
		}

		const std::streamsize size = file.tellg();
		if (size <= 0) {
			return {};
		}
		file.seekg(0, std::ios::beg);

		std::vector<char> buffer(static_cast<std::size_t>(size));
		if (!file.read(buffer.data(), size)) {
			return {};
		}
		return buffer;
	}

	VkShaderModule create_shader_module(VkDevice device, std::span<const char> code) {
		VkShaderModuleCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		ci.codeSize = code.size();
		ci.pCode = reinterpret_cast<const std::uint32_t*>(code.data());

		VkShaderModule module = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
			return VK_NULL_HANDLE;
		}
		return module;
	}
}

namespace strata::gfx {

	struct Renderer2d::Impl {
		const VulkanContext* ctx{ nullptr };   // non-owning
		const Swapchain* swapchain{ nullptr }; // non-owning

		VkDevice        device{ VK_NULL_HANDLE };			// non-owning
		VkCommandPool   command_pool{ VK_NULL_HANDLE };		// owning
		VkCommandBuffer cmd{ VK_NULL_HANDLE };				// owning

		VkSemaphore image_available{ VK_NULL_HANDLE };		// owning
		VkSemaphore render_finished{ VK_NULL_HANDLE };		// owning
		VkFence     in_flight{ VK_NULL_HANDLE };			// owning

		VkPipelineLayout pipeline_layout{ VK_NULL_HANDLE }; // owning
		VkPipeline		 pipeline{ VK_NULL_HANDLE };		// owning

		Impl(const VulkanContext& context, const Swapchain& swapchain)
			: ctx(&context)
			, swapchain(&swapchain)
			, device(context.device()) {

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

			auto vert_bytes{ read_binary_file("../../engine/gfx/shaders/fullscreen_triangle.vert.spv") };
			auto frag_bytes{ read_binary_file("../../engine/gfx/shaders/flat_color.frag.spv") };

			if (vert_bytes.empty() || frag_bytes.empty()) {
				std::println(stderr, "Renderer2d: failed to load shader SPIR-V");
				return;
			}

			VkShaderModule vert_module{ create_shader_module(device, vert_bytes) };
			VkShaderModule frag_module{ create_shader_module(device, frag_bytes) };

			if (!vert_module || !frag_module) {
				std::println(stderr, "Renderer2d: failed to create shader modules");
				if (vert_module) vkDestroyShaderModule(device, vert_module, nullptr);
				if (frag_module) vkDestroyShaderModule(device, frag_module, nullptr);
				return;
			}

			// Describe shader stages
			VkPipelineShaderStageCreateInfo vert_stage{};
			vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vert_stage.module = vert_module;
			vert_stage.pName = "main";

			VkPipelineShaderStageCreateInfo frag_stage{};
			frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			frag_stage.module = frag_module;
			frag_stage.pName = "main";

			VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

			// No vertex buffers: everything is generated from gl_VertexIndex
			VkPipelineVertexInputStateCreateInfo vertex_input{};
			vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertex_input.vertexBindingDescriptionCount = 0;
			vertex_input.pVertexBindingDescriptions = nullptr;
			vertex_input.vertexAttributeDescriptionCount = 0;
			vertex_input.pVertexAttributeDescriptions = nullptr;

			VkPipelineInputAssemblyStateCreateInfo input_assembly{};
			input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			input_assembly.primitiveRestartEnable = VK_FALSE;

			// Viewport/scissor: we can bake the initial extent here; on resize we recreate.
			Extent2d extent{ swapchain.extent() };

			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = static_cast<float>(extent.width);
			viewport.height = static_cast<float>(extent.height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor{};
			scissor.offset = { 0, 0 };
			scissor.extent = VkExtent2D{
				static_cast<std::uint32_t>(extent.width),
				static_cast<std::uint32_t>(extent.height)
			};

			VkPipelineViewportStateCreateInfo viewport_state{};
			viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewport_state.viewportCount = 1;
			viewport_state.pViewports = &viewport;
			viewport_state.scissorCount = 1;
			viewport_state.pScissors = &scissor;

			VkPipelineRasterizationStateCreateInfo raster{};
			raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			raster.depthClampEnable = VK_FALSE;
			raster.rasterizerDiscardEnable = VK_FALSE;
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.cullMode = VK_CULL_MODE_BACK_BIT;
			raster.frontFace = VK_FRONT_FACE_CLOCKWISE; // depends on your vertex order
			raster.depthBiasEnable = VK_FALSE;
			raster.lineWidth = 1.0f;

			VkPipelineMultisampleStateCreateInfo msaa{};
			msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			msaa.sampleShadingEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState blend_attach{};
			blend_attach.colorWriteMask =
				VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT |
				VK_COLOR_COMPONENT_A_BIT;
			blend_attach.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.logicOpEnable = VK_FALSE;
			blend.attachmentCount = 1;
			blend.pAttachments = &blend_attach;

			// No descriptor sets / push constants yet
			VkPipelineLayoutCreateInfo layout_ci{};
			layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

			if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS) {
				std::println(stderr, "Renderer2d: failed to create pipeline layout");
				vkDestroyShaderModule(device, vert_module, nullptr);
				vkDestroyShaderModule(device, frag_module, nullptr);
				pipeline_layout = VK_NULL_HANDLE;
				return;
			}

			// Dynamic rendering hook: tell the pipeline which color format it will render into.
			VkFormat color_format = static_cast<VkFormat>(swapchain.color_format_bits());

			VkPipelineRenderingCreateInfo rendering_ci{};
			rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
			rendering_ci.colorAttachmentCount = 1;
			rendering_ci.pColorAttachmentFormats = &color_format;

			VkGraphicsPipelineCreateInfo gp_ci{};
			gp_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			gp_ci.pNext = &rendering_ci;   // <--- key for dynamic rendering
			gp_ci.stageCount = 2;
			gp_ci.pStages = stages;
			gp_ci.pVertexInputState = &vertex_input;
			gp_ci.pInputAssemblyState = &input_assembly;
			gp_ci.pViewportState = &viewport_state;
			gp_ci.pRasterizationState = &raster;
			gp_ci.pMultisampleState = &msaa;
			gp_ci.pDepthStencilState = nullptr;        // no depth/stencil
			gp_ci.pColorBlendState = &blend;
			gp_ci.pDynamicState = nullptr;        // using static viewport/scissor
			gp_ci.layout = pipeline_layout;
			gp_ci.renderPass = VK_NULL_HANDLE; // dynamic rendering, no render pass
			gp_ci.subpass = 0;
			gp_ci.basePipelineHandle = VK_NULL_HANDLE;

			if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_ci, nullptr, &pipeline) != VK_SUCCESS) {
				std::println(stderr, "Renderer2d: failed to create graphics pipeline");
				vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
				pipeline_layout = VK_NULL_HANDLE;
				vkDestroyShaderModule(device, vert_module, nullptr);
				vkDestroyShaderModule(device, frag_module, nullptr);
				pipeline = VK_NULL_HANDLE;
				return;
			}

			// Shader modules can be destroyed after pipeline creation
			vkDestroyShaderModule(device, vert_module, nullptr);
			vkDestroyShaderModule(device, frag_module, nullptr);
		}

		~Impl() {
			if (!device) return;

			if (pipeline)		  vkDestroyPipeline(device, pipeline, nullptr);
			if (pipeline_layout)  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);

			if (in_flight)        vkDestroyFence(device, in_flight, nullptr);
			if (image_available)  vkDestroySemaphore(device, image_available, nullptr);
			if (render_finished)  vkDestroySemaphore(device, render_finished, nullptr);

			if (command_pool)	  vkDestroyCommandPool(device, command_pool, nullptr);
		}

		FrameResult draw_frame();
	};

	// ----- Renderer2d forwarding -----------------------------------------------

	Renderer2d::Renderer2d(const VulkanContext& ctx, const Swapchain& swapchain)
		: p_(std::make_unique<Impl>(ctx, swapchain)) {
	}

	Renderer2d::~Renderer2d() = default;

	Renderer2d::Renderer2d(Renderer2d&&) noexcept = default;
	Renderer2d& Renderer2d::operator=(Renderer2d&&) noexcept = default;

	FrameResult Renderer2d::draw_frame() {
		if (!p_) {
			return FrameResult::Error;
		}
		return p_->draw_frame();
	}

	// ----- Renderer2d::Impl -------------------------------------------

	FrameResult Renderer2d::Impl::draw_frame() {
		// Basic sanity check: if any of the core pieces are missing, bail out.
		// In normal usage this shouldn't happen, but it makes the function robust
		// against partially constructed / torn-down state.
		if (!device || !cmd || !ctx || !swapchain->valid()) {
			return FrameResult::Error;
		}

		using u64 = std::uint64_t;

		// Timeout used both for vkWaitForFences and vkAcquireNextImageKHR.
		constexpr u64 kTimeout = std::numeric_limits<u64>::max();

		// ---------------------------------------------------------------------
		// 1) Wait for GPU to finish the previous frame
		// ---------------------------------------------------------------------
		//
		// The fence 'in_flight' is signaled by vkQueueSubmit at the end of this
		// function. Waiting on it here guarantees:
		//   - the command buffer is no longer in use by the GPU
		//   - the swapchain image we used last frame is considered done
		//
		// After the wait, we reset the fence back to the "unsignaled" state so
		// it can be used again for the next vkQueueSubmit.
		VkResult waitRes{ vkWaitForFences(device, 1, &in_flight, VK_TRUE, kTimeout) };
		if (waitRes != VK_SUCCESS) {
			// VK_TIMEOUT shouldn't happen with "infinite" timeout, but be defensive.
			std::println(stderr, "vkWaitForFences failed: {}", static_cast<int>(waitRes));
			return FrameResult::Error;
		}
		vkResetFences(device, 1, &in_flight);

		// ---------------------------------------------------------------------
		// 2) Acquire next image from the swapchain
		// ---------------------------------------------------------------------
		//
		// vkAcquireNextImageKHR:
		//   - picks which swapchain image we should render into this frame
		//   - signals 'image_available' when that image is ready for use
		//
		// We don't use a fence here; we rely on the semaphore for GPU-GPU sync.
		uint32_t image_index = 0;
		VkResult acquire_result = vkAcquireNextImageKHR(
			device,
			swapchain->handle(),   // VkSwapchainKHR
			kTimeout,              // timeout (ns)
			image_available,       // signaled when the image is ready
			VK_NULL_HANDLE,        // optional fence (unused here)
			&image_index           // out: which image index to render to
		);

		if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
			// Window was resized or surface became incompatible with the swapchain.
			return FrameResult::SwapchainOutOfDate;
		}
		if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
			std::println(stderr, "vkAcquireNextImageKHR failed: {}",
				static_cast<int>(acquire_result));
			return FrameResult::Error;
		}

		// ---------------------------------------------------------------------
		// 3) Reset and begin command buffer recording
		// ---------------------------------------------------------------------
		//
		// For simplicity we record a fresh command buffer every frame.
		vkResetCommandBuffer(cmd, 0);

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = 0; // could be VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT

		if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
			std::println(stderr, "vkBeginCommandBuffer failed");
			return FrameResult::Error;
		}

		// Pull out swapchain image/view/extent for the image we just acquired.
		auto    images = swapchain->images();
		auto    views = swapchain->image_views();
		Extent2d extent = swapchain->extent();

		VkImage     image = images[image_index];
		VkImageView view = views[image_index];

		// ---------------------------------------------------------------------
		// 4) Transition image from UNDEFINED/PRESENT to COLOR_ATTACHMENT_OPTIMAL
		// ---------------------------------------------------------------------
		//
		// We want to use the image as a color attachment. That requires:
		//   - transitioning its layout to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		//   - making sure writes to the color attachment are properly synchronized
		//
		// Because we're going to CLEAR the image (and don't care about old
		// contents), we can safely pretend the old layout is UNDEFINED here.
		//
		// Layouts  = *roles* of an image
		// Barriers = *safe switches* between roles
		//
		// These are the GPU's contract
		// They prevent: tearing, undefined pixels, partially rendered frames, etc..
		VkImageMemoryBarrier pre{};
		pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		pre.srcAccessMask = 0;									  // nothing to wait on before this (top-of-pipe)
		pre.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		pre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;				  // previous contents discarded
		pre.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		pre.image = image;
		pre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		pre.subresourceRange.baseMipLevel = 0;
		pre.subresourceRange.levelCount = 1;
		pre.subresourceRange.baseArrayLayer = 0;
		pre.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,             // before: nothing is running yet
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // after: color attachment writes
			0,
			0, nullptr,
			0, nullptr,
			1, &pre
		);

		// ---------------------------------------------------------------------
		// 5) Begin dynamic rendering with a single color attachment
		// ---------------------------------------------------------------------
		//
		// Dynamic rendering replaces the classic "render pass + framebuffer" setup.
		// Here we say:
		//   - which image view we render into
		//   - what layout it is in
		//   - how to load/store it
		//   - what region (renderArea) we care about
		//
		// This frame just clears to a solid "debug" color, but the same rendering
		// region would be used for actual draw calls later.
		VkClearValue clear{};
		clear.color = { { 0.6f, 0.4f, 0.8f, 1.0f } }; // soft light purple

		VkRenderingAttachmentInfo color_attach{};
		color_attach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attach.imageView = view;                                       // target image view
		color_attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                   // clear at start
		color_attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;                 // keep result for present
		color_attach.clearValue = clear;                                     // clear color
		color_attach.resolveMode = VK_RESOLVE_MODE_NONE;                     // no MSAA resolve
		color_attach.resolveImageView = VK_NULL_HANDLE;
		color_attach.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkRenderingInfo render_info{};
		render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		render_info.renderArea.offset = { 0, 0 };
		render_info.renderArea.extent = VkExtent2D{
			static_cast<uint32_t>(extent.width),
			static_cast<uint32_t>(extent.height)
		};
		render_info.layerCount = 1;
		render_info.colorAttachmentCount = 1;
		render_info.pColorAttachments = &color_attach;
		render_info.pDepthAttachment = nullptr;
		render_info.pStencilAttachment = nullptr;

		// "Start" dynamic rendering. From here until vkCmdEndRendering, any
		// draw calls that write to color attachments will target 'color_attach'.
		vkCmdBeginRendering(cmd, &render_info);

		// Bind our graphics pipeline
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Fullscreen triangle: 3 vertices, 1 instance, firstVertex = 0, firstInstance = 0
		vkCmdDraw(cmd, 3, 1, 0, 0);

		// TODO: later:
		//   - bind descriptor sets / vertex buffers

		vkCmdEndRendering(cmd);

		// ---------------------------------------------------------------------
		// 6) Transition image to PRESENT_SRC_KHR for presentation
		// ---------------------------------------------------------------------
		//
		// After rendering, the image is in COLOR_ATTACHMENT_OPTIMAL. The present
		// engine expects PRESENT_SRC_KHR. This barrier makes that transition and
		// ensures all color writes are finished before presentation.
		VkImageMemoryBarrier post{};
		post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		post.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // finish all color writes
		post.dstAccessMask = 0;                                    // presentation doesn't need a memory access mask
		post.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		post.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		post.image = image;
		post.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		post.subresourceRange.baseMipLevel = 0;
		post.subresourceRange.levelCount = 1;
		post.subresourceRange.baseArrayLayer = 0;
		post.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // after color output
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // before "end of pipe"
			0,
			0, nullptr,
			0, nullptr,
			1, &post
		);

		// ---------------------------------------------------------------------
		// 7) End command buffer recording
		// ---------------------------------------------------------------------
		if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
			std::println(stderr, "vkEndCommandBuffer failed");
			return FrameResult::Error;
		}

		// ---------------------------------------------------------------------
		// 8) Submit to the graphics queue
		// ---------------------------------------------------------------------
		//
		// We submit:
		//   - wait on 'image_available' so we don't render before the image is ready
		//   - execute our command buffer
		//   - signal 'render_finished' when rendering is complete
		//   - associate the submission with 'in_flight' fence so the CPU can wait
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &image_available;
		submit.pWaitDstStageMask = &wait_stage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &render_finished;

		if (vkQueueSubmit(ctx->graphics_queue(), 1, &submit, in_flight) != VK_SUCCESS) {
			std::println(stderr, "vkQueueSubmit failed");
			return FrameResult::Error;
		}

		// ---------------------------------------------------------------------
		// 9) Present the rendered image to the surface
		// ---------------------------------------------------------------------
		//
		// Presentation waits on 'render_finished' so the present engine doesn't
		// read from the image until rendering is complete.
		VkSwapchainKHR sw = swapchain->handle();

		VkPresentInfoKHR present{};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &render_finished;
		present.swapchainCount = 1;
		present.pSwapchains = &sw;
		present.pImageIndices = &image_index;
		present.pResults = nullptr; // per-swapchain results (optional)

		VkResult pres = vkQueuePresentKHR(ctx->present_queue(), &present);
		if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
			// The swapchain is no longer optimal/valid for this surface.
			return FrameResult::SwapchainOutOfDate;
		}
		else if (pres != VK_SUCCESS) {
			std::println(stderr, "vkQueuePresentKHR failed: {}", static_cast<int>(pres));
			return FrameResult::Error;
		}
		return FrameResult::Ok;
	}

	FrameResult draw_frame_and_handle_resize(const VulkanContext& ctx, Swapchain& swapchain, Renderer2d& renderer, Extent2d framebuffer_size) {
		// 0) If window is minimized (0x0), don't do *any* Vulkan work.
		//    This avoids swapchain creation with invalid extents and keeps things sane.
		if (framebuffer_size.width == 0 || framebuffer_size.height == 0) {
			return FrameResult::Ok; // "nothing to do this frame"
		}

		// 1) Draw one frame.
		FrameResult result{ renderer.draw_frame() };
		if (result == FrameResult::Ok) {
			return FrameResult::Ok;
		}
		if (result == FrameResult::Error) {
			return FrameResult::Error;
		}

		// -----------------------------------------------------------------
		// 2) Swapchain is out of date – handle resize / mode change.
		// -----------------------------------------------------------------
		// At this point result == FrameResult::SwapchainOutOfDate.

		// Make sure GPU is idle before we tear down / replace swapchain.
		vkDeviceWaitIdle(ctx.device());

		// Recreate swapchain for the current framebuffer size.
		// Use old swapchain handle so WSI knows we’re replacing it.
		VkSwapchainKHR old_handle{ swapchain.handle() };

		Swapchain new_swapchain{ Swapchain::create(ctx, framebuffer_size, old_handle) };

		if (!new_swapchain.valid()) {
			std::println(stderr,
				"draw_frame_and_handle_resize: swapchain recreation failed; will retry");
			// Old swapchain is still valid; we just skip this frame.
			return FrameResult::Ok;
		}

		// Move-assign: RAII destroys the old VkSwapchainKHR, image views, etc.
		swapchain = std::move(new_swapchain);

		// Recreate renderer so its internal pointers refer to the new swapchain.
		renderer = Renderer2d{ ctx, swapchain };

		// We didn’t present anything this frame, but we recovered.
		return FrameResult::Ok;
	}
} // namespace strata::gfx
