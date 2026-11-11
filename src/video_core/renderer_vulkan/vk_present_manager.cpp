// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdlib>
#include <utility>

#include "common/settings.h"
#include "common/thread.h"
#include "core/frontend/emu_window.h"
#include "video_core/nx2_profile.h"
#ifdef HAS_LSFG
#include "video_core/renderer_vulkan/present/lsfg_common.h"
#endif
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#ifdef EGLANTINE_STREAMLINE
#include "video_core/vulkan_common/streamline.h"
#endif
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {


namespace {

constexpr size_t MAX_FRAMES_IN_FLIGHT = 7;
constexpr u32 MAX_PRESENT_ATTEMPTS = 3;
#ifdef HAS_LSFG
static_assert(MAX_FRAMES_IN_FLIGHT <= LSFG_MAX_TARGETS);
#endif

bool CanStoreToFrame(const vk::PhysicalDevice& physical_device, VkFormat format) {
#ifdef HAS_LSFG
    const VkFormatProperties props{physical_device.GetFormatProperties(format)};
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
#else
    return false;
#endif
}

bool CanBlitToSwapchain(const vk::PhysicalDevice& physical_device, VkFormat format) {
    const VkFormatProperties props{physical_device.GetFormatProperties(format)};
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
}

[[nodiscard]] VkImageSubresourceLayers MakeImageSubresourceLayers() {
    return VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] VkImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                        s32 swapchain_height) {
    return VkImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            {
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                {
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            {
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                {
                    .x = swapchain_width,
                    .y = swapchain_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] VkImageCopy MakeImageCopy(u32 frame_width, u32 frame_height, u32 swapchain_width,
                                        u32 swapchain_height) {
    return VkImageCopy{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .extent =
            {
                .width = (std::min)(frame_width, swapchain_width),
                .height = (std::min)(frame_height, swapchain_height),
                .depth = 1,
            },
    };
}

} // Anonymous namespace

PresentManager::PresentManager(const vk::Instance& instance_,
                               Core::Frontend::EmuWindow& render_window_,
                               const Device& device_,
                               MemoryAllocator& memory_allocator_,
                               Scheduler& scheduler_,
                               Swapchain& swapchain_,
                               vk::SurfaceKHR& surface_)
    : instance{instance_}
    , render_window{render_window_}
    , device{device_}
    , memory_allocator{memory_allocator_}
    , scheduler{scheduler_}
    , swapchain{swapchain_}
    , surface{surface_}
    , blit_supported{CanBlitToSwapchain(device.GetPhysical(), swapchain.GetImageViewFormat())}
    , storage_supported{CanStoreToFrame(device.GetPhysical(), swapchain.GetImageFormat())}
    , use_present_thread{Settings::values.async_presentation.GetValue()}
{
    SetImageCount();

    auto& dld = device.GetLogical();
    cmdpool = dld.CreateCommandPool({
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    });
    auto cmdbuffers = cmdpool.Allocate(image_count);

    frames.resize(image_count);
    for (u32 i = 0; i < frames.size(); i++) {
        Frame& frame = frames[i];
        frame.index = i;
        frame.cmdbuf = vk::CommandBuffer{cmdbuffers[i], device.GetDispatchLoader()};
        frame.render_ready = dld.CreateSemaphore({
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        });
        frame.present_done = dld.CreateFence({
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        });
        frame.storage_capable = storage_supported;
        free_queue.push_back(&frame);
    }

    if (use_present_thread) {
        present_thread = std::jthread([this](std::stop_token token) { PresentThread(token); });
    }
}

PresentManager::~PresentManager() = default;

Frame* PresentManager::GetRenderFrame() {

    VideoCommon::Nx2Profile::Scope scope{VideoCommon::Nx2Profile::Zone::PresentWait};
    // Wait for free presentation frames
    std::unique_lock lock{free_mutex};
    free_cv.wait(lock, [this] { return !free_queue.empty(); });

    // Take the frame from the queue
    Frame* frame = free_queue.front();
    free_queue.pop_front();

    // Wait for the presentation to be finished so all frame resources are free
    frame->present_done.Wait();
    frame->present_done.Reset();

    return frame;
}

void PresentManager::Present(Frame* frame) {
    // The renderer flushed the frame's commands just before: they are the previous tick.
    frame->render_tick = scheduler.CurrentTick() - 1;
    if (use_present_thread) {
        scheduler.Record([this, frame](vk::CommandBuffer) {
#ifdef EGLANTINE_STREAMLINE
            // Right after this frame's commands on the worker: its DLSS evaluation's token.
            frame->frame_gen_token = Streamline::TakeFrameGenToken();
#endif
            std::unique_lock lock{queue_mutex};
            present_queue.push_back(frame);
            frame_cv.notify_one();
        });
    } else {
        {
            VideoCommon::Nx2Profile::Scope scope{VideoCommon::Nx2Profile::Zone::PresentWait};
            scheduler.WaitWorker();
        }
#ifdef EGLANTINE_STREAMLINE
        frame->frame_gen_token = Streamline::TakeFrameGenToken();
#endif
        CopyToSwapchain(frame);
        free_queue.push_back(frame);
    }
}

size_t PresentManager::MaxExtraFrames() const {
    return image_count - 1;
}

bool PresentManager::NeedsStorage(const Frame* frame, bool required) const {
    return required && frame->storage_capable && !frame->storage_view;
}

void PresentManager::RecreateFrame(Frame* frame, u32 width, u32 height, VkFormat image_view_format,
                                   VkRenderPass rd, bool storage) {
    auto& dld = device.GetLogical();

    frame->width = width;
    frame->height = height;

    const bool with_storage = storage && frame->storage_capable;
    VkImageUsageFlags storage_usage = 0;
    if (with_storage) {
        storage_usage = VK_IMAGE_USAGE_STORAGE_BIT;
    }

    frame->image = memory_allocator.CreateImage({
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchain.GetImageFormat(),
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | storage_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });

    frame->image_view = dld.CreateImageView({
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = *frame->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_view_format,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    });

    frame->storage_view = vk::ImageView{};
    if (with_storage) {
        frame->storage_view = dld.CreateImageView({
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = *frame->image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain.GetImageFormat(),
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        });
    }

    const VkImageView image_view{*frame->image_view};
    frame->framebuffer = dld.CreateFramebuffer({
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = rd,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = width,
        .height = height,
        .layers = 1,
    });
}

void PresentManager::WaitPresent() {
    if (!use_present_thread) {
        return;
    }

    // Wait for the present queue to be empty
    {
        std::unique_lock queue_lock{queue_mutex};
        frame_cv.wait(queue_lock, [this] { return present_queue.empty(); });
    }

    // The above condition will be satisfied when the last frame is taken from the queue.
    // To ensure that frame has been presented as well take hold of the swapchain
    // mutex.
    std::scoped_lock swapchain_lock{swapchain_mutex};
}

void PresentManager::PresentThread(std::stop_token token) {
    Common::SetCurrentThreadName("VulkanPresent");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::High);
    Common::SetCurrentThreadToPerformanceCores();
    while (!token.stop_requested()) {
        std::unique_lock lock{queue_mutex};
        // Wait for presentation frames
        frame_cv.wait(lock, token, [this] { return !present_queue.empty(); });
        if (!token.stop_requested()) {
            // Take the frame and notify anyone waiting
            Frame* frame = present_queue.front();
            present_queue.pop_front();
            frame_cv.notify_one();

            // By exchanging the lock ownership we take the swapchain lock
            // before the queue lock goes out of scope. This way the swapchain
            // lock in WaitPresent is guaranteed to occur after here.
            void(std::exchange(lock, std::unique_lock{swapchain_mutex}));
            CopyToSwapchain(frame);

            // Free the frame for reuse
            std::scoped_lock fl{free_mutex};
            free_queue.push_back(frame);
            free_cv.notify_one();
        }
    }
}

void PresentManager::RecreateSwapchain(Frame* frame) {
    swapchain.Create(*surface, frame->width, frame->height); // Pass raw pointer
    SetImageCount();
}

void PresentManager::SetImageCount() {
    // We cannot have more than 7 images in flight at any given time.
    // FRAMES_IN_FLIGHT is 8, and the cache TICKS_TO_DESTROY is 8.
    // Mali drivers will give us 6.
#ifdef HAS_LSFG
    const size_t generations = Settings::FrameGenMaxGenerations();
    const size_t queued_composites = Settings::values.frame_gen_queue_target.GetValue() + 1;
    image_count =
        std::clamp<size_t>((generations + 1) * queued_composites, swapchain.GetImageCount(),
                           MAX_FRAMES_IN_FLIGHT);
#else
    image_count = std::min<size_t>(swapchain.GetImageCount(), MAX_FRAMES_IN_FLIGHT);
#endif
    swapchain_image_count = swapchain.GetImageCount();
    swapchain_image_format = swapchain.GetImageFormat();
}

void PresentManager::DiscardFrame(Frame* frame) {
    static constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const VkSemaphore render_ready = *frame->render_ready;
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1U,
        .pWaitSemaphores = &render_ready,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 0U,
        .pCommandBuffers = nullptr,
        .signalSemaphoreCount = 0U,
        .pSignalSemaphores = nullptr,
    };

    std::scoped_lock submit_lock{scheduler.submit_mutex};
    void(device.GetGraphicsQueue().Submit(submit_info, *frame->present_done));
}

void PresentManager::CopyToSwapchain(Frame* frame) {
    bool requires_recreation = false;

    for (u32 attempt = 0; attempt < MAX_PRESENT_ATTEMPTS; ++attempt) {
        try {
            // Recreate surface and swapchain if needed.
            if (requires_recreation) {
#ifdef __ANDROID__
                surface = CreateSurface(instance, render_window.GetWindowInfo());
#endif
                RecreateSwapchain(frame);
            }

            // Draw to swapchain.
            return CopyToSwapchainImpl(frame);
        } catch (const vk::Exception& except) {
            const VkResult result = except.GetResult();
            if (result != VK_ERROR_SURFACE_LOST_KHR && result != VK_INCOMPLETE &&
                result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) {
                throw;
            }

            requires_recreation = true;
        }
    }

    DiscardFrame(frame);
}

void PresentManager::TagGenericFrameGen(Frame* frame, vk::CommandBuffer cmdbuf) {
#ifdef EGLANTINE_STREAMLINE
    constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    constexpr VkFormat MOTION_FORMAT = VK_FORMAT_R16G16_SFLOAT;
    const u32 width = frame->game_right > frame->game_left ? frame->game_right - frame->game_left
                                                           : swapchain.GetExtent().width;
    const u32 height = frame->game_bottom > frame->game_top ? frame->game_bottom - frame->game_top
                                                            : swapchain.GetExtent().height;
    if (width < 64 || height < 64) {
        return;
    }
    if (!fg_generic_depth || fg_generic_extent.width != width ||
        fg_generic_extent.height != height) {
        if (fg_generic_depth) {
            // Frame generation may still read the old ones on its own queues.
            device.GetLogical().WaitIdle();
        }
        const auto create = [&](VkFormat format, VkImageAspectFlags aspect, vk::Image& image,
                                vk::ImageView& view) {
            image = memory_allocator.CreateImage(VkImageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = format,
                .extent = {width, height, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            });
            view = device.GetLogical().CreateImageView(VkImageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = *image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = format,
                .components{},
                .subresourceRange{aspect, 0, 1, 0, 1},
            });
        };
        create(DEPTH_FORMAT, VK_IMAGE_ASPECT_DEPTH_BIT, fg_generic_depth, fg_generic_depth_view);
        create(MOTION_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, fg_generic_motion,
               fg_generic_motion_view);
        fg_generic_extent = {width, height};
        const auto barrier = [](VkImage image, VkImageAspectFlags aspect, VkAccessFlags src,
                                VkAccessFlags dst, VkImageLayout from, VkImageLayout to) {
            return VkImageMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = src,
                .dstAccessMask = dst,
                .oldLayout = from,
                .newLayout = to,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange{aspect, 0, 1, 0, 1},
            };
        };
        constexpr VkImageLayout DST = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        constexpr VkImageLayout READ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const std::array to_clear{
            barrier(*fg_generic_depth, VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, DST),
            barrier(*fg_generic_motion, VK_IMAGE_ASPECT_COLOR_BIT, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, DST),
        };
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, {}, {}, to_clear);
        // Reversed depth: 0 is the far plane. Zero motion: DLSS-G's optical flow does the rest.
        const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkClearDepthStencilValue far_depth{0.0f, 0};
        const VkClearColorValue zero{};
        cmdbuf.ClearDepthStencilImage(*fg_generic_depth, DST,
                                      far_depth, depth_range);
        cmdbuf.ClearColorImage(*fg_generic_motion, DST, zero, color_range);
        const std::array to_read{
            barrier(*fg_generic_depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, DST, READ),
            barrier(*fg_generic_motion, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, DST, READ),
        };
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, {}, {}, to_read);
        LOG_INFO(Render_Vulkan,
                 "DLSS Frame Generation without the game's motion: optical flow only at {}x{}",
                 width, height);
    }
    frame->frame_gen_token = Streamline::GenericFrameGenToken(
        *fg_generic_depth, *fg_generic_depth_view, DEPTH_FORMAT, *fg_generic_motion,
        *fg_generic_motion_view, MOTION_FORMAT, width, height, *cmdbuf);
#endif
}

void PresentManager::CopyToSwapchainImpl(Frame* frame) {

    // If the size of the incoming frames has changed, recreate the swapchain
    // to account for that.
    const bool is_suboptimal = swapchain.NeedsRecreation() || swapchain.IsOutDated();
    const bool size_changed =
        swapchain.GetWidth() != frame->width || swapchain.GetHeight() != frame->height;
    if (is_suboptimal || size_changed) {
        RecreateSwapchain(frame);
    }
    if (!swapchain.IsCreated()) {
        DiscardFrame(frame);
        return;
    }

#ifdef EGLANTINE_STREAMLINE
    const auto acquire_begin = std::chrono::steady_clock::now();
    // DLSS Frame Generation's pacer takes one present at a time and waits on the GPU for it:
    // presented before the GPU had rendered it, each present waited a whole GPU frame plus
    // frame generation plus half a frame of pacing (~42 ms, half the real frame rate). Hand it
    // rendered frames instead (LICHII_DLSS_FG_PRESENT_WAIT=0 turns this off).
    static const bool wait_render = [] {
        const char* const value = std::getenv("LICHII_DLSS_FG_PRESENT_WAIT");
        return !value || value[0] != '0';
    }();
    if (wait_render && frame->frame_gen_token && frame->render_tick != 0) {
        scheduler.GetMasterSemaphore().Wait(frame->render_tick);
    }
#endif
    while (swapchain.AcquireNextImage(!use_present_thread)) {
        RecreateSwapchain(frame);
    }
#ifdef EGLANTINE_STREAMLINE
    Streamline::NoteFrameGenAcquire(static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                             acquire_begin)
            .count()));
#endif

    const vk::CommandBuffer cmdbuf{frame->cmdbuf};
    cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
#ifdef EGLANTINE_STREAMLINE
    if (!frame->frame_gen_token && Streamline::WantsGenericFrameGen()) {
        TagGenericFrameGen(frame, cmdbuf);
    }
#endif

    const VkImage image{swapchain.CurrentImage()};
    const VkExtent2D extent = swapchain.GetExtent();
    const std::array pre_barriers{
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *frame->image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };
    const std::array post_barriers{
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *frame->image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };

    cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER, VK_PIPELINE_STAGE_TRANSFER_BIT, {},
                           {}, {}, pre_barriers);

    if (blit_supported) {
        cmdbuf.BlitImage(*frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         MakeImageBlit(frame->width, frame->height, extent.width, extent.height),
                         VK_FILTER_LINEAR);
    } else {
        cmdbuf.CopyImage(*frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         MakeImageCopy(frame->width, frame->height, extent.width, extent.height));
    }

    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, {},
                           {}, {}, post_barriers);

    cmdbuf.End();

    const VkSemaphore present_semaphore = swapchain.CurrentPresentSemaphore();
    const VkSemaphore render_semaphore = swapchain.CurrentRenderSemaphore();
    const std::array wait_semaphores = {present_semaphore, *frame->render_ready};

    static constexpr std::array<VkPipelineStageFlags, 2> wait_stage_masks{
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
    };

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 2U,
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = cmdbuf.address(),
        .signalSemaphoreCount = 1U,
        .pSignalSemaphores = &render_semaphore,
    };

    // Submit the image copy/blit to the swapchain
    {
        std::unique_lock submit_lock{scheduler.submit_mutex, std::defer_lock};
        if (!device.HasSwapchainQueue()) {
            submit_lock.lock();
        }
        switch (const VkResult result =
                    (device.HasSwapchainQueue() ? device.GetSwapchainQueue() : device.GetGraphicsQueue())
                        .Submit(submit_info, *frame->present_done)) {
        case VK_SUCCESS:
            break;
        case VK_ERROR_DEVICE_LOST:
            device.ReportLoss();
            [[fallthrough]];
        default:
            vk::Check(result);
            break;
        }
    }

    // Present
#ifdef EGLANTINE_STREAMLINE
    void* const frame_gen_token = std::exchange(frame->frame_gen_token, nullptr);
    Streamline::FrameGenBeforePresent(frame_gen_token, frame->game_left, frame->game_top,
                                      frame->game_right, frame->game_bottom);
#endif
    swapchain.Present(render_semaphore);
#ifdef EGLANTINE_STREAMLINE
    Streamline::FrameGenAfterPresent(frame_gen_token);
#endif
}

} // namespace Vulkan
