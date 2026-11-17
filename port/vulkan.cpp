#include "vulkan.h"
#include "city_assets.h"
#include "city_particles.h"
#include "city_view.h"
#include "intro_movie.h"
#include "menu_font.h"
#include "menu_runtime.h"
#include "menu_swf.h"
#include "retail_achievements.h"
#include "retail_audio.h"
#include "retail_sprite.h"
#include "retail_minimap.h"
#include "retail_radio.h"
#include "retail_save.h"
#include "startup_logo.h"
#include "win32_touch.h"
#include "StateMachine.h"
#include "filesystem.h"
#include "retail_constants.h"
#include "IFileStream.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
}

struct Vulkan {
    HMODULE loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    PFN_vkGetInstanceProcAddr get = loader ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(loader, "vkGetInstanceProcAddr")) : nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkBuffer upload = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImage texture = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkSemaphore> rendered;

    template<class Function> Function load(const char* name) const
    {
        const auto address = get(instance, name);
        if (!address) throw std::runtime_error(std::string("Vulkan loader is missing ") + name);
        return reinterpret_cast<Function>(address);
    }

    ~Vulkan();
};

#define VK_CALL(name) vk.load<PFN_##name>(#name)

Vulkan::~Vulkan()
{
    const auto& vk = *this;
    if (device) {
        VK_CALL(vkDeviceWaitIdle)(device);
        if (acquired) VK_CALL(vkDestroySemaphore)(device, acquired, nullptr);
        for (auto semaphore : rendered)
            if (semaphore) VK_CALL(vkDestroySemaphore)(device, semaphore, nullptr);
        if (pool) VK_CALL(vkDestroyCommandPool)(device, pool, nullptr);
        if (upload) VK_CALL(vkDestroyBuffer)(device, upload, nullptr);
        if (memory) VK_CALL(vkFreeMemory)(device, memory, nullptr);
        if (texture) VK_CALL(vkDestroyImage)(device, texture, nullptr);
        if (textureMemory) VK_CALL(vkFreeMemory)(device, textureMemory, nullptr);
        if (swapchain) VK_CALL(vkDestroySwapchainKHR)(device, swapchain, nullptr);
        VK_CALL(vkDestroyDevice)(device, nullptr);
    }
    if (surface) VK_CALL(vkDestroySurfaceKHR)(instance, surface, nullptr);
    if (instance) VK_CALL(vkDestroyInstance)(instance, nullptr);
    if (loader) FreeLibrary(loader);
}

void openDevice(Vulkan& vk, HWND window)
{
    if (!vk.get)
        throw std::runtime_error("The 32-bit Vulkan loader is unavailable. Install a Vulkan-capable GPU driver.");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "OpenGangstar";
    app.apiVersion = VK_API_VERSION_1_0;
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = extensions;
    check(VK_CALL(vkCreateInstance)(&instanceInfo, nullptr, &vk.instance), "vkCreateInstance");

    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = window;
    check(VK_CALL(vkCreateWin32SurfaceKHR)(vk.instance, &surfaceInfo, nullptr, &vk.surface),
          "vkCreateWin32SurfaceKHR");

    uint32_t gpuCount = 0;
    check(VK_CALL(vkEnumeratePhysicalDevices)(vk.instance, &gpuCount, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(gpuCount);
    check(VK_CALL(vkEnumeratePhysicalDevices)(vk.instance, &gpuCount, devices.data()), "vkEnumeratePhysicalDevices");
    for (auto candidate : devices) {
        uint32_t count = 0;
        VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties)(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties)(candidate, &count, families.data());
        for (uint32_t i = 0; i < count; ++i) {
            if (!families[i].queueCount || !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = false;
            check(VK_CALL(vkGetPhysicalDeviceSurfaceSupportKHR)(candidate, i, vk.surface, &present),
                  "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (!present) continue;
            vk.gpu = candidate;
            vk.family = i;
            break;
        }
        if (vk.gpu) break;
    }
    if (!vk.gpu) throw std::runtime_error("No 32-bit Vulkan graphics queue can present to the window.");

    float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = vk.family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
    check(VK_CALL(vkCreateDevice)(vk.gpu, &deviceInfo, nullptr, &vk.device), "vkCreateDevice");
    VK_CALL(vkGetDeviceQueue)(vk.device, vk.family, 0, &vk.queue);
}

VkSwapchainCreateInfoKHR swapchainInfo(const Vulkan& vk, HWND window, bool renderScene)
{
    VkSurfaceCapabilitiesKHR caps{};
    check(VK_CALL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(vk.gpu, vk.surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (renderScene) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((caps.supportedUsageFlags & usage) != usage)
        throw std::runtime_error("The Vulkan surface does not support the required swapchain image usage.");

    uint32_t count = 0;
    const auto getFormats = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR);
    check(getFormats(vk.gpu, vk.surface, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(getFormats(vk.gpu, vk.surface, &count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    const auto selected = std::find_if(formats.begin(), formats.end(), [&](VkSurfaceFormatKHR format) {
        if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
            (format.format != VK_FORMAT_B8G8R8A8_UNORM && format.format != VK_FORMAT_B8G8R8A8_SRGB &&
             format.format != VK_FORMAT_R8G8B8A8_UNORM && format.format != VK_FORMAT_R8G8B8A8_SRGB))
            return false;
        VkFormatProperties properties{};
        VK_CALL(vkGetPhysicalDeviceFormatProperties)(vk.gpu, format.format, &properties);
        constexpr auto blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        return (properties.optimalTilingFeatures & blit) == blit;
    });
    if (selected == formats.end()) throw std::runtime_error("No Vulkan surface format supports sprite blits.");

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        RECT rect{};
        if (!GetClientRect(window, &rect)) throw std::runtime_error("Could not measure the Win32 window.");
        extent.width = std::clamp(static_cast<uint32_t>(rect.right), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(rect.bottom), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!extent.width || !extent.height) throw std::runtime_error("The Vulkan surface has no drawable extent.");
    const auto alpha = caps.supportedCompositeAlpha;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = vk.surface;
    info.minImageCount = caps.minImageCount;
    info.imageFormat = selected->format;
    info.imageColorSpace = selected->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = usage;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(alpha & (~alpha + 1u));
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    return info;
}

VkDeviceMemory allocate(const Vulkan& vk, const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties memory{};
    VK_CALL(vkGetPhysicalDeviceMemoryProperties)(vk.gpu, &memory);
    uint32_t index = 0;
    while (index < memory.memoryTypeCount &&
           (!(requirements.memoryTypeBits & (1u << index)) || (memory.memoryTypes[index].propertyFlags & flags) != flags))
        ++index;
    if (index == memory.memoryTypeCount) throw std::runtime_error("No compatible Vulkan memory type.");

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = index;
    VkDeviceMemory block = VK_NULL_HANDLE;
    check(VK_CALL(vkAllocateMemory)(vk.device, &allocation, nullptr, &block), "vkAllocateMemory");
    return block;
}

void prepareUpload(Vulkan& vk, VkFormat format, VkExtent2D size)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = static_cast<VkDeviceSize>(size.width) * size.height * sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(VK_CALL(vkCreateBuffer)(vk.device, &bufferInfo, nullptr, &vk.upload), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    VK_CALL(vkGetBufferMemoryRequirements)(vk.device, vk.upload, &requirements);
    constexpr auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vk.memory = allocate(vk, requirements, host);
    check(VK_CALL(vkBindBufferMemory)(vk.device, vk.upload, vk.memory, 0), "vkBindBufferMemory");

    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {size.width, size.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    check(VK_CALL(vkCreateImage)(vk.device, &image, nullptr, &vk.texture), "vkCreateImage");
    VK_CALL(vkGetImageMemoryRequirements)(vk.device, vk.texture, &requirements);
    vk.textureMemory = allocate(vk, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(VK_CALL(vkBindImageMemory)(vk.device, vk.texture, vk.textureMemory, 0), "vkBindImageMemory");
}

void uploadImage(Vulkan& vk, const VkSwapchainCreateInfoKHR& info, const Image& image)
{
    const auto bytes = static_cast<VkDeviceSize>(image.bgra.size()) * sizeof(uint32_t);
    void* mapped = nullptr;
    check(VK_CALL(vkMapMemory)(vk.device, vk.memory, 0, bytes, 0, &mapped), "vkMapMemory");
    auto* pixels = static_cast<uint32_t*>(mapped);
    const bool rgba = info.imageFormat == VK_FORMAT_R8G8B8A8_UNORM ||
                      info.imageFormat == VK_FORMAT_R8G8B8A8_SRGB;
    for (uint32_t color : image.bgra) {
        if (rgba) color = (color & 0x0000ff00) | ((color & 0x00ff0000) >> 16) | ((color & 0xff) << 16);
        *pixels++ = color | 0xff000000;
    }
    VK_CALL(vkUnmapMemory)(vk.device, vk.memory);
}

void prepareFrames(Vulkan& vk)
{
    uint32_t count = 0;
    check(VK_CALL(vkGetSwapchainImagesKHR)(vk.device, vk.swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
    vk.images.resize(count);
    check(VK_CALL(vkGetSwapchainImagesKHR)(vk.device, vk.swapchain, &count, vk.images.data()), "vkGetSwapchainImagesKHR");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(VK_CALL(vkCreateSemaphore)(vk.device, &semaphore, nullptr, &vk.acquired), "vkCreateSemaphore");
    // Presentation consumes a semaphore separately from command execution.
    // Reacquiring its image proves that semaphore is safe to use again.
    vk.rendered.resize(count);
    for (auto& complete : vk.rendered)
        check(VK_CALL(vkCreateSemaphore)(vk.device, &semaphore, nullptr, &complete), "vkCreateSemaphore");
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = vk.family;
    check(VK_CALL(vkCreateCommandPool)(vk.device, &poolInfo, nullptr, &vk.pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = vk.pool;
    allocation.commandBufferCount = 1;
    check(VK_CALL(vkAllocateCommandBuffers)(vk.device, &allocation, &vk.command), "vkAllocateCommandBuffers");
}

uint32_t beginFrame(Vulkan& vk)
{
    uint32_t index = 0;
    check(VK_CALL(vkAcquireNextImageKHR)(vk.device, vk.swapchain, UINT64_MAX, vk.acquired, VK_NULL_HANDLE, &index),
          "vkAcquireNextImageKHR");
    check(VK_CALL(vkResetCommandPool)(vk.device, vk.pool, 0), "vkResetCommandPool");
    const auto command = vk.command;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(VK_CALL(vkBeginCommandBuffer)(command, &begin), "vkBeginCommandBuffer");
    return index;
}

void finishFrame(Vulkan& vk, uint32_t index, VkPipelineStageFlags waitStage)
{
    check(VK_CALL(vkEndCommandBuffer)(vk.command), "vkEndCommandBuffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &vk.acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &vk.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &vk.rendered.at(index);
    check(VK_CALL(vkQueueSubmit)(vk.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &vk.rendered.at(index);
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vk.swapchain;
    presentInfo.pImageIndices = &index;
    check(VK_CALL(vkQueuePresentKHR)(vk.queue, &presentInfo), "vkQueuePresentKHR");
    check(VK_CALL(vkQueueWaitIdle)(vk.queue), "vkQueueWaitIdle");
}

void present(Vulkan& vk, VkExtent2D source, VkExtent2D canvas, VkRect2D viewport,
             const std::vector<SpriteRegion>& regions, const VkClearColorValue* clear)
{
    const auto index = beginFrame(vk);
    const auto command = vk.command;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.image = vk.texture;
    VK_CALL(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {source.width, source.height, 1};
    VK_CALL(vkCmdCopyBufferToImage)(command, vk.upload, vk.texture, barrier.newLayout, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VK_CALL(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = vk.images.at(index);
    VK_CALL(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);

    // The atlas parts can overlap. Preserve the frame's draw order and the white clear.
    const auto afterWrite = [&] {
        VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        VK_CALL(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      0, 1, &memory, 0, nullptr, 0, nullptr);
    };
    if (clear) {
        VK_CALL(vkCmdClearColorImage)(command, barrier.image, barrier.newLayout, clear, 1,
                                      &barrier.subresourceRange);
        afterWrite();
    }
    const auto screenX = [&](uint32_t x) {
        return viewport.offset.x + int32_t(uint64_t(x) * viewport.extent.width / canvas.width);
    };
    const auto screenY = [&](uint32_t y) {
        return viewport.offset.y + int32_t(uint64_t(y) * viewport.extent.height / canvas.height);
    };
    for (const auto& rect : regions) {
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = copy.imageSubresource;
        blit.srcOffsets[0] = {int32_t(rect.sourceX), int32_t(rect.sourceY), 0};
        blit.srcOffsets[1] = {int32_t(rect.sourceX + rect.width), int32_t(rect.sourceY + rect.height), 1};
        if (rect.flipX) std::swap(blit.srcOffsets[0].x, blit.srcOffsets[1].x);
        if (rect.flipY) std::swap(blit.srcOffsets[0].y, blit.srcOffsets[1].y);
        blit.dstOffsets[0] = {screenX(rect.targetX), screenY(rect.targetY), 0};
        blit.dstOffsets[1] = {screenX(rect.targetX + rect.width), screenY(rect.targetY + rect.height), 1};
        if (blit.dstOffsets[0].x == blit.dstOffsets[1].x || blit.dstOffsets[0].y == blit.dstOffsets[1].y)
            continue;
        VK_CALL(vkCmdBlitImage)(command, vk.texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               barrier.image, barrier.newLayout, 1, &blit, VK_FILTER_NEAREST);
        afterWrite();
    }

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = barrier.newLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VK_CALL(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);
    finishFrame(vk, index, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

struct CityTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

struct CityFragmentConstants {
    std::array<float, 4> tintAndCutoff;
    float brightness;
};

struct CityVehicleEffect {
    CityScene scene;
    CityAnimationClip clip{};
    size_t node = 0;
    CityParticleSimulation simulation;
    std::string texture;
    uint32_t firstVertex = 0;
    float elapsedMs = 0;
};

constexpr int skipArtworkWidth = 90;
constexpr int skipWidth = 180;
constexpr int skipHeight = 74;
constexpr int skipLeft = 480 - skipWidth;

struct CityRender {
    explicit CityRender(Vulkan& context) : context(context) {}
    CityRender(const CityRender&) = delete;
    CityRender& operator=(const CityRender&) = delete;
    ~CityRender()
    {
        auto& vk = context;
        VK_CALL(vkDeviceWaitIdle)(vk.device);
        if (explosionPipeline) VK_CALL(vkDestroyPipeline)(vk.device, explosionPipeline, nullptr);
        if (checkpointPipeline) VK_CALL(vkDestroyPipeline)(vk.device, checkpointPipeline, nullptr);
        if (captionPipeline) VK_CALL(vkDestroyPipeline)(vk.device, captionPipeline, nullptr);
        if (vehicleExteriorPipeline) VK_CALL(vkDestroyPipeline)(vk.device, vehicleExteriorPipeline, nullptr);
        if (pipeline) VK_CALL(vkDestroyPipeline)(vk.device, pipeline, nullptr);
        if (layout) VK_CALL(vkDestroyPipelineLayout)(vk.device, layout, nullptr);
        if (texturePool) VK_CALL(vkDestroyDescriptorPool)(vk.device, texturePool, nullptr);
        if (textureLayout) VK_CALL(vkDestroyDescriptorSetLayout)(vk.device, textureLayout, nullptr);
        if (sampler) VK_CALL(vkDestroySampler)(vk.device, sampler, nullptr);
        if (districtSampler) VK_CALL(vkDestroySampler)(vk.device, districtSampler, nullptr);
        if (minimapSampler) VK_CALL(vkDestroySampler)(vk.device, minimapSampler, nullptr);
        const auto destroyTexture = [&](const CityTexture& texture) {
            if (texture.view) VK_CALL(vkDestroyImageView)(vk.device, texture.view, nullptr);
            if (texture.image) VK_CALL(vkDestroyImage)(vk.device, texture.image, nullptr);
            if (texture.memory) VK_CALL(vkFreeMemory)(vk.device, texture.memory, nullptr);
        };
        for (const auto& entry : textures) destroyTexture(entry.second);
        for (const auto& caption : captions) destroyTexture(caption);
        for (const auto& entry : portraits) destroyTexture(entry.second);
        for (const auto& frame : failureTextures) destroyTexture(frame);
        for (const auto& title : missionTitles) {
            for (const auto& frame : title.textures) destroyTexture(frame);
            destroyTexture(title.label);
        }
        destroyTexture(payphoneObjective);
        destroyTexture(nextVehicleObjective);
        destroyTexture(hummerTutorial);
        destroyTexture(weaponShopObjective);
        for (const auto& texture : returnToLcObjectives) destroyTexture(texture);
        destroyTexture(postLcDriveObjective);
        destroyTexture(postLcAmbushObjective);
        destroyTexture(postLcConfrontationObjective);
        destroyTexture(postLcFightObjective);
        destroyTexture(locoNextObjective);
        destroyTexture(locoCooperObjective);
        destroyTexture(cooperNextDriveObjective);
        destroyTexture(cooperNextFightObjective);
        destroyTexture(brawlNextObjective);
        destroyTexture(postBasketballObjective);
        destroyTexture(postBasketballAmbushObjective);
        destroyTexture(repairDriveObjective);
        destroyTexture(repairTutorial);
        destroyTexture(repairPaymentPrompt);
        destroyTexture(deliveryObjective);
        destroyTexture(elToroObjective);
        destroyTexture(elToroFightObjective);
        destroyTexture(elToroDriveObjective);
        destroyTexture(vanMissionObjective);
        destroyTexture(vanDriveObjective);
        destroyTexture(vanProtectObjective);
        destroyTexture(basketballFightObjective);
        destroyTexture(basketballTutorial);
        destroyTexture(weaponShopTutorial);
        for (const auto& texture : weaponShopInventory) destroyTexture(texture);
        for (const auto& texture : weaponShopConfirmation) destroyTexture(texture);
        destroyTexture(weaponShopInsufficientCash);
        for (const auto& gauge : statusGauges)
            for (const auto& texture : gauge.layers) destroyTexture(texture);
        for (const auto& texture : spyLayers) destroyTexture(texture);
        for (const auto& texture : cashGlyphs) destroyTexture(texture);
        destroyTexture(failureTitle);
        for (const auto& texture : missionCompleteObjectives) destroyTexture(texture);
        for (const auto& texture : missionRewardCash) destroyTexture(texture);
        for (const auto& animation : missionObjectiveAnimations)
            for (const auto& frame : animation.textures) destroyTexture(frame);
        for (const auto& frame : missionRewardFrames) destroyTexture(frame);
        destroyTexture(failurePrompt);
        destroyTexture(cinematicBars);
        destroyTexture(cinematicSkip);
        for (const auto& layer : minimapTextures) destroyTexture(layer);
        for (const auto& layer : minimapCheckpointTextures) destroyTexture(layer);
        for (auto frame : frames) VK_CALL(vkDestroyFramebuffer)(vk.device, frame, nullptr);
        for (auto view : colorViews) VK_CALL(vkDestroyImageView)(vk.device, view, nullptr);
        if (renderPass) VK_CALL(vkDestroyRenderPass)(vk.device, renderPass, nullptr);
        if (depthView) VK_CALL(vkDestroyImageView)(vk.device, depthView, nullptr);
        if (depth) VK_CALL(vkDestroyImage)(vk.device, depth, nullptr);
        if (depthMemory) VK_CALL(vkFreeMemory)(vk.device, depthMemory, nullptr);
        if (geometry) VK_CALL(vkDestroyBuffer)(vk.device, geometry, nullptr);
        if (geometryMemory) VK_CALL(vkFreeMemory)(vk.device, geometryMemory, nullptr);
    }

    Vulkan& context;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline captionPipeline = VK_NULL_HANDLE;
    VkPipeline checkpointPipeline = VK_NULL_HANDLE;
    VkPipeline explosionPipeline = VK_NULL_HANDLE;
    VkPipeline vehicleExteriorPipeline = VK_NULL_HANDLE;
    VkImage depth = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkBuffer geometry = VK_NULL_HANDLE;
    VkDeviceMemory geometryMemory = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t captionVertex = 0;
    uint32_t weaponShopInventoryVertex = 0;
    uint32_t dialogueCaptionVertex = 0;
    uint32_t cinematicBarsVertex = 0;
    uint32_t cinematicSkipVertex = 0;
    uint32_t failureTitleVertex = 0;
    uint32_t failurePromptVertex = 0;
    uint32_t routeArrowVertex = 0;
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler districtSampler = VK_NULL_HANDLE;
    VkSampler minimapSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;
    VkDescriptorPool texturePool = VK_NULL_HANDLE;
    std::map<std::string, CityTexture> textures;
    std::vector<CityTexture> captions;
    std::map<int32_t, CityTexture> portraits;
    std::vector<CityTexture> failureTextures;
    struct HudAnimation {
        std::vector<CityTexture> textures;
        std::vector<uint32_t> frameEnds;
    };
    struct MissionTitleRender : HudAnimation {
        CityTexture label;
    };
    std::array<MissionTitleRender, 9> missionTitles;
    size_t missionTitleIndex = 0;
    std::optional<double> nextMissionFadeMs;
    CityTexture payphoneObjective;
    CityTexture nextVehicleObjective;
    CityTexture hummerTutorial;
    CityTexture weaponShopObjective;
    CityTexture postLcDriveObjective;
    CityTexture postLcAmbushObjective, postLcConfrontationObjective, postLcFightObjective;
    bool postLcIntroFinished = false;
    bool postLcAmbushFinished = false;
    std::optional<RetailScriptExecution> postLcWalkScript;
    std::optional<RetailScriptExecution> postLcFightScript;
    CityTexture locoNextObjective, locoCooperObjective, cooperNextDriveObjective, cooperNextFightObjective;
    std::optional<RetailScriptExecution> taggedFightScript;
    const CityMissionCinematic* taggedFightNextScene = nullptr;
    CityTexture brawlNextObjective, postBasketballObjective, postBasketballAmbushObjective;
    std::optional<RetailScriptExecution> postBasketballEscapeScript, repairDriveScript, deliveryScript;
    CityTexture repairDriveObjective, repairTutorial, deliveryObjective, elToroObjective, elToroFightObjective;
    std::optional<RetailScriptExecution> elToroDestinationScript, elToroDriveScript;
    CityTexture elToroDriveObjective, vanMissionObjective, vanDriveObjective, vanProtectObjective;
    std::optional<RetailScriptExecution> vanProtectionScript, vanHandshakeScript;
    double vanProtectionMs = 0;
    std::optional<RetailScriptExecution> vanAcquisitionScript, vanDriveScript, vanReentryScript;
    std::optional<double> vanDriveMarker;
    bool vanSecondDrive = false;
    std::optional<double> elToroDestinationMarker, elToroDriveMarker;
    bool repairTutorialVisible = false;
    enum class RepairShop { inactive, approach, entrance, confirm, repairing };
    RepairShop repairShop = RepairShop::inactive;
    CityTexture repairPaymentPrompt;
    int32_t repairQuote = 0;
    double repairElapsedMs = 0;
    CityTexture basketballFightObjective, basketballTutorial;
    std::optional<RetailScriptExecution> basketballRouteScript, postBasketballRouteScript, postBasketballVehicleScript;
    std::optional<double> basketballMarker, postBasketballMarker, repairDriveMarker, repairShopMarker, deliveryMarker;
    bool basketballTutorialVisible = false;
    bool locoCooperPending = false;
    std::optional<CityVehiclePose> locoCooper;
    std::optional<RetailScriptExecution> locoCooperScript;
    bool cooperRemovalPending = false;
    bool hummerRemovalPending = false;
    bool hummerRemovalReady = false;
    std::optional<CityVehicleGeometry> cooperGeometry;
    std::optional<CityTerrain::VehicleFootprint> cooperFootprint;
    std::optional<size_t> cooperActor;
    std::vector<CityVertex> cooperVertices;
    std::optional<RetailScriptExecution> locoGuardScript;
    struct WalkingMarker { size_t index; double startMs; };
    std::optional<WalkingMarker> postLcWalkMarker;
    std::optional<double> postLcTailMs;
    std::optional<float> spyGauge;
    std::optional<double> hummerArrivalFadeMs;
    std::array<CityTexture, 2> returnToLcObjectives;
    CityTexture weaponShopTutorial;
    std::array<CityTexture, 2> weaponShopInventory;
    std::array<CityTexture, 2> weaponShopConfirmation;
    CityTexture weaponShopInsufficientCash;
    struct StatusGauge {
        std::array<CityTexture, 3> layers;
        std::array<uint32_t, 3> vertices{};
        uint32_t fillWidth = 0;
    };
    std::array<StatusGauge, statusGaugeCount> statusGauges;
    std::array<CityTexture, 4> spyLayers;
    std::array<uint32_t, 4> spyVertices{};
    uint32_t spyWidth = 0;
    RetailHealthBlink healthBlink;
    std::array<CityTexture, retailCashCharacters.size()> cashGlyphs;
    std::array<uint32_t, retailCashCharacters.size()> cashGlyphVertices{}, cashGlyphWidths{};
    RetailLanguage cashLanguage = RetailLanguage::english;
    int32_t displayedCash = -1;
    std::string cashLabel;
    uint32_t missionTitleLabelVertex = 0;
    std::optional<double> missionTitleMs;
    std::optional<double> missionRevealMs;
    std::array<CityTexture, 8> missionCompleteObjectives;
    std::array<HudAnimation, 2> missionObjectiveAnimations;
    uint32_t missionCompleteObjectiveVertex = 0;
    std::vector<CityTexture> missionRewardFrames;
    std::vector<uint32_t> missionRewardFrameEnds;
    std::optional<double> missionRewardMs;
    std::array<CityTexture, 8> missionRewardCash;
    size_t missionRewardIndex = 0;
    static constexpr double missionCompleteMessageMs = 3000;
    double hudAnimationMs(const std::vector<uint32_t>& frameEnds) const
    {
        return frameEnds.empty() ? 0. : frameEnds.back() * 2000. / retailFrameRate;
    }
    double missionObjectiveHoldEndMs() const
    {
        return hudAnimationMs(missionObjectiveAnimations[0].frameEnds) + missionCompleteMessageMs;
    }
    double missionObjectiveEndMs() const
    {
        return missionObjectiveHoldEndMs() + hudAnimationMs(missionObjectiveAnimations[1].frameEnds);
    }
    double missionCashRewardEndMs() const
    {
        return hudAnimationMs(missionRewardFrameEnds) + missionCompleteMessageMs;
    }
    CityTexture failureTitle;
    CityTexture failurePrompt;
    std::vector<uint32_t> failureFrameEnds;
    int32_t retailFrameRate = 30;
    bool restoreVehicleGeometry = false;
    bool failureAnimationFinished(double elapsedMs) const
    {
        return failureFrameEnds.empty() || elapsedMs * retailFrameRate / 2000 + 0.5 >= failureFrameEnds.back();
    }
    CityTexture cinematicBars;
    CityTexture cinematicSkip;
    bool skipArmed = false;
    std::array<CityTexture, 3> minimapTextures;
    std::array<uint32_t, 3> minimapVertices{};
    std::vector<CityTexture> minimapCheckpointTextures;
    std::vector<uint32_t> minimapCheckpointVertices;
    std::vector<RetailMinimapStep> minimapCheckpointAnimation;
    enum class MinimapTarget { walkingCheckpoint, vehicle, payphone, fightActor, nextMission, nextVehicle, hummerRoute, postLcWalk, basketball, postBasketball, repairDrive, repairShop, delivery, elToroDestination, elToroDrive, vanDrive };
    std::optional<MinimapTarget> lastMinimapTarget;
    bool trackOpeningVehicle = false;
    double minimapTargetStartMs = 0;
    uint32_t minimapWidth = 0, minimapHeight = 0;
    float minimapHalfWidth = 0, minimapHalfHeight = 0;
    std::array<float, 2> minimapMarkerLimits{};
    std::vector<CitySurface> surfaces;
    size_t lowDetailFirstSurface = 0;
    size_t lowDetailEndSurface = 0;
    size_t distantDraws = 0;
    std::optional<RetailAchievementState> achievements;
    std::optional<size_t> checkpointFirstSurface;
    std::optional<size_t> payphoneFirstSurface;
    std::optional<size_t> vehicleObjectiveFirstSurface;
    std::optional<size_t> nextMissionFirstSurface;
    std::optional<size_t> hummerRouteFirstSurface;
    std::optional<size_t> weaponShopDriveFirstSurface;
    std::optional<size_t> weaponShopApproachFirstSurface;
    std::optional<size_t> weaponShopEntranceFirstSurface;
    std::optional<size_t> returnToLcFirstSurface;
    std::optional<size_t> postLcDriveFirstSurface, cooperNextDriveFirstSurface, basketballFirstSurface, postBasketballFirstSurface, repairDriveFirstSurface, repairShopFirstSurface, deliveryFirstSurface, elToroFirstSurface, elToroDestinationFirstSurface, elToroDriveFirstSurface, vanDriveFirstSurface, vanSecondDriveFirstSurface;
    std::array<std::optional<size_t>, 3> postLcWalkFirstSurfaces;
    std::vector<CitySkinRange> skins;
    std::vector<CityVertex> vertices;
    std::vector<CityVertex> vehicleVertices;
    std::vector<CityVehicleLightMesh> vehicleLights;
    std::vector<CityBillboard> billboards;
    std::array<CityVehicleEffect, 3> vehicleEffects;
    std::vector<CityWheelRange> wheels;
    std::vector<CityVehicleMorphRange> vehicleMorphs;
    std::optional<CityVehicleGeometry> nextVehicleGeometry, postBasketballVehicleGeometry, vanGeometry;
    std::optional<CityTerrain::VehicleFootprint> vanFootprint;
    std::optional<CityTerrain::VehicleFootprint> nextVehicleFootprint, postBasketballVehicleFootprint;
    std::array<std::vector<CityCheckpointMesh>, 24> checkpointMeshes;
    uint32_t vehicleFirstVertex = 0;
    CityTerrain terrain;
    CityOpening opening{};
    std::optional<CityStartupPlayback> startup;
    std::optional<CityPayphonePlayback> payphoneCinematic;
    std::optional<CityPayphonePlayback> postFightCinematic;
    const CityMissionCinematic* activeMissionCinematic = nullptr;
    std::map<const CityMissionCinematic*, size_t> missionCaptionBases;
    std::optional<RetailScriptExecution> nextMissionScript;
    std::optional<RetailScriptExecution> hummerRouteScript;
    bool hummerTutorialVisible = false;
    std::optional<double> hummerRouteMarker;
    const CityMissionCinematic* pendingHummerCinematic = nullptr;
    bool weaponShopDrive = false;
    bool returningToLc = false;
    std::optional<double> returnToLcFadeMs;
    bool weaponShopApproach = false;
    bool weaponShopUnlocked = false;
    bool weaponShopTutorialVisible = false;
    bool weaponShopOpen = false;
    bool weaponShopConfirmPurchase = false;
    bool weaponShopPurchaseDenied = false;
    std::array<uint32_t, 2> weaponShopServicePrices{};
    size_t weaponShopSelection = 0;
    std::optional<double> nextMissionMarker;
    std::optional<double> payphoneGameplayMs;
    std::optional<double> payphoneRevealMs;
    std::optional<RetailScriptExecution> payphoneLockScript;
    const CityCinematicPlacement* payphoneLockTarget = nullptr;
    const CityCinematicPlacement* payphoneLockMarker = nullptr;
    bool payphoneTargetLocked = false;
    std::unique_ptr<RetailAudio> audio;
    std::unique_ptr<RadioPlayback> radio;
    size_t nextAudio = 0;
    bool fireSoundActive = false;
    std::chrono::steady_clock::time_point lastTick;
    CityPlayback playback;
    std::vector<VkImageView> colorViews;
    std::vector<VkFramebuffer> frames;
};

VkImageView imageView(const Vulkan& vk, VkImage image, VkFormat format, VkImageAspectFlags aspect,
                      uint32_t mipLevels = 1)
{
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {aspect, 0, mipLevels, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    check(VK_CALL(vkCreateImageView)(vk.device, &info, nullptr, &view), "vkCreateImageView");
    return view;
}

void uploadCityTexture(Vulkan& vk, CityTexture& texture, const Image& pixels)
{
    constexpr auto format = VK_FORMAT_B8G8R8A8_UNORM;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {pixels.width, pixels.height, 1};
    image.mipLevels = static_cast<uint32_t>(1 + pixels.mipLevels.size());
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(VK_CALL(vkCreateImage)(vk.device, &image, nullptr, &texture.image), "vkCreateImage (city texture)");
    VkMemoryRequirements memory{};
    VK_CALL(vkGetImageMemoryRequirements)(vk.device, texture.image, &memory);
    texture.memory = allocate(vk, memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(VK_CALL(vkBindImageMemory)(vk.device, texture.image, texture.memory, 0), "vkBindImageMemory (city texture)");
    texture.view = imageView(vk, texture.image, format, VK_IMAGE_ASPECT_COLOR_BIT, image.mipLevels);

    struct Staging {
        Vulkan& context;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        ~Staging() {
            auto& vk = context;
            if (buffer) VK_CALL(vkDestroyBuffer)(vk.device, buffer, nullptr);
            if (memory) VK_CALL(vkFreeMemory)(vk.device, memory, nullptr);
        }
    } staging{vk};
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    std::vector<VkBufferImageCopy> copies;
    for (uint32_t level = 0; level < image.mipLevels; ++level) {
        const auto& source = level ? pixels.mipLevels[level - 1] : pixels;
        VkBufferImageCopy copy{};
        copy.bufferOffset = buffer.size;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        copy.imageExtent = {source.width, source.height, 1};
        copies.push_back(copy);
        buffer.size += source.bgra.size() * sizeof(uint32_t);
    }
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(VK_CALL(vkCreateBuffer)(vk.device, &buffer, nullptr, &staging.buffer), "vkCreateBuffer (city texture)");
    VK_CALL(vkGetBufferMemoryRequirements)(vk.device, staging.buffer, &memory);
    staging.memory = allocate(vk, memory, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(VK_CALL(vkBindBufferMemory)(vk.device, staging.buffer, staging.memory, 0), "vkBindBufferMemory (city texture)");
    void* mapped = nullptr;
    check(VK_CALL(vkMapMemory)(vk.device, staging.memory, 0, buffer.size, 0, &mapped), "vkMapMemory (city texture)");
    for (uint32_t level = 0; level < image.mipLevels; ++level) {
        const auto& source = level ? pixels.mipLevels[level - 1] : pixels;
        std::memcpy(static_cast<char*>(mapped) + copies[level].bufferOffset,
                    source.bgra.data(), source.bgra.size() * sizeof(uint32_t));
    }
    VK_CALL(vkUnmapMemory)(vk.device, staging.memory);

    check(VK_CALL(vkResetCommandPool)(vk.device, vk.pool, 0), "vkResetCommandPool (city texture)");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(VK_CALL(vkBeginCommandBuffer)(vk.command, &begin), "vkBeginCommandBuffer (city texture)");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.mipLevels, 0, 1};
    VK_CALL(vkCmdPipelineBarrier)(vk.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);
    VK_CALL(vkCmdCopyBufferToImage)(vk.command, staging.buffer, texture.image, barrier.newLayout,
                                   image.mipLevels, copies.data());
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = barrier.newLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VK_CALL(vkCmdPipelineBarrier)(vk.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);
    check(VK_CALL(vkEndCommandBuffer)(vk.command), "vkEndCommandBuffer (city texture)");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &vk.command;
    check(VK_CALL(vkQueueSubmit)(vk.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (city texture)");
    check(VK_CALL(vkQueueWaitIdle)(vk.queue), "vkQueueWaitIdle (city texture)");
}

void drawHighlightedCaption(HDC dc, const std::wstring& text, const std::vector<bool>& highlighted,
                             int width, int height, bool manualAdvance)
{
    TEXTMETRICW metrics{};
    if (!GetTextMetricsW(dc, &metrics)) throw std::runtime_error("Could not measure retail caption text.");
    struct Line { size_t first, length; };
    std::vector<Line> lines;
    const int limit = width - 24;
    for (size_t first = 0; first < text.size();) {
        const auto newline = text.find(L'\n', first);
        const auto end = newline == std::wstring::npos ? text.size() : newline;
        if (end == first) {
            lines.push_back({first++, 0});
            continue;
        }
        int fit = 0;
        SIZE extent{};
        if (!GetTextExtentExPointW(dc, text.data() + first, static_cast<int>(end - first), limit,
                                   &fit, nullptr, &extent))
            throw std::runtime_error("Could not wrap retail caption text.");
        size_t stop = first + std::max(1, fit);
        if (stop < end) {
            const auto space = text.rfind(L' ', stop);
            if (space != std::wstring::npos && space > first) stop = space;
        }
        lines.push_back({first, stop - first});
        first = stop;
        while (first < end && text[first] == L' ') ++first;
        if (first == newline) ++first;
    }
    const auto available = height - (manualAdvance ? 10 : 0);
    const auto lineHeight = static_cast<int>(metrics.tmHeight);
    int y = std::max(0, (available - static_cast<int>(lines.size()) * lineHeight) / 2);
    for (const auto& line : lines) {
        if (line.length) {
            SIZE extent{};
            std::vector<int> advances(line.length);
            if (!GetTextExtentExPointW(dc, text.data() + line.first, static_cast<int>(line.length),
                                       width, nullptr, advances.data(), &extent))
                throw std::runtime_error("Could not position retail caption text.");
            const int left = (width - extent.cx) / 2;
            size_t start = 0;
            while (start < line.length) {
                const bool yellow = highlighted[line.first + start];
                size_t end = start + 1;
                while (end < line.length && highlighted[line.first + end] == yellow) ++end;
                SetTextColor(dc, yellow ? RGB(255, 255, 51) : RGB(255, 255, 255));
                TextOutW(dc, left + (start ? advances[start - 1] : 0), y,
                         text.data() + line.first + start, static_cast<int>(end - start));
                start = end;
            }
        }
        y += lineHeight;
    }
}

Image rasterCaption(const CityDialogue& dialogue, bool tutorial = false, bool title = false,
                    int width = 432, int height = 68, const wchar_t* retailFace = nullptr,
                    bool transparent = false)
{
    const auto line = dialogue.speaker.empty() ? dialogue.text : dialogue.speaker + ": " + dialogue.text;
    const auto encodedLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), int(line.size()), nullptr, 0);
    if (!encodedLength) throw std::runtime_error("Invalid retail dialogue encoding.");
    std::wstring encoded(encodedLength, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), int(line.size()), encoded.data(), encodedLength);
    std::wstring text;
    std::vector<bool> highlighted;
    bool yellow = false;
    for (size_t i = 0; i < encoded.size(); ++i) {
        auto code = encoded[i];
        if (code == L'\\' && i + 1 < encoded.size()) {
            const auto escape = encoded[i + 1];
            if (escape == L'0' || escape == L'1') {
                yellow = escape == L'0';
                ++i;
                continue;
            }
            if (escape == L'\\' || escape == L'n') {
                code = escape == L'n' ? L'\n' : L'\\';
                ++i;
            }
        }
        text.push_back(code);
        highlighted.push_back(yellow);
    }
    const auto length = static_cast<int>(text.size());
    Image image{static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                std::vector<uint32_t>(static_cast<size_t>(width) * height)};
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    struct Raster {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = nullptr;
        HFONT font = nullptr;
        ~Raster() {
            if (dc) DeleteDC(dc);
            if (bitmap) DeleteObject(bitmap);
            if (font) DeleteObject(font);
        }
    } raster;
    void* pixels = nullptr;
    raster.bitmap = CreateDIBSection(raster.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    raster.font = CreateFontW(title ? -28 : -16, 0, 0, 0, retailFace ? FW_NORMAL : FW_SEMIBOLD,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH, retailFace ? retailFace : L"Segoe UI");
    if (!raster.dc || !raster.bitmap || !raster.font)
        throw std::runtime_error("Could not allocate the retail dialogue caption.");
    SelectObject(raster.dc, raster.bitmap);
    SelectObject(raster.dc, raster.font);
    if (retailFace) {
        wchar_t selected[LF_FACESIZE]{};
        auto visibleText = text;
        visibleText.erase(std::remove_if(visibleText.begin(), visibleText.end(), [](wchar_t code) {
            return code == L'\n' || code == L'\r' || code == L'\t';
        }), visibleText.end());
        std::vector<WORD> glyphs(visibleText.size());
        if (!GetTextFaceW(raster.dc, LF_FACESIZE, selected) || _wcsicmp(selected, retailFace) != 0 ||
            (!visibleText.empty() && GetGlyphIndicesW(raster.dc, visibleText.data(), static_cast<int>(visibleText.size()),
                glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) ||
            std::find(glyphs.begin(), glyphs.end(), 0xffff) != glyphs.end())
            throw std::runtime_error("Could not select all retail caption glyphs.");
    }
    std::memset(pixels, 0, image.bgra.size() * sizeof(uint32_t));
    SetTextColor(raster.dc, RGB(255, 255, 255));
    SetBkMode(raster.dc, TRANSPARENT);
    RECT bounds{12, 0, width - 12, height};
    const auto flags = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
    if (std::find(highlighted.begin(), highlighted.end(), true) != highlighted.end()) {
        drawHighlightedCaption(raster.dc, text, highlighted, width, height, !dialogue.durationMs && !tutorial);
    } else {
        const auto textHeight = DrawTextW(raster.dc, text.data(), length, &bounds, flags | DT_CALCRECT);
        bounds = {12, std::max(0, (height - textHeight) / 2), width - 12, height};
        DrawTextW(raster.dc, text.data(), length, &bounds, flags);
    }
    SetTextColor(raster.dc, RGB(255, 255, 255));
    if (!dialogue.durationMs && !tutorial) {
        RECT prompt{width - 20, height - 20, width - 4, height};
        DrawTextW(raster.dc, L"\x25bc", 1, &prompt, DT_RIGHT | DT_SINGLELINE);
    }
    GdiFlush();
    const auto* textPixels = static_cast<const uint32_t*>(pixels);
    for (size_t i = 0; i < image.bgra.size(); ++i) {
        const auto level = (textPixels[i] >> 16) & 255;
        const auto alpha = title || transparent ? level : 176u + level * 79u / 255u;
        auto pixel = alpha << 24;
        if (alpha)
            for (unsigned channel = 0; channel < 24; channel += 8)
                pixel |= (((textPixels[i] >> channel) & 255u) * 255u / alpha) << channel;
        image.bgra[i] = pixel;
    }
    return image;
}

void prepareVehicleEffects(CityRender& city, FileSystemBase& assets, CityDrawData& draw)
{
    if (!city.opening.vehicleActor) return;
    constexpr std::array<const char*, 3> paths{"p_smoke_00.bdae", "p_fire_00.bdae", "p_explosion_00.bdae"};
    for (size_t i = 0; i < paths.size(); ++i) {
        CityScene scene;
        try {
            scene = loadCityScene(assets, paths[i]);
        } catch (const std::runtime_error& error) {
            std::cerr << error.what() << '\n';
            continue;
        }
        auto texture = cityParticleTexture(scene);
        if (scene.particleEmitters.size() != 1 || texture.empty()) {
            std::cerr << "Could not bind particle scene " << paths[i] << '\n';
            continue;
        }
        auto& emitter = scene.particleEmitters.front();
        const auto node = std::find_if(scene.nodes.begin(), scene.nodes.end(), [&](const CityNode& entry) {
            return std::find(entry.particleEmitters.begin(), entry.particleEmitters.end(), emitter.name) !=
                   entry.particleEmitters.end();
        });
        if (node == scene.nodes.end()) {
            std::cerr << "Could not locate emitter node in " << paths[i] << '\n';
            continue;
        }
        const auto track = emitter.tracks.find(CityParticleParameter::birthRate);
        if (!emitter.maxParticles || emitter.type > 2 || track == emitter.tracks.end() ||
            track->second.empty() || !track->second.back().timeMs ||
            emitter.maxParticles > (UINT32_MAX - draw.vertices.size()) / 6)
            throw std::runtime_error(std::string("Invalid city particle definition ") + paths[i]);
        auto& effect = city.vehicleEffects[i];
        effect.clip = readCityParticleClip(scene);
        effect.node = static_cast<size_t>(node - scene.nodes.begin());
        effect.texture = std::move(texture);
        effect.firstVertex = static_cast<uint32_t>(draw.vertices.size());
        draw.vertices.resize(draw.vertices.size() + size_t(emitter.maxParticles) * 6);
        effect.scene = std::move(scene);
    }
}

void updateVehicleEffects(CityRender& city, double deltaMs)
{
    if (!city.playback.vehicle) return;
    const auto& vehicle = *city.playback.vehicle;
    const auto& actor = city.opening.actors[*city.opening.vehicleActor];
    const auto damage = vehicle.damageEffect();
    const auto anchor = cityVehicleSmokeFrame(actor, vehicle);
    const std::array active{anchor.has_value() && damage != CityVehiclePose::DamageEffect::none,
                            anchor.has_value() && damage == CityVehiclePose::DamageEffect::fire, vehicle.exploded};
    const auto ms = static_cast<float>(std::clamp(deltaMs, 0., 50.));
    for (size_t i = 0; i < active.size(); ++i) {
        auto& effect = city.vehicleEffects[i];
        if (effect.texture.empty()) continue;
        const bool burst = i == 2;
        if (active[i]) effect.elapsedMs += ms;
        const auto duration = static_cast<float>(effect.clip.durationMs);
        if (!burst && effect.elapsedMs >= duration) effect.elapsedMs = std::fmod(effect.elapsedMs, duration);
        const auto animation = std::min(effect.elapsedMs, duration);
        const bool emit = active[i] && (!burst || animation < duration);
        const CityParticleFrame vehicleRoot{vehicle.position};
        const auto frame = cityParticleFrame(effect.scene, effect.node, effect.clip, animation,
                                             burst ? vehicleRoot : anchor.value_or(vehicleRoot));
        effect.simulation.advance(effect.scene.particleEmitters.front(), ms * 0.001f, animation, frame, emit);
        if (!active[i]) effect.elapsedMs = 0;
    }
}

void prepareCityTextures(CityRender& city, FileSystemBase& assets, const CityDrawData& draw,
                         const RetailMinimapAssets* minimap, const RetailStatusHud* statusHud,
                         RetailLanguage language)
{
    auto& vk = city.context;
    for (const auto& surface : draw.surfaces) city.textures.try_emplace(surface.texture);
    for (const auto& effect : city.vehicleEffects)
        if (!effect.texture.empty()) city.textures.try_emplace(effect.texture);
    if (city.opening.payphone) city.textures.try_emplace("");
    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    check(VK_CALL(vkCreateDescriptorSetLayout)(vk.device, &layout, nullptr, &city.textureLayout),
          "vkCreateDescriptorSetLayout (city texture)");
    const auto& checkpoint = city.opening.checkpoint;
    const auto tutorialCount = (checkpoint ? 2 : 0) + (city.opening.payphone ? 2 : 0);
    const auto* payphoneLines = city.opening.payphone ? &city.opening.payphone->cinematicDialogue : nullptr;
    const auto payphoneCount = payphoneLines ? payphoneLines->size() : 0;
    const auto* postFightLines = city.opening.payphone ? &city.opening.payphone->postFight.dialogue : nullptr;
    const auto postFightCount = postFightLines ? postFightLines->size() : 0;
    const auto* missionLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->cinematic.dialogue : nullptr;
    const auto missionLineCount = missionLines ? missionLines->size() : 0;
    const auto* pierLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->pierCinematic.dialogue : nullptr;
    const auto pierLineCount = pierLines ? pierLines->size() : 0;
    const auto* shopLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->weaponShopCinematic.dialogue : nullptr;
    const auto shopLineCount = shopLines ? shopLines->size() : 0;
    const auto* closingLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->lcClosingCinematic.dialogue : nullptr;
    const auto closingLineCount = closingLines ? closingLines->size() : 0;
    const auto* postLcLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postLcIntro.dialogue : nullptr;
    const auto postLcLineCount = postLcLines ? postLcLines->size() : 0;
    const auto* ambushLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postLcAmbush.dialogue : nullptr;
    const auto ambushLineCount = ambushLines ? ambushLines->size() : 0;
    const auto* confrontationLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postLcConfrontation.dialogue : nullptr;
    const auto confrontationLineCount = confrontationLines ? confrontationLines->size() : 0;
    const auto* phoneLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postLcPhone.dialogue : nullptr;
    const auto phoneLineCount = phoneLines ? phoneLines->size() : 0;
    const auto* fightIntroLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postLcFightIntro.dialogue : nullptr;
    const auto fightIntroLineCount = fightIntroLines ? fightIntroLines->size() : 0;
    const auto* locoNextLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->locoNextIntro.dialogue : nullptr;
    const auto locoNextLineCount = locoNextLines ? locoNextLines->size() : 0;
    const auto* cooperLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->locoCooperIntro.dialogue : nullptr;
    const auto cooperLineCount = cooperLines ? cooperLines->size() : 0;
    const auto* cooperNextLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->cooperNextIntro.dialogue : nullptr;
    const auto cooperNextLineCount = cooperNextLines ? cooperNextLines->size() : 0;
    const auto* cooperArrivalLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->cooperNextArrival.dialogue : nullptr;
    const auto cooperArrivalLineCount = cooperArrivalLines ? cooperArrivalLines->size() : 0;
    const auto* cooperClosingLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->cooperNextClosing.dialogue : nullptr;
    const auto cooperClosingLineCount = cooperClosingLines ? cooperClosingLines->size() : 0;
    const auto* brawlNextLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->brawlNextIntro.dialogue : nullptr;
    const auto brawlNextLineCount = brawlNextLines ? brawlNextLines->size() : 0;
    const auto* basketballLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->basketballArrival.dialogue : nullptr;
    const auto basketballLineCount = basketballLines ? basketballLines->size() : 0;
    const auto* basketballClosingLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->basketballClosing.dialogue : nullptr;
    const auto basketballClosingLineCount = basketballClosingLines ? basketballClosingLines->size() : 0;
    const auto* postBasketballLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postBasketballIntro.dialogue : nullptr;
    const auto postBasketballLineCount = postBasketballLines ? postBasketballLines->size() : 0;
    const auto* ambush29Lines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postBasketballAmbush.dialogue : nullptr;
    const auto ambush29LineCount = ambush29Lines ? ambush29Lines->size() : 0;
    const auto* briefing29Lines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->postBasketballBriefing.dialogue : nullptr;
    const auto briefing29LineCount = briefing29Lines ? briefing29Lines->size() : 0;
    const auto* repairArrivalLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->repairArrival.dialogue : nullptr;
    const auto repairArrivalLineCount = repairArrivalLines ? repairArrivalLines->size() : 0;
    const auto* deliveryLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->deliveryClosing.dialogue : nullptr;
    const auto deliveryLineCount = deliveryLines ? deliveryLines->size() : 0;
    const auto* elToroLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->elToroIntro.dialogue : nullptr;
    const auto elToroLineCount = elToroLines ? elToroLines->size() : 0;
    const auto* elToroArrivalLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->elToroArrival.dialogue : nullptr;
    const auto elToroArrivalLineCount = elToroArrivalLines ? elToroArrivalLines->size() : 0;
    const auto* elToroInterludeLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->elToroInterlude.dialogue : nullptr;
    const auto elToroInterludeLineCount = elToroInterludeLines ? elToroInterludeLines->size() : 0;
    const auto* elToroDepartureLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->elToroDeparture.dialogue : nullptr;
    const auto elToroDepartureLineCount = elToroDepartureLines ? elToroDepartureLines->size() : 0;
    const auto* elToroClosingLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->elToroClosing.dialogue : nullptr;
    const auto elToroClosingLineCount = elToroClosingLines ? elToroClosingLines->size() : 0;
    const auto* vanIntroLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanMissionIntro.dialogue : nullptr;
    const auto vanIntroLineCount = vanIntroLines ? vanIntroLines->size() : 0;
    const auto* vanBriefingLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanBriefing.dialogue : nullptr;
    const auto vanBriefingLineCount = vanBriefingLines ? vanBriefingLines->size() : 0;
    const auto* vanArrivalLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanArrival.dialogue : nullptr;
    const auto vanArrivalLineCount = vanArrivalLines ? vanArrivalLines->size() : 0;
    const auto* vanReentryLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanReentryBriefing.dialogue : nullptr;
    const auto vanReentryLineCount = vanReentryLines ? vanReentryLines->size() : 0;
    const auto* vanSecondArrivalLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanSecondArrival.dialogue : nullptr;
    const auto vanSecondArrivalLineCount = vanSecondArrivalLines ? vanSecondArrivalLines->size() : 0;
    const auto* vanHandshakeLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanHandshake.dialogue : nullptr;
    const auto vanHandshakeLineCount = vanHandshakeLines ? vanHandshakeLines->size() : 0;
    const auto* vanHandshakeBriefingLines = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->vanHandshakeBriefing.dialogue : nullptr;
    const auto vanHandshakeBriefingLineCount = vanHandshakeBriefingLines ? vanHandshakeBriefingLines->size() : 0;
    for (const auto* lines : {cooperClosingLines, brawlNextLines, basketballLines, basketballClosingLines,
                             postBasketballLines, ambush29Lines, briefing29Lines, repairArrivalLines, deliveryLines, elToroLines, elToroArrivalLines, elToroInterludeLines, elToroDepartureLines, elToroClosingLines, vanIntroLines, vanBriefingLines, vanArrivalLines, vanReentryLines, vanSecondArrivalLines, vanHandshakeLines,
                             vanHandshakeBriefingLines})
        if (lines)
            for (const auto& dialogue : *lines)
                if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (cooperArrivalLines)
        for (const auto& dialogue : *cooperArrivalLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (cooperNextLines)
        for (const auto& dialogue : *cooperNextLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (cooperLines)
        for (const auto& dialogue : *cooperLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (locoNextLines)
        for (const auto& dialogue : *locoNextLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (fightIntroLines)
        for (const auto& dialogue : *fightIntroLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (phoneLines)
        for (const auto& dialogue : *phoneLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (confrontationLines)
        for (const auto& dialogue : *confrontationLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    const bool hasPayphoneObjective = city.opening.payphone && !city.opening.payphone->gameplayObjective.empty();
    const bool hasBars = city.startup || !city.opening.dialogue.empty();
    for (const auto& dialogue : city.opening.dialogue)
        if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (payphoneLines)
        for (const auto& dialogue : *payphoneLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (postFightLines)
        for (const auto& dialogue : *postFightLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (missionLines)
        for (const auto& dialogue : *missionLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (pierLines)
        for (const auto& dialogue : *pierLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (shopLines)
        for (const auto& dialogue : *shopLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (closingLines)
        for (const auto& dialogue : *closingLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (postLcLines)
        for (const auto& dialogue : *postLcLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    if (ambushLines)
        for (const auto& dialogue : *ambushLines)
            if (dialogue.portrait >= 0) city.portraits.try_emplace(dialogue.portrait);
    std::vector<RetailSpriteRasterStep> failure;
    const auto* next = city.opening.payphone && city.opening.payphone->postFight.nextCheckpoint
        ? &city.opening.payphone->postFight.nextCheckpoint->title : nullptr;
    const auto* nextMission = next ? &*city.opening.payphone->postFight.nextCheckpoint : nullptr;
    const std::array<const CityMissionTitle*, 9> titles{
        city.opening.missionTitle ? &*city.opening.missionTitle : nullptr, next,
        nextMission && nextMission->postLcTitle ? &*nextMission->postLcTitle : nullptr,
        nextMission && nextMission->locoNextTitle ? &*nextMission->locoNextTitle : nullptr,
        nextMission && nextMission->cooperNextTitle ? &*nextMission->cooperNextTitle : nullptr,
        nextMission && nextMission->brawlNextTitle ? &*nextMission->brawlNextTitle : nullptr,
        nextMission && nextMission->postBasketballTitle ? &*nextMission->postBasketballTitle : nullptr,
        nextMission && nextMission->elToroTitle ? &*nextMission->elToroTitle : nullptr,
        nextMission && nextMission->vanMissionTitle ? &*nextMission->vanMissionTitle : nullptr};
    std::array<std::vector<RetailSpriteRasterStep>, titles.size()> titleFrames;
    size_t titleTextures = 0;
    for (size_t i = 0; i < titles.size(); ++i) {
        const auto* title = titles[i];
        if (!title) continue;
        const std::unique_ptr<IFileStream> stream(assets.openResource(title->sprite.c_str()));
        if (!stream) throw std::runtime_error("Missing retail mission title sprite.");
        const auto sprite = readRetailSprite(*stream);
        titleFrames[i] = rasterRetailSpriteAnimation(assets, title->sprite,
            sprite.animations.at(title->animation).name, 568, 320);
        if (titleFrames[i].empty()) throw std::runtime_error("Empty retail mission title animation.");
        titleTextures += titleFrames[i].size() + 1;
    }
    const bool hasFailureTitle = city.opening.vehicleProtection &&
                                 !city.opening.vehicleProtection->failureTitle.empty();
    const bool hasFailurePrompt = city.opening.vehicleProtection &&
                                  !city.opening.vehicleProtection->failurePrompt.empty();
    if (city.opening.vehicleProtection) {
        try {
            failure = rasterRetailSpriteAnimation(assets, "cinematics_hud.bsprite", "FAILED", 480, 320);
        } catch (const std::runtime_error& error) {
            std::cerr << "Could not load retail mission failure animation. " << error.what() << '\n';
        }
    }
    const auto completionLabels = std::count_if(titles.begin(), titles.begin() + city.missionCompleteObjectives.size(), [](const auto* title) {
        return title && !title->completionText.empty();
    });
    const auto cashLabels = std::count_if(titles.begin(), titles.begin() + city.missionRewardCash.size(), [](const auto* title) {
        return title && title->cashReward > 0;
    });
    std::vector<RetailSpriteRasterStep> rewardFrames;
    std::array<std::vector<RetailSpriteRasterStep>, 2> objectiveFrames;
    if (cashLabels)
        rewardFrames = rasterRetailSpriteAnimation(assets, "huds.bsprite", "MISSION_END", 480, 320, true);
    if (completionLabels) {
        objectiveFrames[0] = rasterRetailSpriteAnimation(assets, "huds.bsprite", "OBJECTIVE", 480, 320, true);
        objectiveFrames[1] = rasterRetailSpriteAnimation(assets, "huds.bsprite", "OBJECTIVE_END", 480, 320, true);
    }
    const auto count = static_cast<uint32_t>(city.textures.size() + city.opening.dialogue.size() +
                                             tutorialCount + payphoneCount + postFightCount + missionLineCount + pierLineCount +
                                             shopLineCount + closingLineCount + postLcLineCount + ambushLineCount + confrontationLineCount + phoneLineCount + fightIntroLineCount + locoNextLineCount + cooperLineCount + cooperNextLineCount + cooperArrivalLineCount + cooperClosingLineCount + brawlNextLineCount + basketballLineCount + basketballClosingLineCount + postBasketballLineCount + ambush29LineCount + briefing29LineCount + repairArrivalLineCount + deliveryLineCount + elToroLineCount + elToroArrivalLineCount + elToroInterludeLineCount + elToroDepartureLineCount + elToroClosingLineCount + vanIntroLineCount + vanBriefingLineCount + vanArrivalLineCount + vanReentryLineCount + vanSecondArrivalLineCount + vanHandshakeLineCount + vanHandshakeBriefingLineCount +
                                             hasPayphoneObjective + (next ? 34 : 0) +
                                             (statusHud ? statusHud->gauges.size() * statusHud->gauges.front().layers.size() +
                                                          statusHud->cashGlyphs.size() + statusHud->spyLayers.size() : 0) +
                                             (hasBars ? 1 : 0) + (city.startup ? 1 : 0) +
                                             failure.size() + hasFailureTitle + hasFailurePrompt +
                                             rewardFrames.size() + cashLabels + completionLabels +
                                             objectiveFrames[0].size() + objectiveFrames[1].size() +
                                             titleTextures + city.portraits.size() +
                                             (minimap ? city.minimapTextures.size() + minimap->checkpointAnimation.size() : 0));
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = count;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(VK_CALL(vkCreateDescriptorPool)(vk.device, &pool, nullptr, &city.texturePool), "vkCreateDescriptorPool");

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    check(VK_CALL(vkCreateSampler)(vk.device, &sampler, nullptr, &city.sampler), "vkCreateSampler");
    VkPhysicalDeviceProperties properties{};
    VK_CALL(vkGetPhysicalDeviceProperties)(vk.gpu, &properties);
    // GS3DStuff::loadCityPart sets the bias on each bound texture object.
    sampler.mipLodBias = -std::min(3.f, properties.limits.maxSamplerLodBias);
    check(VK_CALL(vkCreateSampler)(vk.device, &sampler, nullptr, &city.districtSampler),
          "vkCreateSampler (district)");
    sampler.mipLodBias = 0;
    if (minimap) {
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        check(VK_CALL(vkCreateSampler)(vk.device, &sampler, nullptr, &city.minimapSampler), "vkCreateSampler (minimap)");
    }
    const auto upload = [&](CityTexture& texture, const Image& pixels, VkSampler textureSampler = VK_NULL_HANDLE) {
        uploadCityTexture(vk, texture, pixels);
        VkDescriptorSetAllocateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        descriptor.descriptorPool = city.texturePool;
        descriptor.descriptorSetCount = 1;
        descriptor.pSetLayouts = &city.textureLayout;
        check(VK_CALL(vkAllocateDescriptorSets)(vk.device, &descriptor, &texture.descriptor), "vkAllocateDescriptorSets");
        const VkDescriptorImageInfo image{textureSampler ? textureSampler : city.sampler, texture.view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = texture.descriptor;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        VK_CALL(vkUpdateDescriptorSets)(vk.device, 1, &write, 0, nullptr);
    };
    std::set<std::string> districtTextures;
    for (size_t i = 0; i < draw.lowDetailEndSurface; ++i) {
        const auto& path = draw.surfaces[i].texture;
        if (!path.empty()) districtTextures.insert(path);
    }
    for (auto& [path, texture] : city.textures) {
        Image pixels{1, 1, {0xffffffff}};
        if (!path.empty()) {
            const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
            if (file) {
                pixels = readTexture(*file, true);
            } else {
                // The neutral texel lets the shader use the material's diffuse color.
                // The asset audit reports the unresolved texture separately.
                std::cerr << "Missing city texture " << path << ". Using the material diffuse color.\n";
            }
        }
        upload(texture, pixels, districtTextures.count(path) ? city.districtSampler : city.sampler);
    }
    if (statusHud) {
        for (size_t i = 0; i < statusHud->gauges.size(); ++i) {
            const auto& source = statusHud->gauges[i];
            auto& gauge = city.statusGauges[i];
            for (size_t layer = 0; layer < gauge.layers.size(); ++layer)
                upload(gauge.layers[layer], source.layers[layer].image);
            gauge.fillWidth = source.layers[1].image.width;
        }
        for (size_t i = 0; i < statusHud->spyLayers.size(); ++i)
            upload(city.spyLayers[i], statusHud->spyLayers[i].image);
        city.spyWidth = statusHud->spyLayers.front().image.width;
        city.cashLanguage = language;
        for (size_t i = 0; i < statusHud->cashGlyphs.size(); ++i) {
            upload(city.cashGlyphs[i], statusHud->cashGlyphs[i]);
            city.cashGlyphWidths[i] = statusHud->cashGlyphs[i].width;
        }
    }
    const auto dialogues = city.opening.dialogue.size();
    const RetailCaptionFont font(assets, language);
    if (!font.family()) std::cerr << "Could not load the retail caption font. Using Segoe UI.\n";
    for (size_t i = 0; i < city.missionCompleteObjectives.size(); ++i) {
        const auto* title = titles[i];
        if (!title) continue;
        if (!title->completionText.empty())
            upload(city.missionCompleteObjectives[i], rasterCaption({0, 0, {}, title->completionText},
                                                                   true, false, 432, 28, font.family(), true));
        if (title->cashReward > 0)
            upload(city.missionRewardCash[i], rasterRetailMissionCash(assets, title->cashReward, language));
    }
    if (completionLabels) {
        for (size_t i = 0; i < objectiveFrames.size(); ++i) {
            auto& animation = city.missionObjectiveAnimations[i];
            animation.textures.resize(objectiveFrames[i].size());
            uint32_t ticks = 0;
            for (size_t frame = 0; frame < objectiveFrames[i].size(); ++frame) {
                const auto& step = objectiveFrames[i][frame];
                upload(animation.textures[frame], step.image);
                ticks += step.duration;
                animation.frameEnds.push_back(ticks);
            }
        }
    }
    if (!rewardFrames.empty()) {
        city.missionRewardFrames.resize(rewardFrames.size());
        uint32_t ticks = 0;
        for (size_t i = 0; i < rewardFrames.size(); ++i) {
            upload(city.missionRewardFrames[i], rewardFrames[i].image);
            ticks += rewardFrames[i].duration;
            city.missionRewardFrameEnds.push_back(ticks);
        }
    }
    for (size_t titleIndex = 0; titleIndex < titles.size(); ++titleIndex) {
        const auto& frames = titleFrames[titleIndex];
        if (frames.empty()) continue;
        auto& title = city.missionTitles[titleIndex];
        title.textures.resize(frames.size());
        uint32_t ticks = 0;
        for (size_t i = 0; i < frames.size(); ++i) {
            upload(title.textures[i], frames[i].image);
            ticks += frames[i].duration;
            title.frameEnds.push_back(ticks);
        }
        upload(title.label, rasterCaption({0, 0, {}, titles[titleIndex]->text},
                                                    true, true, 432, 68, font.family()));
    }
    city.failureTextures.resize(failure.size());
    uint32_t end = 0;
    for (size_t i = 0; i < failure.size(); ++i) {
        upload(city.failureTextures[i], failure[i].image);
        end += failure[i].duration;
        city.failureFrameEnds.push_back(end);
    }
    if (hasFailureTitle)
        upload(city.failureTitle, rasterCaption({0, 0, {}, city.opening.vehicleProtection->failureTitle}, true, true));
    if (hasFailurePrompt)
        upload(city.failurePrompt, rasterCaption({0, 0, {}, city.opening.vehicleProtection->failurePrompt}, true));
    if (hasBars) {
        std::optional<Image> bars;
        try {
            bars = rasterRetailSpriteFrame(assets, "cinematics_hud.bsprite", "BARS", 480, 320);
        } catch (const std::runtime_error& error) {
            std::cerr << "Could not load retail cinematic bars. " << error.what() << '\n';
        }
        if (bars) upload(city.cinematicBars, *bars);
    }
    if (city.startup) {
        // The 568-wide retail profile places the button artwork at x=478.
        auto image = rasterRetailSpriteFrame(assets, "cinematics_hud.bsprite", "SKIP", skipWidth, skipHeight,
                                             skipWidth - skipArtworkWidth - 478);
        const auto strings = readLocalizedStrings(assets, "menu", language);
        const auto label = rasterCaption({0, 0, {}, strings.at(0x28)}, true, true,
                                         skipWidth - skipArtworkWidth, skipHeight, font.family());
        for (size_t row = 0; row < image.height; ++row)
            std::copy_n(label.bgra.data() + row * label.width, label.width, image.bgra.data() + row * image.width);
        upload(city.cinematicSkip, image);
    }
    city.captions.resize(dialogues + tutorialCount + payphoneCount + postFightCount + missionLineCount +
                         pierLineCount + shopLineCount + closingLineCount + postLcLineCount + ambushLineCount + confrontationLineCount + phoneLineCount + fightIntroLineCount + locoNextLineCount + cooperLineCount + cooperNextLineCount + cooperArrivalLineCount + cooperClosingLineCount + brawlNextLineCount + basketballLineCount + basketballClosingLineCount + postBasketballLineCount + ambush29LineCount + briefing29LineCount + repairArrivalLineCount + deliveryLineCount + elToroLineCount + elToroArrivalLineCount + elToroInterludeLineCount + elToroDepartureLineCount + elToroClosingLineCount + vanIntroLineCount + vanBriefingLineCount + vanArrivalLineCount + vanReentryLineCount + vanSecondArrivalLineCount + vanHandshakeLineCount + vanHandshakeBriefingLineCount);
    for (auto& [portrait, texture] : city.portraits)
        upload(texture, rasterRetailDialoguePortrait(assets, portrait));
    for (size_t i = 0; i < dialogues; ++i) {
        const auto& dialogue = city.opening.dialogue[i];
        upload(city.captions[i], rasterCaption(dialogue, false, false, dialogue.portrait >= 0 ? 352 : 432,
                                               68, font.family()));
    }
    if (checkpoint) {
        upload(city.captions[dialogues], rasterCaption(checkpoint->tutorial, true));
        upload(city.captions[dialogues + 1], rasterCaption(checkpoint->nextTutorial, true));
    }
    if (const auto& route = city.opening.payphone) {
        upload(city.captions[dialogues + 2], rasterCaption(route->destination.tutorial, true));
        upload(city.captions[dialogues + 3], rasterCaption(route->destination.nextTutorial, true));
        if (hasPayphoneObjective)
            upload(city.payphoneObjective,
                   rasterCaption({0, 0, {}, route->gameplayObjective}, true, false, 432, 68, font.family()));
        if (route->postFight.nextCheckpoint) {
            upload(city.nextVehicleObjective,
                   rasterCaption({0, 0, {}, route->postFight.nextCheckpoint->vehicleObjective},
                                 true, false, 432, 68, font.family()));
            upload(city.hummerTutorial, rasterCaption(route->postFight.nextCheckpoint->vehicleTutorial,
                                                       true, false, 432, 68, font.family()));
            upload(city.weaponShopObjective,
                   rasterCaption({0, 0, {}, route->postFight.nextCheckpoint->weaponShopObjective},
                                 true, false, 432, 68, font.family()));
            const auto& next = *route->postFight.nextCheckpoint;
            upload(city.returnToLcObjectives[0],
                   rasterCaption({0, 0, {}, next.returnToHummerObjective}, true, false, 432, 68, font.family()));
            upload(city.returnToLcObjectives[1],
                   rasterCaption({0, 0, {}, next.returnToLcObjective}, true, false, 432, 68, font.family()));
            upload(city.postLcDriveObjective,
                   rasterCaption({0, 0, {}, next.postLcDriveObjective}, true, false, 432, 68, font.family()));
            upload(city.postLcAmbushObjective,
                   rasterCaption({0, 0, {}, next.postLcAmbushObjective}, true, false, 432, 68, font.family()));
            upload(city.postLcConfrontationObjective,
                   rasterCaption({0, 0, {}, next.postLcConfrontationObjective}, true, false, 432, 68, font.family()));
            upload(city.postLcFightObjective,
                   rasterCaption({0, 0, {}, next.postLcFightObjective}, true, false, 432, 68, font.family()));
            upload(city.locoNextObjective,
                   rasterCaption({0, 0, {}, next.locoNextObjective}, true, false, 432, 68, font.family()));
            upload(city.locoCooperObjective,
                   rasterCaption({0, 0, {}, next.locoCooperObjective}, true, false, 432, 68, font.family()));
            upload(city.cooperNextDriveObjective,
                   rasterCaption({0, 0, {}, next.cooperNextDriveObjective}, true, false, 432, 68, font.family()));
            upload(city.cooperNextFightObjective,
                   rasterCaption({0, 0, {}, next.cooperNextFightObjective}, true, false, 432, 68, font.family()));
            upload(city.elToroObjective,
                   rasterCaption({0, 0, {}, next.elToroObjective}, true, false, 432, 68, font.family()));
            upload(city.vanMissionObjective,
                   rasterCaption({0, 0, {}, next.vanMissionObjective}, true, false, 432, 68, font.family()));
            upload(city.vanProtectObjective,
                   rasterCaption({0, 0, {}, next.vanProtectObjective}, true, false, 432, 68, font.family()));
            upload(city.vanDriveObjective,
                   rasterCaption({0, 0, {}, next.vanDriveObjective}, true, false, 432, 68, font.family()));
            upload(city.elToroDriveObjective,
                   rasterCaption({0, 0, {}, next.elToroDriveObjective}, true, false, 432, 68, font.family()));
            upload(city.elToroFightObjective,
                   rasterCaption({0, 0, {}, next.elToroFightObjective}, true, false, 432, 68, font.family()));
            upload(city.deliveryObjective,
                   rasterCaption({0, 0, {}, next.deliveryObjective}, true, false, 432, 68, font.family()));
            upload(city.repairDriveObjective,
                   rasterCaption({0, 0, {}, next.repairDriveObjective}, true, false, 432, 68, font.family()));
            upload(city.repairTutorial, rasterCaption(next.repairTutorial, true, false, 432, 68, font.family()));
            upload(city.repairPaymentPrompt, rasterCaption({0, 0, {}, next.repairPaymentPrompt +
                "\n\n\n\nEnter / A: Confirm    Esc / B: Cancel"}, true, false, 320, 256, font.family()));
            upload(city.postBasketballAmbushObjective,
                   rasterCaption({0, 0, {}, next.postBasketballAmbushObjective}, true, false, 432, 68, font.family()));
            upload(city.postBasketballObjective,
                   rasterCaption({0, 0, {}, next.postBasketballObjective}, true, false, 432, 68, font.family()));
            upload(city.brawlNextObjective,
                   rasterCaption({0, 0, {}, next.brawlNextObjective}, true, false, 432, 68, font.family()));
            upload(city.basketballFightObjective,
                   rasterCaption({0, 0, {}, next.basketballFightObjective}, true, false, 432, 68, font.family()));
            upload(city.basketballTutorial, rasterCaption(next.basketballTutorial, true, false, 432, 68, font.family()));
            auto tutorial = route->postFight.nextCheckpoint->weaponShopTutorial;
            tutorial.text = "Walk to the yellow marker and press E or gamepad A to enter.";
            upload(city.weaponShopTutorial, rasterCaption(tutorial, true, false, 432, 68, font.family()));
            const auto menuLabels = readLocalizedStrings(assets, "menu", language);
            const auto& items = route->postFight.nextCheckpoint->weaponShopInventory;
            constexpr std::array<uint32_t, 2> serviceIndices{2, 1};
            for (size_t selection = 0; selection < serviceIndices.size(); ++selection) {
                const auto service = std::find_if(items.begin(), items.end(), [&](const auto& item) {
                    return item.category == 73 && item.index == serviceIndices[selection];
                });
                if (service == items.end()) throw std::runtime_error("Missing retail shop service.");
                city.weaponShopServicePrices[selection] = service->price;
                auto inventory = menuLabels.at(88) + "\n\n";
                for (const auto& item : items) {
                    inventory += (&item == &*service ? "> " : "") + item.label +
                        "   $" + std::to_string(item.price) + "\n";
                }
                inventory += "\nUp / Down: Select    Enter / A: Buy\nEsc / B: Close";
                upload(city.weaponShopInventory[selection], rasterCaption({0, 0, {}, inventory},
                                                                          true, false, 320, 256, font.family()));
                const auto confirmation = service->label + "   $" + std::to_string(service->price) +
                    "\n\nEnter / A: Confirm    Esc / B: Cancel";
                upload(city.weaponShopConfirmation[selection], rasterCaption(
                    {0, 0, {}, confirmation}, true, false, 320, 256, font.family()));
            }
            upload(city.weaponShopInsufficientCash, rasterCaption(
                {0, 0, {}, "Insufficient cash\n\nEnter / A or Esc / B: Back"}, true, false, 320, 256, font.family()));
        }
        for (size_t i = 0; i < payphoneCount; ++i) {
            const auto& line = (*payphoneLines)[i];
            upload(city.captions[dialogues + tutorialCount + i],
                   rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
        }
        for (size_t i = 0; i < postFightCount; ++i) {
            const auto& line = (*postFightLines)[i];
            upload(city.captions[dialogues + tutorialCount + payphoneCount + i],
                   rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
        }
        for (size_t i = 0; i < missionLineCount; ++i) {
            const auto& line = (*missionLines)[i];
            upload(city.captions[dialogues + tutorialCount + payphoneCount + postFightCount + i],
                   rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
        }
        const auto vanHandshakeBriefingBase = city.captions.size() - vanHandshakeBriefingLineCount;
        const auto vanHandshakeBase = vanHandshakeBriefingBase - vanHandshakeLineCount;
        const auto vanSecondArrivalBase = vanHandshakeBase - vanSecondArrivalLineCount;
        const auto vanReentryBase = vanSecondArrivalBase - vanReentryLineCount;
        const auto vanArrivalBase = vanReentryBase - vanArrivalLineCount;
        const auto vanBriefingBase = vanArrivalBase - vanBriefingLineCount;
        const auto vanIntroBase = vanBriefingBase - vanIntroLineCount;
        const auto elToroClosingBase = vanIntroBase - elToroClosingLineCount;
        const auto elToroDepartureBase = elToroClosingBase - elToroDepartureLineCount;
        const auto elToroInterludeBase = elToroDepartureBase - elToroInterludeLineCount;
        const auto elToroArrivalBase = elToroInterludeBase - elToroArrivalLineCount;
        const auto elToroBase = elToroArrivalBase - elToroLineCount;
        const auto deliveryBase = elToroBase - deliveryLineCount;
        const auto repairArrivalBase = deliveryBase - repairArrivalLineCount;
        const auto briefing29Base = repairArrivalBase - briefing29LineCount;
        const auto ambush29Base = briefing29Base - ambush29LineCount;
        const auto postBasketballBase = ambush29Base - postBasketballLineCount;
        const auto basketballClosingBase = postBasketballBase - basketballClosingLineCount;
        const auto basketballBase = basketballClosingBase - basketballLineCount;
        const auto brawlNextBase = basketballBase - brawlNextLineCount;
        const auto cooperClosingBase = brawlNextBase - cooperClosingLineCount;
        const auto cooperArrivalBase = cooperClosingBase - cooperArrivalLineCount;
        const auto cooperNextBase = cooperArrivalBase - cooperNextLineCount;
        const auto cooperBase = cooperNextBase - cooperLineCount;
        const auto locoNextBase = cooperBase - locoNextLineCount;
        const auto fightIntroBase = locoNextBase - fightIntroLineCount;
        const auto phoneBase = fightIntroBase - phoneLineCount;
        const auto confrontationBase = phoneBase - confrontationLineCount;
        const auto ambushBase = confrontationBase - ambushLineCount;
        const auto postLcBase = ambushBase - postLcLineCount;
        const auto closingBase = postLcBase - closingLineCount;
        const auto shopBase = closingBase - shopLineCount;
        const auto pierBase = shopBase - pierLineCount;
        for (size_t i = 0; i < pierLineCount; ++i) {
            const auto& line = (*pierLines)[i];
            upload(city.captions[pierBase + i],
                   rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
        }
        city.missionCaptionBases.emplace(&route->postFight, dialogues + tutorialCount + payphoneCount);
        if (const auto& nextMission = route->postFight.nextCheckpoint) {
            city.missionCaptionBases.emplace(&nextMission->cinematic,
                dialogues + tutorialCount + payphoneCount + postFightCount);
            city.missionCaptionBases.emplace(&nextMission->pierCinematic, pierBase);
            city.missionCaptionBases.emplace(&nextMission->weaponShopCinematic, shopBase);
            for (size_t i = 0; i < shopLineCount; ++i) {
                const auto& line = (*shopLines)[i];
                upload(city.captions[shopBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->lcClosingCinematic, closingBase);
            for (size_t i = 0; i < closingLineCount; ++i) {
                const auto& line = (*closingLines)[i];
                upload(city.captions[closingBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->postLcIntro, postLcBase);
            for (size_t i = 0; i < postLcLineCount; ++i) {
                const auto& line = (*postLcLines)[i];
                upload(city.captions[postLcBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            const auto uploadSceneCaptions = [&](const CityMissionCinematic& scene, size_t base) {
                city.missionCaptionBases.emplace(&scene, base);
                for (size_t i = 0; i < scene.dialogue.size(); ++i) {
                    const auto& line = scene.dialogue[i];
                    upload(city.captions[base + i],
                           rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
                }
            };
            uploadSceneCaptions(nextMission->basketballArrival, basketballBase);
            uploadSceneCaptions(nextMission->basketballClosing, basketballClosingBase);
            uploadSceneCaptions(nextMission->postBasketballIntro, postBasketballBase);
            uploadSceneCaptions(nextMission->postBasketballAmbush, ambush29Base);
            uploadSceneCaptions(nextMission->postBasketballBriefing, briefing29Base);
            uploadSceneCaptions(nextMission->repairArrival, repairArrivalBase);
            uploadSceneCaptions(nextMission->deliveryClosing, deliveryBase);
            uploadSceneCaptions(nextMission->elToroIntro, elToroBase);
            uploadSceneCaptions(nextMission->elToroArrival, elToroArrivalBase);
            uploadSceneCaptions(nextMission->elToroInterlude, elToroInterludeBase);
            uploadSceneCaptions(nextMission->elToroDeparture, elToroDepartureBase);
            uploadSceneCaptions(nextMission->elToroClosing, elToroClosingBase);
            uploadSceneCaptions(nextMission->vanMissionIntro, vanIntroBase);
            uploadSceneCaptions(nextMission->vanBriefing, vanBriefingBase);
            uploadSceneCaptions(nextMission->vanArrival, vanArrivalBase);
            uploadSceneCaptions(nextMission->vanReentryBriefing, vanReentryBase);
            uploadSceneCaptions(nextMission->vanSecondArrival, vanSecondArrivalBase);
            uploadSceneCaptions(nextMission->vanHandshake, vanHandshakeBase);
            uploadSceneCaptions(nextMission->vanHandshakeBriefing, vanHandshakeBriefingBase);
            uploadSceneCaptions(nextMission->brawlNextIntro, brawlNextBase);
            uploadSceneCaptions(nextMission->cooperNextClosing, cooperClosingBase);
            city.missionCaptionBases.emplace(&nextMission->cooperNextArrival, cooperArrivalBase);
            for (size_t i = 0; i < cooperArrivalLineCount; ++i) {
                const auto& line = (*cooperArrivalLines)[i];
                upload(city.captions[cooperArrivalBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->cooperNextIntro, cooperNextBase);
            for (size_t i = 0; i < cooperNextLineCount; ++i) {
                const auto& line = (*cooperNextLines)[i];
                upload(city.captions[cooperNextBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->locoCooperIntro, cooperBase);
            for (size_t i = 0; i < cooperLineCount; ++i) {
                const auto& line = (*cooperLines)[i];
                upload(city.captions[cooperBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->locoNextIntro, locoNextBase);
            for (size_t i = 0; i < locoNextLineCount; ++i) {
                const auto& line = (*locoNextLines)[i];
                upload(city.captions[locoNextBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->postLcFightIntro, fightIntroBase);
            for (size_t i = 0; i < fightIntroLineCount; ++i) {
                const auto& line = (*fightIntroLines)[i];
                upload(city.captions[fightIntroBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->postLcPhone, phoneBase);
            for (size_t i = 0; i < phoneLineCount; ++i) {
                const auto& line = (*phoneLines)[i];
                upload(city.captions[phoneBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->postLcConfrontation, confrontationBase);
            for (size_t i = 0; i < confrontationLineCount; ++i) {
                const auto& line = (*confrontationLines)[i];
                upload(city.captions[confrontationBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
            city.missionCaptionBases.emplace(&nextMission->postLcAmbush, ambushBase);
            for (size_t i = 0; i < ambushLineCount; ++i) {
                const auto& line = (*ambushLines)[i];
                upload(city.captions[ambushBase + i],
                       rasterCaption(line, false, false, line.portrait >= 0 ? 352 : 432, 68, font.family()));
            }
        }
    }
    if (minimap) {
        upload(city.minimapTextures[0], minimap->frames.at("MINIMAP_BG").image);
        upload(city.minimapTextures[1], minimap->map, city.minimapSampler);
        upload(city.minimapTextures[2], minimap->frames.at("MINIMAP_PLAYER").image);
        city.minimapCheckpointTextures.resize(minimap->checkpointAnimation.size());
        for (size_t i = 0; i < minimap->checkpointAnimation.size(); ++i)
            upload(city.minimapCheckpointTextures[i], minimap->frames.at(minimap->checkpointAnimation[i].frame).image);
    }
    city.surfaces = draw.surfaces;
}

void prepareCityTargets(CityRender& city, VkFormat color, VkExtent2D size)
{
    auto& vk = city.context;
    VkFormatProperties depthProperties{};
    VK_CALL(vkGetPhysicalDeviceFormatProperties)(vk.gpu, VK_FORMAT_D32_SFLOAT, &depthProperties);
    const auto depth = (depthProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM;
    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = depth;
    depthInfo.extent = {size.width, size.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    check(VK_CALL(vkCreateImage)(vk.device, &depthInfo, nullptr, &city.depth), "vkCreateImage (city depth)");
    VkMemoryRequirements memory{};
    VK_CALL(vkGetImageMemoryRequirements)(vk.device, city.depth, &memory);
    city.depthMemory = allocate(vk, memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(VK_CALL(vkBindImageMemory)(vk.device, city.depth, city.depthMemory, 0), "vkBindImageMemory (city depth)");
    city.depthView = imageView(vk, city.depth, depth, VK_IMAGE_ASPECT_DEPTH_BIT);

    const VkAttachmentDescription attachments[]{
        {0, color, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        {0, depth, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    const VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 2;
    passInfo.pAttachments = attachments;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    check(VK_CALL(vkCreateRenderPass)(vk.device, &passInfo, nullptr, &city.renderPass), "vkCreateRenderPass");

    city.colorViews.reserve(vk.images.size());
    city.frames.reserve(vk.images.size());
    for (const auto image : vk.images) {
        city.colorViews.push_back(imageView(vk, image, color, VK_IMAGE_ASPECT_COLOR_BIT));
        const VkImageView views[]{city.colorViews.back(), city.depthView};
        VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        frame.renderPass = city.renderPass;
        frame.attachmentCount = 2;
        frame.pAttachments = views;
        frame.width = size.width;
        frame.height = size.height;
        frame.layers = 1;
        VkFramebuffer handle = VK_NULL_HANDLE;
        check(VK_CALL(vkCreateFramebuffer)(vk.device, &frame, nullptr, &handle), "vkCreateFramebuffer");
        city.frames.push_back(handle);
    }
}

void prepareCityPipeline(CityRender& city, VkExtent2D size)
{
    auto& vk = city.context;
    constexpr uint32_t vertexCode[] =
#include "city_vert.inc"
    ;
    constexpr uint32_t fragmentCode[] =
#include "city_frag.inc"
    ;
    struct ShaderModules {
        Vulkan& context;
        VkShaderModule handles[2]{};
        ~ShaderModules() {
            auto& vk = context;
            for (auto module : handles) if (module) VK_CALL(vkDestroyShaderModule)(vk.device, module, nullptr);
        }
    } modules{vk};
    VkShaderModuleCreateInfo code{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    code.codeSize = sizeof vertexCode;
    code.pCode = vertexCode;
    check(VK_CALL(vkCreateShaderModule)(vk.device, &code, nullptr, &modules.handles[0]), "vkCreateShaderModule (vertex)");
    code.codeSize = sizeof fragmentCode;
    code.pCode = fragmentCode;
    check(VK_CALL(vkCreateShaderModule)(vk.device, &code, nullptr, &modules.handles[1]), "vkCreateShaderModule (fragment)");
    const VkPipelineShaderStageCreateInfo stages[]{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         modules.handles[0], "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         modules.handles[1], "main", nullptr}
    };
    const VkVertexInputBindingDescription vertices{0, sizeof(CityVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription coordinates[]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CityVertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(CityVertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CityVertex, color)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CityVertex, normal)}
    };
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &vertices;
    input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(coordinates));
    input.pVertexAttributeDescriptions = coordinates;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkViewport viewport{0, 0, float(size.width), float(size.height), 0, 1};
    const VkRect2D scissor{{0, 0}, size};
    VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    view.viewportCount = 1;
    view.pViewports = &viewport;
    view.scissorCount = 1;
    view.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkPushConstantRange constants[]{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CityCameraMatrix)},
        {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(CityCameraMatrix), sizeof(CityFragmentConstants)}
    };
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &city.textureLayout;
    layout.pushConstantRangeCount = 2;
    layout.pPushConstantRanges = constants;
    check(VK_CALL(vkCreatePipelineLayout)(vk.device, &layout, nullptr, &city.layout), "vkCreatePipelineLayout");
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &view;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.layout = city.layout;
    info.renderPass = city.renderPass;
    check(VK_CALL(vkCreateGraphicsPipelines)(vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &city.pipeline),
          "vkCreateGraphicsPipelines (city)");
    if (std::any_of(city.surfaces.begin(), city.surfaces.end(),
                    [](const CitySurface& surface) { return surface.vehicleExterior; })) {
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        check(VK_CALL(vkCreateGraphicsPipelines)(vk.device, VK_NULL_HANDLE, 1, &info, nullptr,
                                                &city.vehicleExteriorPipeline),
              "vkCreateGraphicsPipelines (vehicle exterior)");
        raster.cullMode = VK_CULL_MODE_NONE;
    }
    const bool worldTransparency = city.checkpointFirstSurface || city.vehicleObjectiveFirstSurface ||
        city.nextMissionFirstSurface || city.hummerRouteFirstSurface || city.weaponShopDriveFirstSurface ||
        city.weaponShopApproachFirstSurface || city.weaponShopEntranceFirstSurface || city.returnToLcFirstSurface ||
        city.postLcDriveFirstSurface || city.cooperNextDriveFirstSurface || city.basketballFirstSurface || city.postBasketballFirstSurface || city.repairDriveFirstSurface || city.repairShopFirstSurface || city.deliveryFirstSurface || city.elToroFirstSurface || city.elToroDestinationFirstSurface || city.elToroDriveFirstSurface || city.vanDriveFirstSurface || city.vanSecondDriveFirstSurface || city.postLcWalkFirstSurfaces[0] || city.postLcWalkFirstSurfaces[1] || city.postLcWalkFirstSurfaces[2] ||
        !city.vehicleLights.empty() ||
        std::any_of(city.vehicleEffects.begin(),
        city.vehicleEffects.end(), [](const CityVehicleEffect& effect) { return !effect.texture.empty(); });
    if (!city.captions.empty() || worldTransparency || city.minimapWidth) {
        depth.depthWriteEnable = VK_FALSE;
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        if (worldTransparency)
            check(VK_CALL(vkCreateGraphicsPipelines)(vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &city.checkpointPipeline),
                  "vkCreateGraphicsPipelines (checkpoint)");
        if (!city.vehicleEffects.back().texture.empty() || !city.vehicleLights.empty()) {
            // The retail explosion and vehicle lights use trans_add: GL_SRC_ALPHA, GL_ONE.
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            check(VK_CALL(vkCreateGraphicsPipelines)(vk.device, VK_NULL_HANDLE, 1, &info, nullptr,
                                                    &city.explosionPipeline),
              "vkCreateGraphicsPipelines (additive effects)");
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        }
        if (!city.captions.empty() || city.minimapWidth) {
            depth.depthTestEnable = VK_FALSE;
            check(VK_CALL(vkCreateGraphicsPipelines)(vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &city.captionPipeline),
                  "vkCreateGraphicsPipelines (caption)");
        }
    }
}

void uploadCity(CityRender& city, const CityDrawData& draw)
{
    auto& vk = city.context;
    city.skins = draw.skins;
    city.indexOffset = draw.vertices.size() * sizeof(draw.vertices.front());
    city.indexCount = static_cast<uint32_t>(draw.indices.size());
    const auto indexBytes = draw.indices.size() * sizeof(draw.indices.front());
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = city.indexOffset + indexBytes;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    check(VK_CALL(vkCreateBuffer)(vk.device, &info, nullptr, &city.geometry), "vkCreateBuffer (city geometry)");
    VkMemoryRequirements memory{};
    VK_CALL(vkGetBufferMemoryRequirements)(vk.device, city.geometry, &memory);
    city.geometryMemory = allocate(vk, memory, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(VK_CALL(vkBindBufferMemory)(vk.device, city.geometry, city.geometryMemory, 0), "vkBindBufferMemory (city geometry)");
    void* mapped = nullptr;
    check(VK_CALL(vkMapMemory)(vk.device, city.geometryMemory, 0, info.size, 0, &mapped), "vkMapMemory (city geometry)");
    std::memcpy(mapped, draw.vertices.data(), static_cast<size_t>(city.indexOffset));
    std::memcpy(static_cast<char*>(mapped) + city.indexOffset, draw.indices.data(), indexBytes);
    VK_CALL(vkUnmapMemory)(vk.device, city.geometryMemory);
}

void activateNextMissionCheckpoint(CityRender& city)
{
    const auto& next = *city.opening.payphone->postFight.nextCheckpoint;
    auto& script = city.nextMissionScript.emplace(CityScriptProgram{{{64, next.payload}}});
    script.unlock(0, 0, 0);
}

void activateLocoGuard(CityRender& city)
{
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    auto& playback = city.playback;
    playback.locoOpponents.clear();
    playback.locoLockTarget.reset();
    playback.playerPunchMs.reset();
    playback.spawnFightOpponent(city.opening, city.terrain, *route.locoGuardPlacement, false);
    CityScriptProgram program(1);
    for (const auto& [opcode, payload] : route.locoGuardCommands)
        program.front().push_back({opcode, payload});
    city.locoGuardScript.emplace(std::move(program)).unlock(0, 0, 0);
}

void activatePostLcCheckpoint(CityRender& city)
{
    city.nextMissionMarker = city.payphoneGameplayMs.value_or(0);
    city.weaponShopUnlocked = true;
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    const auto& wait = city.playback.openingMissionsPaid[7] ? route.vanEntranceWait :
        city.playback.openingMissionsPaid[6] ? route.elToroCheckpointWait :
        city.playback.openingMissionsPaid[5] ? route.postBasketballCheckpointWait :
        city.playback.openingMissionsPaid[4] ? route.brawlNextCheckpointWait :
        city.playback.openingMissionsPaid[3] ? route.cooperNextCheckpointWait :
        city.playback.openingMissionsPaid[2] ? route.locoNextCheckpointWait : route.postLcCheckpointWait;
    auto& script = city.nextMissionScript.emplace(CityScriptProgram{{{64, wait}}});
    script.unlock(0, 0, 0);
}

void saveOpeningCheckpoint(CityRender& city, const ResourceFileSystem& assets, int32_t script)
{
    const auto& playback = city.playback;
    if (!playback.player) return;
    RetailSavedState save;
    save.cash = playback.playerCash;
    save.context = 0;
    save.position = playback.vehicle && playback.vehicle->occupied
        ? playback.vehicle->position : playback.player->position;
    save.armor[0] = playback.playerArmor;
    save.completedScripts.push_back({script, -1});
    if (city.achievements) city.achievements->capture(save);
    try {
        writeWindowsOpeningCheckpoint(assets, save);
    } catch (const std::runtime_error& error) {
        std::cerr << "Could not save the Windows checkpoint: " << error.what() << '\n';
    }
}

void completeOpeningMission(CityRender& city, size_t mission, const CityMissionTitle& title)
{
    if (!city.playback.creditOpeningMission(mission, title.cashReward)) return;
    city.missionRewardIndex = mission;
    city.missionRewardMs = 0;
    if (city.achievements) {
        city.achievements->recordMissionCash(title.cashReward);
        city.achievements->recordMissionEnded();
    }
    if (city.audio) {
        try {
            city.audio->playMissionCompletion(0);
        } catch (const std::runtime_error& error) {
            std::cerr << error.what() << '\n';
        }
    }
}

void activateReturnToLc(CityRender& city)
{
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    city.returningToLc = true;
    auto& script = city.hummerRouteScript.emplace(CityScriptProgram{{
        {89, route.returnToHummerWait}, {105, route.returnToLcWait}}});
    script.unlock(0, 0, 0);
}

void restoreOpeningCheckpoint(CityRender& city, const RetailSavedState& checkpoint)
{
    const auto& opening = city.opening;
    if (!opening.payphone || !opening.payphone->postFight.nextCheckpoint ||
        opening.payphone->postFight.unlockedScript != windowsOpeningCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved opening checkpoint.");
    const auto ground = city.terrain.groundContact(*checkpoint.position);
    if (!ground) throw std::runtime_error("No walkable ground at the saved checkpoint.");
    auto& playback = city.playback;
    playback = CityPlayback{};
    const auto& route = *opening.payphone;
    const auto& next = *route.postFight.nextCheckpoint;
    const auto script = checkpoint.completedScripts.empty() ? windowsOpeningCheckpointScript :
        checkpoint.completedScripts.front().script;
    const bool postCooperDelivery = script == windowsPostCooperDeliveryCheckpointScript;
    const bool postDelivery = postCooperDelivery || script == windowsPostDeliveryCheckpointScript;
    const bool postBasketball = postDelivery || script == windowsPostBasketballCheckpointScript;
    const bool postBrawl = postBasketball || script == windowsPostBrawlCheckpointScript;
    const bool postCooper = postBrawl || script == windowsPostCooperCheckpointScript;
    const bool postLoco = postCooper || script == windowsPostLocoCheckpointScript;
    const bool postLc = postLoco || script == windowsPostLcCheckpointScript;
    if (postLc && next.lcUnlockedScript != windowsPostLcCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved LC handoff.");
    if (postLoco && next.locoUnlockedScript != windowsPostLocoCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved Loco handoff.");
    if (postCooper && next.cooperUnlockedScript != windowsPostCooperCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved Cooper handoff.");
    if (postBrawl && next.brawlUnlockedScript != windowsPostBrawlCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved C2M501 handoff.");
    if (postBasketball && next.basketballUnlockedScript != windowsPostBasketballCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved basketball handoff.");
    if (postDelivery && (!next.elToroCheckpoint || next.deliveryUnlockedScripts.empty() ||
                         next.deliveryUnlockedScripts.front() != windowsPostDeliveryCheckpointScript))
        throw std::runtime_error("This game data does not contain the saved car delivery handoff.");
    if (postCooperDelivery && next.elToroUnlockedScript != windowsPostCooperDeliveryCheckpointScript)
        throw std::runtime_error("This game data does not contain the saved Cooper delivery handoff.");
    const auto& placements = postCooperDelivery ? next.elToroClosing.placements :
        postDelivery ? next.deliveryClosing.placements :
        postBasketball ? next.basketballClosing.placements :
        postBrawl ? next.cooperNextClosing.placements : postLoco ? next.postLcFightIntro.placements :
        postLc ? next.lcClosingCinematic.placements : route.postFight.placements;
    const auto player = std::find_if(placements.rbegin(), placements.rend(), [](const auto& cue) {
        return cue.actor == "Player" && cue.visible;
    });
    const float yaw = player == placements.rend() ? 0 : player->rotationDegrees[2];
    playback.player = CityPlayerPose{*checkpoint.position, yaw};
    playback.player->followCameraYaw = yaw;
    playback.player->groundTriangle = ground->triangle;
    playback.player->position[2] = ground->height;
    playback.playerCash = *checkpoint.cash;
    playback.playerArmor = *checkpoint.armor[0];
    playback.openingMissionsPaid[0] = true;
    playback.elapsedMs = opening.durationMs;
    playback.nextDialogue = opening.dialogue.size();
    playback.payphoneReached = true;
    playback.enteredVehicle = true;
    playback.payphoneOpponentDisabled = true;
    playback.payphoneOpponentHealth = 0;
    playback.protectionRegistered = true;
    city.payphoneGameplayMs = route.cinematicDurationMs;
    city.nextAudio = opening.audio.size();
    city.startup.reset();
    if (city.achievements)
        city.achievements->restore(sampleRetailSave(checkpoint, windowsOpeningCheckpointVersion, 0));
    if (postLc) {
        playback.openingMissionsPaid[1] = true;
        playback.openingMissionsPaid[2] = postLoco;
        playback.openingMissionsPaid[3] = postCooper;
        playback.openingMissionsPaid[4] = postBrawl;
        playback.openingMissionsPaid[5] = postBasketball;
        playback.openingMissionsPaid[6] = postDelivery;
        playback.openingMissionsPaid[7] = postCooperDelivery;
        playback.nextMissionVehicleActive = true;
        playback.nextMissionVehicleAcquired = true;
        activatePostLcCheckpoint(city);
    } else {
        activateNextMissionCheckpoint(city);
    }
}

const CityActor& bindMissionVehicle(CityRender& city, std::string_view name,
                                    const std::optional<CityVehicleGeometry>& vehicleGeometry,
                                    const std::optional<CityTerrain::VehicleFootprint>& footprint,
                                    const std::optional<CityAnimationClip>& seated,
                                    const std::optional<std::array<CityVehicleCharacterClips, 2>>& clips)
{
    auto& opening = city.opening;
    const auto actor = std::find_if(opening.actors.begin(), opening.actors.end(), [&](const auto& entry) {
        return entry.payphoneOnly && entry.name == name;
    });
    if (actor == opening.actors.end() || !vehicleGeometry || !footprint)
        throw std::runtime_error("Missing mission vehicle geometry for " + std::string(name));
    const auto& geometry = *vehicleGeometry;
    if (opening.vehicleActor != size_t(actor - opening.actors.begin())) {
        city.vehicleFirstVertex = geometry.vehicleFirstVertex;
        const auto first = city.vertices.begin() + geometry.vehicleFirstVertex;
        city.vehicleVertices.assign(first, first + geometry.vehicleVertexCount);
        city.wheels = geometry.wheels;
        city.vehicleMorphs = geometry.vehicleMorphs;
        city.terrain.vehicleFootprint = *footprint;
        opening.vehicleActor = actor - opening.actors.begin();
        opening.vehicleSeated = seated;
        opening.vehicleCharacterClips = clips;
    }
    return *actor;
}

const CityActor& bindHummerGeometry(CityRender& city)
{
    const auto& checkpoint = *city.opening.payphone->postFight.nextCheckpoint;
    return bindMissionVehicle(city, checkpoint.targetVehicle, city.nextVehicleGeometry,
                              city.nextVehicleFootprint, checkpoint.vehicleSeated, checkpoint.vehicleCharacterClips);
}

void activateCarDelivery(CityRender& city)
{
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    city.repairShop = CityRender::RepairShop::inactive;
    city.repairShopMarker.reset();
    city.repairTutorialVisible = false;
    city.deliveryScript.emplace(CityScriptProgram{{{105, route.deliveryWait}}}).unlock(0, 0, 0);
}

void stagePostBasketballVehicle(CityRender& city)
{
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    const auto& actor = bindMissionVehicle(city, route.postBasketballVehicle, city.postBasketballVehicleGeometry,
                                          city.postBasketballVehicleFootprint, route.postBasketballVehicleSeated,
                                          route.postBasketballVehicleClips);
    auto& playback = city.playback;
    const auto& placement = route.postBasketballApproach.front();
    if (placement.actor != actor.name || !placement.visible ||
        !playback.spawnVehicle(actor, city.terrain, placement.position, placement.rotationDegrees[2]))
        throw std::runtime_error("Could not stage the retail script 29 vehicle.");
    playback.nextMissionVehicleActive = true;
    playback.nextMissionVehicleAcquired = false;
    playback.postLcVehicleActive = true;
    playback.missionConditions.clear();
    playback.locoOpponents.clear();
    playback.locoLockTarget.reset();
    for (const auto& cue : route.postBasketballApproach) {
        if (cue.actor != actor.name) playback.spawnFightOpponent(city.opening, city.terrain, cue, false);
    }
    CityScriptProgram program(1);
    for (const auto& [opcode, payload] : route.postBasketballVehicleCommands)
        program.front().push_back({opcode, payload});
    city.postBasketballVehicleScript.emplace(std::move(program)).unlock(0, 0, 0);
}

void activatePostLcDrive(CityRender& city)
{
    const auto& actor = bindHummerGeometry(city);
    auto& playback = city.playback;
    const auto& mission = *city.opening.payphone->postFight.nextCheckpoint;
    const auto& placements = mission.postLcIntro.placements;
    const auto vehicle = std::find_if(placements.rbegin(), placements.rend(), [&](const auto& cue) {
        return cue.visible && cue.actor == mission.targetVehicle;
    });
    if (vehicle == placements.rend()) throw std::runtime_error("Missing retail script 25 Hummer placement.");
    if (const auto player = std::find_if(placements.rbegin(), placements.rend(), [](const auto& cue) {
            return cue.visible && cue.actor == "Player";
        }); player != placements.rend()) {
        playback.player = CityPlayerPose{player->position, player->rotationDegrees[2]};
        playback.player->followCameraYaw = player->rotationDegrees[2];
        if (const auto ground = city.terrain.groundContact(player->position)) {
            playback.player->position[2] = ground->height;
            playback.player->groundTriangle = ground->triangle;
        }
    }
    playback.vehicle.reset();
    playback.nextMissionVehicleActive = true;
    playback.nextMissionVehicleAcquired = false;
    playback.postLcVehicleActive = true;
    playback.scriptedVehicleExit = false;
    playback.cameraTransition.reset();
    playback.cameraShake = {};
    playback.lastCameraTapMs.reset();
    playback.missionConditions.clear();
    if (!playback.spawnVehicle(actor, city.terrain, vehicle->position, vehicle->rotationDegrees[2]))
        throw std::runtime_error("No walkable ground under the retail script 25 Hummer.");
    city.hummerRouteMarker.reset();
    city.postLcIntroFinished = true;
    city.postLcAmbushFinished = false;
    city.postLcWalkScript.reset();
    city.postLcFightScript.reset();
    playback.locoOpponents.clear();
    playback.locoLockTarget.reset();
    city.postLcWalkMarker.reset();
    city.postLcTailMs.reset();
    city.spyGauge.reset();
    city.hummerArrivalFadeMs.reset();
    city.pendingHummerCinematic = nullptr;
    auto& script = city.hummerRouteScript.emplace(CityScriptProgram{{{89, mission.postLcVehicleWait},
        {70, mission.postLcVehicleProtection}, {105, mission.postLcDriveWait}}});
    script.unlock(0, 0, 0);
}

void activateNextMissionVehicle(CityRender& city)
{
    auto& playback = city.playback;
    const auto& checkpoint = *city.opening.payphone->postFight.nextCheckpoint;
    bindHummerGeometry(city);
    if (playback.nextMissionVehicleActive) {
        const auto& placements = checkpoint.cinematic.placements;
        const auto placement = std::find_if(placements.rbegin(), placements.rend(),
            [](const auto& cue) { return cue.actor == "Player" && cue.visible; });
        if (placement != placements.rend()) {
            playback.player = CityPlayerPose{placement->position, placement->rotationDegrees[2]};
            playback.player->followCameraYaw = placement->rotationDegrees[2];
            if (const auto ground = city.terrain.groundContact(placement->position)) {
                playback.player->position[2] = ground->height;
                playback.player->groundTriangle = ground->triangle;
            }
        }
    }
    playback.vehicle.reset();
    playback.cameraTransition.reset();
    playback.cameraShake = {};
    playback.lastCameraTapMs.reset();
    playback.scriptedVehicleExit = false;
    playback.missionConditions.clear();
    for (auto& effect : city.vehicleEffects) effect.simulation = {};
    if (city.radio) city.radio->stop();
    playback.nextMissionVehicleActive = true;
    city.hummerRouteMarker.reset();
    city.pendingHummerCinematic = nullptr;
    city.weaponShopDrive = false;
    city.returningToLc = false;
    city.returnToLcFadeMs.reset();
    city.weaponShopApproach = false;
    city.weaponShopUnlocked = false;
    city.weaponShopTutorialVisible = false;
    city.weaponShopOpen = false;
    city.hummerTutorialVisible = false;
    if (checkpoint.vehicleCheckpoint) {
        auto& script = city.hummerRouteScript.emplace(CityScriptProgram{{{89, checkpoint.vehicleEntryWait},
            {57, checkpoint.vehicleRouteDelay}, {112, checkpoint.vehicleTutorialCommand},
            {70, checkpoint.vehicleProtectionCommand},
            {105, checkpoint.vehicleCheckpointWait}}});
        script.unlock(0, 0, 0);
    }
}

uint32_t nextMissionFadeDuration(const CityRender& city)
{
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    if (city.missionTitleIndex == 8) return route.vanEntranceFadeMs;
    if (city.missionTitleIndex == 7) return route.elToroFadeMs;
    if (city.missionTitleIndex == 6) return route.postBasketballFadeMs;
    if (city.missionTitleIndex == 5) return route.brawlNextFadeMs;
    if (city.missionTitleIndex == 4) return route.cooperNextFadeMs;
    if (city.missionTitleIndex == 3) return route.locoNextFadeMs;
    return city.missionTitleIndex == 2 ? route.postLcFadeMs : route.fadeMs;
}

uint32_t hummerArrivalFadeDuration(const CityRender& city)
{
    if (!city.pendingHummerCinematic) return 0;
    const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
    if (city.pendingHummerCinematic == &route.cooperNextArrival) return route.cooperNextArrivalFadeMs;
    if (city.pendingHummerCinematic == &route.postLcAmbush) return route.postLcArrivalFadeMs;
    if (city.pendingHummerCinematic == &route.postBasketballBriefing) return route.postBasketballBriefingFadeMs;
    if (city.pendingHummerCinematic == &route.elToroClosing) return route.elToroClosingFadeMs;
    if (city.pendingHummerCinematic == &route.vanArrival) return route.vanArrivalFadeMs;
    if (city.pendingHummerCinematic == &route.vanSecondArrival) return route.vanSecondArrivalFadeMs;
    return 0;
}

bool presentCity(CityRender& city, VkExtent2D size, size_t* lightDraws = nullptr)
{
    auto& vk = city.context;
    const auto elapsedMs = city.playback.elapsedMs;
    const auto payphoneMs = city.payphoneCinematic ? std::optional{city.payphoneCinematic->elapsedMs()} : std::nullopt;
    const auto postFightMs = city.postFightCinematic ? std::optional{city.postFightCinematic->elapsedMs()} : std::nullopt;
    const auto* missionScene = city.activeMissionCinematic;
    const auto actorSceneMs = city.payphoneGameplayMs ? city.payphoneGameplayMs : payphoneMs;
    const auto* actorScene = city.postLcTailMs && !missionScene
        ? &city.opening.payphone->postFight.nextCheckpoint->postLcTail : missionScene;
    const auto actorMissionMs = postFightMs ? postFightMs : city.postLcTailMs;
    const auto* vehicle = city.playback.vehicle ? &*city.playback.vehicle : nullptr;
    const auto cinematicVehicle = vehicle && missionScene && postFightMs && city.opening.vehicleActor
        ? cityCinematicVehiclePose(*missionScene, city.opening.actors.at(*city.opening.vehicleActor).name,
                                   *vehicle, city.terrain, *postFightMs)
        : std::nullopt;
    if (cinematicVehicle) vehicle = &*cinematicVehicle;
    const auto* player = city.playback.player ? &*city.playback.player : nullptr;
    std::array<float, 3> cameraEye;
    std::optional<CityCameraMatrix> distantCamera;
    const auto camera = cityCameraMatrix(city.opening, float(size.width) / size.height, elapsedMs, player, vehicle,
                                         city.playback.cameraTransition, city.playback.cameraShake.offset,
                                         city.startup ? &*city.startup : nullptr, &cameraEye, &distantCamera,
                                         payphoneMs, postFightMs, missionScene);
    const bool failed = city.playback.missionFailure.has_value();
    const bool showMinimap = city.minimapWidth && player && !city.playback.activeDialogue && !failed &&
        !payphoneMs && !postFightMs;
    std::optional<std::array<float, 2>> minimapMarker;
    std::vector<std::array<float, 2>> killTargetMarkers;
    size_t minimapMarkerStep = 0;
    const bool updateVehicle = vehicle && !city.vehicleVertices.empty();
    auto cooperPose = city.locoCooper;
    if (city.cooperActor && city.cooperGeometry && postFightMs) {
        const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
        if (missionScene == &route.locoCooperIntro || missionScene == &route.elToroDeparture) {
            const auto& actor = city.opening.actors[*city.cooperActor];
            cooperPose = cityStagedVehiclePose(*missionScene, actor.name, city.terrain, *postFightMs);
        }
    }
    const auto wheelOffsets = updateVehicle
        ? cityVehicleWheelOffsets(city.opening.actors.at(*city.opening.vehicleActor), *vehicle,
                                  city.wheels, city.terrain) : std::vector<float>{};
    const auto lights = vehicle && !city.playback.nextMissionVehicleActive && !vehicle->exploded && !city.vehicleLights.empty() &&
            (vehicle->lightEffects[0] || vehicle->lightEffects[1])
        ? cityVehicleLightFrames(city.opening.actors.at(*city.opening.vehicleActor), *vehicle,
                                 city.wheels, wheelOffsets) : std::nullopt;
    const bool updateWalkMarker = !city.checkpointMeshes[0].empty() && elapsedMs >= city.opening.durationMs &&
                                  !city.playback.timeAfterCheckpointMs;
    const bool updateRouteMarker = !city.checkpointMeshes[1].empty() && city.playback.payphoneActive &&
                                   !city.playback.payphoneReached;
    const auto vehicleObjective = city.playback.vehicleObjectivePosition(city.opening);
    const bool updateVehicleMarker = !failed && vehicleObjective && !city.checkpointMeshes[2].empty();
    const bool postLc = city.playback.openingMissionsPaid[1];
    const size_t footMarkerIndex = city.playback.openingMissionsPaid[6] ? 19 : !postLc && city.weaponShopUnlocked ? 7 :
        !postLc && city.weaponShopApproach ? 6 : 3;
    const bool updateNextMissionMarker = city.nextMissionMarker && !failed && !city.checkpointMeshes[footMarkerIndex].empty();
    const bool updateHummerMarker = city.hummerRouteMarker && !failed && !city.checkpointMeshes[4].empty();
    const bool updateShopMarker = city.hummerRouteMarker && city.weaponShopDrive && !failed &&
        !city.returningToLc && !city.checkpointMeshes[5].empty();
    const bool updateLcMarker = city.hummerRouteMarker && city.returningToLc && !failed &&
        !city.checkpointMeshes[8].empty();
    const bool cooperNextDrive = city.missionTitleIndex == 4;
    const size_t vanMarkerIndex = city.vanSecondDrive ? 23 : 22;
    const bool updateVanDrive = city.vanDriveMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[vanMarkerIndex].empty();
    const bool updateElToroDrive = city.elToroDriveMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[21].empty();
    const bool updateElToroDestination = city.elToroDestinationMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[20].empty();
    const bool updateDeliveryMarker = city.deliveryMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[18].empty();
    const bool updateRepairShopMarker = city.repairShopMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[17].empty();
    const bool updateRepairDriveMarker = city.repairDriveMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[16].empty();
    const bool updatePostBasketballMarker = city.postBasketballMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[15].empty();
    const bool updateBasketballMarker = city.basketballMarker && !failed && !postFightMs &&
        !city.checkpointMeshes[14].empty();
    const bool updateCooperNextMarker = cooperNextDrive && city.hummerRouteMarker && !failed &&
        !city.checkpointMeshes[13].empty();
    const bool updatePostLcDriveMarker = city.hummerRouteMarker && city.postLcIntroFinished && !cooperNextDrive && !failed &&
        !city.checkpointMeshes[9].empty();
    const auto walkMarker = !failed && !postFightMs ? city.postLcWalkMarker : std::nullopt;
    const std::array updateMarkers{updateWalkMarker, updateRouteMarker, updateVehicleMarker,
                                   updateNextMissionMarker && footMarkerIndex == 3,
                                   updateHummerMarker && !city.weaponShopDrive && !city.returningToLc &&
                                       !city.postLcIntroFinished && !cooperNextDrive, updateShopMarker,
                                   updateNextMissionMarker && footMarkerIndex == 6,
                                   updateNextMissionMarker && footMarkerIndex == 7, updateLcMarker, updatePostLcDriveMarker,
                                   walkMarker && walkMarker->index == 0, walkMarker && walkMarker->index == 1,
                                   walkMarker && walkMarker->index == 2, updateCooperNextMarker, updateBasketballMarker, updatePostBasketballMarker, updateRepairDriveMarker, updateRepairShopMarker, updateDeliveryMarker,
                                   updateNextMissionMarker && footMarkerIndex == 19, updateElToroDestination, updateElToroDrive,
                                   updateVanDrive && vanMarkerIndex == 22, updateVanDrive && vanMarkerIndex == 23};
    const bool particlesVisible = std::any_of(city.vehicleEffects.begin(), city.vehicleEffects.end(),
        [](const CityVehicleEffect& effect) { return !effect.simulation.particles().empty(); });
    if (!city.skins.empty() || !city.billboards.empty() || updateVehicle || city.restoreVehicleGeometry || updateWalkMarker ||
        updateRouteMarker || updateVehicleMarker || updateNextMissionMarker || updateHummerMarker || updateShopMarker ||
        updateLcMarker || updatePostLcDriveMarker || updateCooperNextMarker || updateBasketballMarker || updatePostBasketballMarker || updateRepairDriveMarker || updateRepairShopMarker || updateDeliveryMarker || updateElToroDestination || updateElToroDrive || updateVanDrive || walkMarker || cooperPose || showMinimap || particlesVisible || lights) {
        if (updateVehicle)
            poseCityVehicle(city.opening.actors[*city.opening.vehicleActor], *vehicle, city.vehicleVertices,
                            city.wheels, city.vehicleMorphs, city.vertices.data() + city.vehicleFirstVertex, wheelOffsets);
        poseCityActors(city.opening, city.skins, city.playback, city.vertices.data(),
                       actorSceneMs, actorMissionMs, actorScene, &city.terrain);
        if (updateWalkMarker)
            poseCityCheckpoint(*city.opening.checkpoint, city.checkpointMeshes[0],
                               elapsedMs - city.opening.durationMs, city.vertices.data());
        if (updateRouteMarker)
            poseCityCheckpoint(city.opening.payphone->destination, city.checkpointMeshes[1],
                               city.playback.timeAfterEntryMs, city.vertices.data());
        if (updateVehicleMarker)
            poseCityVehicleObjective(city.opening, vehicle, city.checkpointMeshes[2],
                                     *city.playback.timeAfterCheckpointMs -
                                         city.opening.checkpoint->nextTutorial.startMs,
                                     city.vertices.data());
        if (updateNextMissionMarker) {
            const auto& next = *city.opening.payphone->postFight.nextCheckpoint;
            const auto& destination = footMarkerIndex == 19 ? *next.elToroCheckpoint :
                footMarkerIndex == 7 ? *next.weaponShopEntranceMarker :
                footMarkerIndex == 6 ? *next.weaponShopApproach : next.marker;
            poseCityCheckpoint(destination,
                               city.checkpointMeshes[footMarkerIndex],
                               city.payphoneGameplayMs.value_or(0) - *city.nextMissionMarker, city.vertices.data());
        }
        if (updateMarkers[4])
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->vehicleCheckpoint,
                               city.checkpointMeshes[4], city.payphoneGameplayMs.value_or(0) - *city.hummerRouteMarker,
                               city.vertices.data());
        if (updateShopMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->weaponShopDriveCheckpoint,
                               city.checkpointMeshes[5], city.payphoneGameplayMs.value_or(0) - *city.hummerRouteMarker,
                               city.vertices.data());
        if (updateLcMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->returnToLcCheckpoint,
                               city.checkpointMeshes[8], city.payphoneGameplayMs.value_or(0) - *city.hummerRouteMarker,
                               city.vertices.data());
        if (updatePostLcDriveMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->postLcDriveCheckpoint,
                               city.checkpointMeshes[9], city.payphoneGameplayMs.value_or(0) - *city.hummerRouteMarker,
                               city.vertices.data());
        if (updateVanDrive) {
            const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
            const auto& target = *(city.vanSecondDrive ? route.vanSecondDriveCheckpoint : route.vanDriveCheckpoint);
            poseCityCheckpoint(target, city.checkpointMeshes[vanMarkerIndex],
                               city.payphoneGameplayMs.value_or(0) - *city.vanDriveMarker, city.vertices.data());
        }
        if (updateElToroDrive)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->elToroDriveCheckpoint,
                               city.checkpointMeshes[21], city.payphoneGameplayMs.value_or(0) - *city.elToroDriveMarker,
                               city.vertices.data());
        if (updateElToroDestination)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->elToroDestination,
                               city.checkpointMeshes[20], city.payphoneGameplayMs.value_or(0) - *city.elToroDestinationMarker,
                               city.vertices.data());
        if (updateDeliveryMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->deliveryCheckpoint,
                               city.checkpointMeshes[18], city.payphoneGameplayMs.value_or(0) - *city.deliveryMarker,
                               city.vertices.data());
        if (updateRepairShopMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->repairShopApproach,
                               city.checkpointMeshes[17], city.payphoneGameplayMs.value_or(0) - *city.repairShopMarker,
                               city.vertices.data());
        if (updateRepairDriveMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->repairDriveCheckpoint,
                               city.checkpointMeshes[16], city.payphoneGameplayMs.value_or(0) - *city.repairDriveMarker,
                               city.vertices.data());
        if (updatePostBasketballMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->postBasketballDestination,
                               city.checkpointMeshes[15], city.payphoneGameplayMs.value_or(0) - *city.postBasketballMarker,
                               city.vertices.data());
        if (updateBasketballMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->basketballCheckpoint,
                               city.checkpointMeshes[14], city.payphoneGameplayMs.value_or(0) - *city.basketballMarker,
                               city.vertices.data());
        if (updateCooperNextMarker)
            poseCityCheckpoint(*city.opening.payphone->postFight.nextCheckpoint->cooperNextDriveCheckpoint,
                               city.checkpointMeshes[13], city.payphoneGameplayMs.value_or(0) - *city.hummerRouteMarker,
                               city.vertices.data());
        if (walkMarker) {
            const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
            poseCityCheckpoint(*route.postLcWalkCheckpoints[walkMarker->index],
                               city.checkpointMeshes[10 + walkMarker->index],
                               city.payphoneGameplayMs.value_or(0) - walkMarker->startMs, city.vertices.data());
        }
        // finishFrame waits for the graphics queue before this allocation is written again.
        void* mapped = nullptr;
        check(VK_CALL(vkMapMemory)(vk.device, city.geometryMemory, 0, city.indexOffset, 0, &mapped),
              "vkMapMemory (actor skinning)");
        for (const auto& billboard : city.billboards) {
            poseCityBillboard(billboard, camera, city.vertices.data());
            const auto offset = billboard.firstVertex * sizeof(CityVertex);
            std::memcpy(static_cast<char*>(mapped) + offset, city.vertices.data() + billboard.firstVertex,
                        billboard.source.size() * sizeof(CityVertex));
        }
        for (const auto& effect : city.vehicleEffects) {
            const auto& particles = effect.simulation.particles();
            if (particles.empty()) continue;
            auto* vertices = city.vertices.data() + effect.firstVertex;
            poseCityParticles(particles, camera, vertices, &effect.scene, effect.elapsedMs);
            std::memcpy(static_cast<char*>(mapped) + effect.firstVertex * sizeof(CityVertex), vertices,
                        particles.size() * 6 * sizeof(CityVertex));
        }
        for (const auto& range : city.skins) {
            const auto count = city.opening.actors[range.actor].skins[range.skin].vertices.size();
            const auto offset = range.firstVertex * sizeof(CityVertex);
            std::memcpy(static_cast<char*>(mapped) + offset, city.vertices.data() + range.firstVertex,
                        count * sizeof(CityVertex));
        }
        if (updateVehicle || city.restoreVehicleGeometry) {
            const auto offset = city.vehicleFirstVertex * sizeof(CityVertex);
            const auto* vertices = updateVehicle ? city.vertices.data() + city.vehicleFirstVertex : city.vehicleVertices.data();
            std::memcpy(static_cast<char*>(mapped) + offset, vertices, city.vehicleVertices.size() * sizeof(CityVertex));
            city.restoreVehicleGeometry = false;
        }
        if (cooperPose) {
            const auto& geometry = *city.cooperGeometry;
            const auto& actor = city.opening.actors[*city.cooperActor];
            auto* vertices = city.vertices.data() + geometry.vehicleFirstVertex;
            const auto offsets = cityVehicleWheelOffsets(actor, *cooperPose, geometry.wheels, city.terrain);
            poseCityVehicle(actor, *cooperPose, city.cooperVertices, geometry.wheels,
                            geometry.vehicleMorphs, vertices, offsets);
            std::memcpy(static_cast<char*>(mapped) + geometry.vehicleFirstVertex * sizeof(CityVertex),
                        vertices, city.cooperVertices.size() * sizeof(CityVertex));
        }
        for (size_t index = 0; index < city.checkpointMeshes.size(); ++index) {
            if (!updateMarkers[index]) continue;
            for (const auto& mesh : city.checkpointMeshes[index]) {
                const auto offset = mesh.firstVertex * sizeof(CityVertex);
                std::memcpy(static_cast<char*>(mapped) + offset, city.vertices.data() + mesh.firstVertex,
                            mesh.positions.size() * sizeof(CityVertex));
            }
        }
        if (lights) for (size_t i = 0; i < city.vehicleLights.size(); ++i) {
            const auto effectIndex = i / lights->size();
            if (!vehicle->lightEffects.at(effectIndex)) continue;
            const auto& mesh = city.vehicleLights[i];
            const auto& effects = city.opening.actors.at(*city.opening.vehicleActor).lights->effects;
            const float opacity = sampleCityDiffuseColor(effects.at(effectIndex), float(vehicle->lightTimeMs))[3];
            poseCityVehicleLight(mesh, lights->at(i % lights->size()), opacity, city.vertices.data());
            std::memcpy(static_cast<char*>(mapped) + mesh.firstVertex * sizeof(CityVertex),
                        city.vertices.data() + mesh.firstVertex, mesh.source.size() * sizeof(CityVertex));
        }
        if (showMinimap) {
            const bool driving = vehicle && vehicle->occupied;
            const auto& position = driving ? vehicle->position : player->position;
            const auto center = retailMinimapUv(RetailMap::city, position[0], position[1]);
            const auto speedKmh = driving ? std::abs(vehicle->speed) * 0.036f : 0.f;
            const auto zoom = retailMinimapScale(speedKmh);
            const std::array right{camera[0][0], camera[0][1]};
            using Target = CityRender::MinimapTarget;
            std::optional<Target> target;
            const std::array<float, 3>* targetPosition = nullptr;
            const auto vehiclePosition = city.trackOpeningVehicle ? vehicleObjective : std::nullopt;
            const auto nextPlacement = [&]() -> const CityCinematicPlacement* {
                if (!city.playback.nextMissionVehicleActive || city.playback.nextMissionVehicleAcquired) return nullptr;
                const auto& checkpoint = *city.opening.payphone->postFight.nextCheckpoint;
                const auto& placements = checkpoint.cinematic.placements;
                const auto found = std::find_if(placements.rbegin(), placements.rend(), [&](const auto& cue) {
                    return cue.actor == checkpoint.targetVehicle;
                });
                return found == placements.rend() || !found->visible ? nullptr : &*found;
            }();
            const CityFightOpponent* guard = nullptr;
            if (city.locoGuardScript) {
                const auto& placement = *city.opening.payphone->postFight.nextCheckpoint->locoGuardPlacement;
                const auto found = city.playback.locoOpponents.find(placement.actor);
                if (found != city.playback.locoOpponents.end() && found->second.health > 0 && !found->second.disabled)
                    guard = &found->second;
            }
            if (city.locoCooperScript && cooperPose) {
                target = Target::fightActor;
                targetPosition = &cooperPose->position;
            } else if (guard) {
                target = Target::fightActor;
                targetPosition = &guard->pose.position;
            } else if (city.vanDriveMarker) {
                target = Target::vanDrive;
                const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
                targetPosition = &(city.vanSecondDrive ? route.vanSecondDriveCheckpoint : route.vanDriveCheckpoint)->position;
            } else if (city.elToroDriveMarker) {
                target = Target::elToroDrive;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->elToroDriveCheckpoint->position;
            } else if (city.elToroDestinationMarker) {
                target = Target::elToroDestination;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->elToroDestination->position;
            } else if (city.deliveryMarker) {
                target = Target::delivery;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->deliveryCheckpoint->position;
            } else if (city.repairShopMarker) {
                target = Target::repairShop;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->repairShopApproach->position;
            } else if (city.repairDriveMarker) {
                target = Target::repairDrive;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->repairDriveCheckpoint->position;
            } else if (city.postBasketballMarker) {
                target = Target::postBasketball;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->postBasketballDestination->position;
            } else if (city.basketballMarker) {
                target = Target::basketball;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint->basketballCheckpoint->position;
            } else if (walkMarker) {
                target = Target::postLcWalk;
                targetPosition = &city.opening.payphone->postFight.nextCheckpoint
                    ->postLcWalkCheckpoints[walkMarker->index]->position;
            } else if (city.hummerRouteMarker) {
                target = Target::hummerRoute;
                const auto& route = *city.opening.payphone->postFight.nextCheckpoint;
                targetPosition = &(cooperNextDrive ? route.cooperNextDriveCheckpoint :
                    city.postLcIntroFinished ? route.postLcDriveCheckpoint :
                    city.returningToLc ? route.returnToLcCheckpoint :
                    city.weaponShopDrive ? route.weaponShopDriveCheckpoint : route.vehicleCheckpoint)->position;
            } else if ((city.returningToLc || city.postLcIntroFinished || cooperNextDrive) && city.hummerRouteScript && vehicle) {
                target = Target::nextVehicle;
                targetPosition = &vehicle->position;
            } else if (city.nextMissionMarker) {
                target = Target::nextMission;
                const auto& next = *city.opening.payphone->postFight.nextCheckpoint;
                targetPosition = &(footMarkerIndex == 19 ? *next.elToroCheckpoint :
                    footMarkerIndex == 7 ? *next.weaponShopEntranceMarker :
                    footMarkerIndex == 6 ? *next.weaponShopApproach : next.marker).position;
            } else if (city.payphoneLockMarker) {
                target = Target::fightActor;
                targetPosition = &city.payphoneLockMarker->position;
            } else if (vehicle && (city.postBasketballVehicleScript ||
                                  ((city.vanAcquisitionScript || city.vanReentryScript) &&
                                   !city.playback.nextMissionVehicleAcquired))) {
                target = Target::nextVehicle;
                targetPosition = &vehicle->position;
            } else if (nextPlacement) {
                target = Target::nextVehicle;
                targetPosition = &nextPlacement->position;
            } else if (city.playback.payphoneActive && !city.playback.payphoneReached && city.opening.payphone) {
                target = Target::payphone;
                targetPosition = &city.opening.payphone->destination.position;
            } else if (city.opening.checkpoint && elapsedMs >= city.opening.durationMs &&
                       !city.playback.timeAfterCheckpointMs) {
                target = Target::walkingCheckpoint;
                targetPosition = &city.opening.checkpoint->position;
            } else if (vehiclePosition) {
                target = Target::vehicle;
                targetPosition = &*vehiclePosition;
            }
            if (target != city.lastMinimapTarget) {
                city.lastMinimapTarget = target;
                city.minimapTargetStartMs = elapsedMs;
            }
            if (targetPosition) {
                const auto uv = retailMinimapUv(RetailMap::city, (*targetPosition)[0], (*targetPosition)[1]);
                minimapMarker = retailMinimapMarkerOffset(center, uv, right, city.minimapMarkerLimits,
                                                          zoom, city.minimapWidth, city.minimapHeight);
                // AnimObject advances half a sprite tick per retail 60 Hz update.
                const auto routeStart = target == Target::vanDrive ? city.vanDriveMarker :
                    target == Target::elToroDrive ? city.elToroDriveMarker :
                    target == Target::elToroDestination ? city.elToroDestinationMarker :
                    target == Target::delivery ? city.deliveryMarker :
                    target == Target::repairShop ? city.repairShopMarker :
                    target == Target::repairDrive ? city.repairDriveMarker :
                    target == Target::postBasketball ? city.postBasketballMarker :
                    target == Target::basketball ? city.basketballMarker :
                    city.hummerRouteMarker ? city.hummerRouteMarker : city.nextMissionMarker;
                const auto markerMs = routeStart ? city.payphoneGameplayMs.value_or(0) - *routeStart
                    : nextPlacement ? city.payphoneGameplayMs.value_or(0) : elapsedMs - city.minimapTargetStartMs;
                minimapMarkerStep = retailMinimapAnimationStep(city.minimapCheckpointAnimation, markerMs * 0.03);
            }
            if (city.taggedFightScript || city.postBasketballEscapeScript || city.vanProtectionScript) {
                for (const auto& [name, opponent] : city.playback.locoOpponents) {
                    if (!opponent.taggedForKill || opponent.disabled || opponent.health <= 0) continue;
                    const auto& p = opponent.pose.position;
                    const auto uv = retailMinimapUv(RetailMap::city, p[0], p[1]);
                    killTargetMarkers.push_back(retailMinimapMarkerOffset(center, uv, right, city.minimapMarkerLimits,
                                                                   zoom, city.minimapWidth, city.minimapHeight));
                }
            }
            const auto base = city.minimapVertices[1];
            for (size_t i = 0; i < 6; ++i) {
                constexpr std::array<float, 6> x{-1, 1, -1, -1, 1, 1}, y{-1, -1, 1, 1, -1, 1};
                city.vertices[base + i].uv = retailMinimapSample(center, right,
                    {x[i] * city.minimapHalfWidth, y[i] * city.minimapHalfHeight},
                    zoom, city.minimapWidth, city.minimapHeight);
            }
            std::memcpy(static_cast<char*>(mapped) + base * sizeof(CityVertex), city.vertices.data() + base,
                        6 * sizeof(CityVertex));
        }
        VK_CALL(vkUnmapMemory)(vk.device, city.geometryMemory);
    }
    const auto sceneBrightness = city.missionTitleMs ? 0.f :
        city.startup ? city.startup->brightness() :
        postFightMs ? cityTrackBrightness(missionScene->fades, *postFightMs) :
        city.postLcTailMs ? cityTrackBrightness(actorScene->fades, *city.postLcTailMs) :
        payphoneMs ? cityTrackBrightness(city.opening.payphone->cinematicFades, *payphoneMs) :
        cityOpeningBrightness(city.opening, elapsedMs);
    const auto brightness = sceneBrightness *
        (city.missionRevealMs ? retailMissionRevealBrightness(*city.missionRevealMs) : 1.f) *
        (city.nextMissionFadeMs ? 1.f - static_cast<float>(*city.nextMissionFadeMs /
            nextMissionFadeDuration(city)) : 1.f) *
        (city.returnToLcFadeMs ? 1.f - static_cast<float>(std::clamp(*city.returnToLcFadeMs /
            city.opening.payphone->postFight.nextCheckpoint->returnToLcFadeMs, 0., 1.)) : 1.f) *
        (city.hummerArrivalFadeMs ? 1.f - static_cast<float>(std::clamp(*city.hummerArrivalFadeMs /
            hummerArrivalFadeDuration(city), 0., 1.)) : 1.f) *
        (city.payphoneRevealMs
            ? static_cast<float>(*city.payphoneRevealMs / city.opening.payphone->gameplayFadeMs) : 1.f);
    const auto index = beginFrame(vk);
    VkClearValue clear[2]{};
    clear[0].color = {{0.085f * brightness, 0.12f * brightness, 0.17f * brightness, 1.f}};
    clear[1].depthStencil = {1.f, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = city.renderPass;
    begin.framebuffer = city.frames.at(index);
    begin.renderArea = {{0, 0}, size};
    begin.clearValueCount = 2;
    begin.pClearValues = clear;
    VK_CALL(vkCmdBeginRenderPass)(vk.command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VK_CALL(vkCmdBindPipeline)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, city.pipeline);
    const VkDeviceSize vertexOffset = 0;
    VK_CALL(vkCmdBindVertexBuffers)(vk.command, 0, 1, &city.geometry, &vertexOffset);
    VK_CALL(vkCmdBindIndexBuffer)(vk.command, city.geometry, city.indexOffset, VK_INDEX_TYPE_UINT32);
    city.distantDraws = 0;
    if (distantCamera && city.lowDetailFirstSurface < city.lowDetailEndSurface) {
        const auto distantFrustum = cityFrustum(*distantCamera);
        VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    sizeof camera, distantCamera->data());
        for (size_t i = city.lowDetailFirstSurface; i < city.lowDetailEndSurface; ++i) {
            const auto& surface = city.surfaces[i];
            if (surface.bounds && !cityBoundsVisible(*surface.bounds, distantFrustum)) continue;
            if (surface.lodLimit && !surface.lodLimit->visible(cameraEye)) continue;
            const auto descriptor = city.textures.at(surface.texture).descriptor;
            VK_CALL(vkCmdBindDescriptorSets)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, city.layout,
                                             0, 1, &descriptor, 0, nullptr);
            const CityFragmentConstants fragment{surface.tintAndCutoff, brightness};
            VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                        sizeof camera, sizeof fragment, &fragment);
            VK_CALL(vkCmdDrawIndexed)(vk.command, surface.indexCount, 1, surface.firstIndex, 0, 0);
            ++city.distantDraws;
        }
        if (city.distantDraws != 0) {
            VkClearAttachment attachment{VK_IMAGE_ASPECT_DEPTH_BIT};
            attachment.clearValue.depthStencil = {1.f, 0};
            const VkClearRect rect{{{0, 0}, size}, 0, 1};
            VK_CALL(vkCmdClearAttachments)(vk.command, 1, &attachment, 1, &rect);
        }
    }
    VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof camera,
                                camera.data());
    auto boundPipeline = city.pipeline;
    bool drewVehicleMarker = false;
    const auto frustum = cityFrustum(camera);
    const auto outsideCamera = [&](const CityVehiclePose& car) {
        const auto& p = car.position;
        return !cityBoundsVisible({{p[0] - 350, p[1] - 350, p[2] - 50},
                                   {p[0] + 350, p[1] + 350, p[2] + 250}}, frustum);
    };
    if (city.cooperRemovalPending && cooperPose && outsideCamera(*cooperPose)) {
        cooperPose.reset();
        city.locoCooper.reset();
        city.cooperRemovalPending = false;
    }
    if (city.hummerRemovalPending && vehicle && !vehicle->occupied && !vehicle->doorAction && outsideCamera(*vehicle))
        city.hummerRemovalReady = true;
    if (city.playback.payphoneDisablePending && city.playback.payphonePursuer && city.playback.payphoneAttack) {
        const auto& position = city.playback.payphonePursuer->position;
        const auto actor = std::find_if(city.opening.actors.begin(), city.opening.actors.end(), [&](const auto& entry) {
            return entry.payphoneOnly && entry.name == city.playback.payphoneAttack->attacker;
        });
        // Keep the fall visible, then use a conservative bound for the stationary body.
        if (actor != city.opening.actors.end() && actor->defeatAnimation &&
            city.playback.payphoneDefeatMs >= actor->defeatAnimation->first.durationMs &&
            !cityBoundsVisible({{position[0] - 150, position[1] - 150, position[2] - 25},
                                {position[0] + 150, position[1] + 150, position[2] + 200}}, frustum)) {
            city.playback.payphoneDisablePending = false;
            city.playback.payphoneOpponentDisabled = true;
        }
    }
    for (auto& [name, opponent] : city.playback.locoOpponents) {
        if (!opponent.disablePending) continue;
        const auto& position = opponent.pose.position;
        if (!cityBoundsVisible({{position[0] - 150, position[1] - 150, position[2] - 25},
                                {position[0] + 150, position[1] + 150, position[2] + 200}}, frustum)) {
            opponent.disabled = true;
            opponent.disablePending = false;
        }
    }
    const auto regularSurfaceCount = city.vehicleLights.empty() ? city.surfaces.size()
        : city.vehicleLights.front().firstSurface;
    for (size_t i = 0; i < regularSurfaceCount; ++i) {
        if (i >= city.lowDetailFirstSurface && i < city.lowDetailEndSurface) continue;
        const auto& surface = city.surfaces[i];
        const bool cooperSurface = city.cooperActor && surface.payphoneActor == city.cooperActor &&
            city.opening.vehicleActor != city.cooperActor;
        const bool visible = cooperSurface ? cooperPose.has_value() && !surface.vehicleInterior :
            citySurfaceVisible(surface, city.opening, city.playback, actorSceneMs, actorMissionMs, actorScene);
        if (!visible || (surface.bounds && (!cityBoundsVisible(*surface.bounds, frustum) ||
                                (surface.lodLimit && !surface.lodLimit->visible(cameraEye))))) continue;
        const auto atOrAfter = [i](std::optional<size_t> first) { return first && i >= *first; };
        std::optional<bool> marker;
        bool vehicleMarker = false;
        bool arrowMarker = false;
        if (atOrAfter(city.vanSecondDriveFirstSurface))
            marker = updateVanDrive && vanMarkerIndex == 23;
        else if (atOrAfter(city.vanDriveFirstSurface))
            marker = updateVanDrive && vanMarkerIndex == 22;
        else if (atOrAfter(city.elToroDriveFirstSurface))
            marker = updateElToroDrive;
        else if (atOrAfter(city.elToroDestinationFirstSurface))
            marker = updateElToroDestination;
        else if (atOrAfter(city.elToroFirstSurface))
            marker = footMarkerIndex == 19 && updateNextMissionMarker;
        else if (atOrAfter(city.deliveryFirstSurface))
            marker = updateDeliveryMarker;
        else if (atOrAfter(city.repairShopFirstSurface))
            marker = updateRepairShopMarker;
        else if (atOrAfter(city.repairDriveFirstSurface))
            marker = updateRepairDriveMarker;
        else if (atOrAfter(city.postBasketballFirstSurface))
            marker = updatePostBasketballMarker;
        else if (atOrAfter(city.basketballFirstSurface))
            marker = city.basketballMarker.has_value() && !postFightMs;
        else if (atOrAfter(city.cooperNextDriveFirstSurface))
            marker = updateCooperNextMarker;
        else if (atOrAfter(city.postLcWalkFirstSurfaces[2]))
            marker = walkMarker && walkMarker->index == 2;
        else if (atOrAfter(city.postLcWalkFirstSurfaces[1]))
            marker = walkMarker && walkMarker->index == 1;
        else if (atOrAfter(city.postLcWalkFirstSurfaces[0]))
            marker = walkMarker && walkMarker->index == 0;
        else if (atOrAfter(city.postLcDriveFirstSurface))
            marker = city.postLcIntroFinished && !cooperNextDrive && city.hummerRouteMarker;
        else if (atOrAfter(city.returnToLcFirstSurface))
            marker = city.returningToLc && city.hummerRouteMarker;
        else if (atOrAfter(city.weaponShopEntranceFirstSurface))
            marker = city.weaponShopUnlocked && (city.nextMissionMarker || postLc);
        else if (atOrAfter(city.weaponShopApproachFirstSurface)) {
            marker = city.weaponShopApproach && !city.weaponShopUnlocked && city.nextMissionMarker;
            arrowMarker = true;
        } else if (atOrAfter(city.weaponShopDriveFirstSurface))
            marker = city.weaponShopDrive && !city.returningToLc && !city.postLcIntroFinished && city.hummerRouteMarker;
        else if (atOrAfter(city.hummerRouteFirstSurface))
            marker = !city.weaponShopDrive && !city.returningToLc && !city.postLcIntroFinished && !cooperNextDrive && city.hummerRouteMarker;
        else if (atOrAfter(city.nextMissionFirstSurface))
            marker = footMarkerIndex == 3 && city.nextMissionMarker;
        else if (atOrAfter(city.vehicleObjectiveFirstSurface)) {
            marker = updateVehicleMarker;
            vehicleMarker = true;
        } else if (atOrAfter(city.payphoneFirstSurface))
            marker = city.playback.payphoneActive && !city.playback.payphoneReached;
        else if (atOrAfter(city.checkpointFirstSurface)) {
            marker = !city.playback.timeAfterCheckpointMs;
            arrowMarker = true;
        }
        if (marker && (failed || !*marker)) continue;
        const auto wantedPipeline = marker ? city.checkpointPipeline
            : citySurfaceCullBackfaces(surface, city.playback) ? city.vehicleExteriorPipeline : city.pipeline;
        if (wantedPipeline != boundPipeline) {
            VK_CALL(vkCmdBindPipeline)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline);
            boundPipeline = wantedPipeline;
        }
        const auto descriptor = city.textures.at(surface.texture).descriptor;
        VK_CALL(vkCmdBindDescriptorSets)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, city.layout, 0, 1,
                                         &descriptor, 0, nullptr);
        CityFragmentConstants fragment{surface.tintAndCutoff, brightness};
        if (surface.vehicle && surface.payphoneActor.has_value() == city.playback.nextMissionVehicleActive &&
            vehicle && vehicle->exploded) {
            // Vehicle::explode assigns BGRA 25, 25, 35, 255 to the wreck.
            fragment.tintAndCutoff[0] = 35.f / 255;
            fragment.tintAndCutoff[1] = fragment.tintAndCutoff[2] = 25.f / 255;
        }
        if (arrowMarker) fragment.tintAndCutoff[3] = -1;
        VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof camera,
                                    sizeof fragment, &fragment);
        VK_CALL(vkCmdDrawIndexed)(vk.command, surface.indexCount, 1, surface.firstIndex, 0, 0);
        drewVehicleMarker |= vehicleMarker && surface.indexCount != 0;
    }
    if (lightDraws) *lightDraws = 0;
    if (lights) {
        VK_CALL(vkCmdBindPipeline)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, city.explosionPipeline);
        for (size_t i = 0; i < city.vehicleLights.size(); ++i) {
            if (!vehicle->lightEffects.at(i / lights->size())) continue;
            const auto& mesh = city.vehicleLights[i];
            for (size_t s = mesh.firstSurface; s < mesh.firstSurface + mesh.surfaceCount; ++s) {
                const auto& surface = city.surfaces[s];
                if (!surface.indexCount) continue;
                const auto descriptor = city.textures.at(surface.texture).descriptor;
                VK_CALL(vkCmdBindDescriptorSets)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, city.layout, 0, 1,
                                                 &descriptor, 0, nullptr);
                CityFragmentConstants fragment{surface.tintAndCutoff, brightness};
                fragment.tintAndCutoff[3] = -2;
                VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof camera,
                                            sizeof fragment, &fragment);
                VK_CALL(vkCmdDrawIndexed)(vk.command, surface.indexCount, 1, surface.firstIndex, 0, 0);
                if (lightDraws) ++*lightDraws;
            }
        }
    }
    auto caption = city.playback.activeDialogue;
    if (const auto& checkpoint = city.opening.checkpoint; !caption && checkpoint) {
        const auto index = city.opening.dialogue.size();
        const auto after = city.playback.timeAfterCheckpointMs;
        const auto& route = city.opening.payphone;
        if (route && city.playback.payphoneActive && !city.playback.payphoneReached)
            caption = index + 3;
        else if (route && !city.playback.payphoneActive && city.playback.enteredVehicle &&
                 city.playback.timeAfterEntryMs >= route->destination.tutorial.startMs)
            caption = index + 2;
        else if (after && *after >= checkpoint->nextTutorial.startMs && !city.playback.enteredVehicle)
            caption = index + 1;
        else if (!after && elapsedMs >= checkpoint->tutorial.startMs)
            caption = index;
    }
    const auto* dialogueLine = city.playback.activeDialogue
        ? &city.opening.dialogue[*city.playback.activeDialogue] : nullptr;
    if (city.payphoneCinematic) {
        caption.reset();
        dialogueLine = nullptr;
        if (const auto line = city.payphoneCinematic->activeDialogue()) {
            const auto& lines = city.opening.payphone->cinematicDialogue;
            caption = city.missionCaptionBases.at(&city.opening.payphone->postFight) - lines.size() + *line;
            dialogueLine = &lines[*line];
        }
    }
    if (city.postFightCinematic) {
        caption.reset();
        dialogueLine = nullptr;
        if (const auto line = city.postFightCinematic->activeDialogue()) {
            const auto& lines = missionScene->dialogue;
            caption = city.missionCaptionBases.at(missionScene) + *line;
            dialogueLine = &lines[*line];
        }
    }
    const auto overlay = [&](VkDescriptorSet descriptor, const CityCameraMatrix& screen,
                             uint32_t firstVertex, uint32_t vertexCount, VkPipeline pipeline = VK_NULL_HANDLE,
                             float shade = -1, float clipLeft = 0) {
        const CityFragmentConstants fragment{{clipLeft, 1, 1, clipLeft > 0 ? -3.f : -1.f},
                                             shade < 0 ? brightness : shade};
        VK_CALL(vkCmdBindPipeline)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline ? pipeline : city.captionPipeline);
        VK_CALL(vkCmdBindDescriptorSets)(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, city.layout, 0, 1,
                                         &descriptor, 0, nullptr);
        VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof screen,
                                    screen.data());
        VK_CALL(vkCmdPushConstants)(vk.command, city.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof screen,
                                    sizeof fragment, &fragment);
        VK_CALL(vkCmdDraw)(vk.command, vertexCount, 1, firstVertex, 0);
    };
    for (const auto& effect : city.vehicleEffects) {
        const auto count = static_cast<uint32_t>(effect.simulation.particles().size());
        if (count) {
            const auto pipeline = &effect == &city.vehicleEffects.back() ? city.explosionPipeline : city.checkpointPipeline;
            overlay(city.textures.at(effect.texture).descriptor, camera, effect.firstVertex, count * 6, pipeline);
        }
    }
    constexpr CityCameraMatrix screen{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    if (city.payphoneGameplayMs && !city.payphoneRevealMs && !postFightMs && !failed && !city.weaponShopOpen) {
        const auto drawStatusGauge = [&](RetailStatusGauge id, int32_t value, int32_t maximum, bool hideFill) {
            const auto& gauge = city.statusGauges[id];
            if (!gauge.fillWidth) return;
            const float clip = static_cast<float>(retailGaugeFillLeft(gauge.fillWidth, value, maximum)) /
                               gauge.fillWidth;
            for (size_t i = 0; i < gauge.layers.size(); ++i) {
                if (i == 1 && (hideFill || !value)) continue;
                overlay(gauge.layers[i].descriptor, screen, gauge.vertices[i], 6, VK_NULL_HANDLE, 1,
                        i == 1 ? clip : 0);
            }
        };
        drawStatusGauge(lifeGauge, city.playback.playerHealth, CityPlayback::playerMaxHealth, city.healthBlink.hideFill);
        if (city.playback.playerArmor > 0 && !(vehicle && vehicle->occupied))
            drawStatusGauge(armorGauge, city.playback.playerArmor, CityPlayback::playerMaxArmor, false);
        if (city.spyGauge && city.spyWidth) {
            for (size_t i = 0; i < city.spyLayers.size() - 1; ++i)
                overlay(city.spyLayers[i].descriptor, screen, city.spyVertices[i], 6);
            auto cursor = screen;
            cursor[0][3] = static_cast<int>(city.spyWidth * (1.f - *city.spyGauge)) / 240.f;
            overlay(city.spyLayers.back().descriptor, cursor, city.spyVertices.back(), 6);
        }
    }
    if (showMinimap) {
        for (const size_t i : {1u, 0u})
            overlay(city.minimapTextures[i].descriptor, screen, city.minimapVertices[i], 6);
        const auto drawMarker = [&](const std::array<float, 2>& position, size_t step) {
            auto markerTransform = screen;
            markerTransform[0][3] = position[0] / 240;
            markerTransform[1][3] = position[1] / 160;
            overlay(city.minimapCheckpointTextures[step].descriptor, markerTransform,
                    city.minimapCheckpointVertices[step], 6);
        };
        if (minimapMarker) drawMarker(*minimapMarker, minimapMarkerStep);
        if (!killTargetMarkers.empty()) {
            const auto step = retailMinimapAnimationStep(city.minimapCheckpointAnimation, city.payphoneGameplayMs.value_or(elapsedMs) * 0.03);
            for (const auto& marker : killTargetMarkers) drawMarker(marker, step);
        }
        overlay(city.minimapTextures[2].descriptor, screen, city.minimapVertices[2], 6);
    }
    if (city.missionTitleMs && *city.missionTitleMs >= 500) {
        const auto& title = city.missionTitles[city.missionTitleIndex];
        const double tick = (*city.missionTitleMs - 500) * city.retailFrameRate / 2000;
        const auto frame = std::upper_bound(title.frameEnds.begin(), title.frameEnds.end(), tick);
        const auto index = std::min<size_t>(frame - title.frameEnds.begin(), title.textures.size() - 1);
        overlay(title.textures[index].descriptor, screen, city.cinematicBarsVertex, 6, VK_NULL_HANDLE, 1);
        overlay(title.label.descriptor, screen, city.missionTitleLabelVertex, 6, VK_NULL_HANDLE, 1);
    } else if (failed) {
        // AnimObject advances by half a sprite tick per retail game update.
        const auto failureTime = city.playback.failureElapsedMs;
        const double tick = failureTime * city.retailFrameRate / 2000;
        if (!city.failureTextures.empty()) {
            const auto frame = std::upper_bound(city.failureFrameEnds.begin(), city.failureFrameEnds.end(), tick);
            const auto index = std::min<size_t>(frame - city.failureFrameEnds.begin(), city.failureTextures.size() - 1);
            overlay(city.failureTextures[index].descriptor, screen, city.cinematicBarsVertex, 6);
        }
        if (city.failureAnimationFinished(failureTime)) {
            if (city.failureTitle.descriptor)
                overlay(city.failureTitle.descriptor, screen, city.failureTitleVertex, 6);
            if (city.failurePrompt.descriptor && failureTime >= 1000 &&
                std::fmod(failureTime - 1000, 1024) < 512)
                overlay(city.failurePrompt.descriptor, screen, city.failurePromptVertex, 6);
        }
    } else {
        const bool postFightBars = postFightMs && !missionScene->cameras.empty() &&
            *postFightMs >= missionScene->cameras.front().startMs &&
            (!missionScene->followCameraMs ||
             *postFightMs < double(*missionScene->followCameraMs) + missionScene->followCameraTransitionMs);
        if ((dialogueLine || (payphoneMs && *payphoneMs >= 500) || postFightBars ||
             (city.startup && city.startup->showsBars())) &&
            city.cinematicBars.descriptor)
            overlay(city.cinematicBars.descriptor, screen, city.cinematicBarsVertex, 6);
        auto captionVertex = city.captionVertex;
        if (dialogueLine) {
            const auto portrait = dialogueLine->portrait;
            if (const auto found = city.portraits.find(portrait); found != city.portraits.end()) {
                overlay(found->second.descriptor, screen, city.cinematicBarsVertex, 6);
                captionVertex = city.dialogueCaptionVertex;
            }
        }
        if (caption) overlay(city.captions[*caption].descriptor, screen, captionVertex, 6);
        else if (city.missionRewardMs && *city.missionRewardMs < city.missionObjectiveEndMs() &&
                 !postFightMs && !city.weaponShopOpen &&
                 city.missionCompleteObjectives[city.missionRewardIndex].descriptor) {
            const double elapsed = *city.missionRewardMs;
            const auto holdEnd = city.missionObjectiveHoldEndMs();
            const bool closing = elapsed >= holdEnd;
            const auto& animation = city.missionObjectiveAnimations[closing ? 1 : 0];
            const auto tick = (elapsed - (closing ? holdEnd : 0.)) * city.retailFrameRate / 2000;
            const auto end = std::upper_bound(animation.frameEnds.begin(), animation.frameEnds.end(), tick);
            const auto frame = std::min<size_t>(end - animation.frameEnds.begin(), animation.textures.size() - 1);
            overlay(animation.textures[frame].descriptor, screen, city.cinematicBarsVertex, 6, VK_NULL_HANDLE, 1);
            if (!closing && end == animation.frameEnds.end())
                overlay(city.missionCompleteObjectives[city.missionRewardIndex].descriptor,
                        screen, city.missionCompleteObjectiveVertex, 6,
                        VK_NULL_HANDLE, 1);
        }
        else if (city.hummerTutorialVisible && city.hummerTutorial.descriptor)
            overlay(city.hummerTutorial.descriptor, screen, city.captionVertex, 6);
        else if (city.weaponShopTutorialVisible && city.weaponShopTutorial.descriptor)
            overlay(city.weaponShopTutorial.descriptor, screen, city.captionVertex, 6);
        else if (city.returningToLc && city.hummerRouteScript)
            overlay(city.returnToLcObjectives[city.hummerRouteMarker ? 1 : 0].descriptor,
                    screen, city.captionVertex, 6);
        else if (city.repairTutorialVisible && city.repairTutorial.descriptor)
            overlay(city.repairTutorial.descriptor, screen, city.captionVertex, 6);
        else if (city.vanProtectionScript && city.vanProtectObjective.descriptor)
            overlay(city.vanProtectObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.vanDriveScript && city.vanDriveObjective.descriptor)
            overlay(city.vanDriveObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.vanAcquisitionScript && city.vanMissionObjective.descriptor)
            overlay(city.vanMissionObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.elToroDriveScript && city.elToroDriveObjective.descriptor)
            overlay(city.elToroDriveObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.elToroDestinationScript && city.elToroObjective.descriptor)
            overlay(city.elToroObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.taggedFightScript && city.missionTitleIndex == 7 && city.elToroFightObjective.descriptor)
            overlay(city.elToroFightObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.deliveryScript && city.deliveryObjective.descriptor)
            overlay(city.deliveryObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.repairDriveScript && city.repairDriveObjective.descriptor)
            overlay(city.repairDriveObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.postBasketballEscapeScript && city.postBasketballAmbushObjective.descriptor)
            overlay(city.postBasketballAmbushObjective.descriptor, screen, city.captionVertex, 6);
        else if ((city.postBasketballRouteScript || city.postBasketballVehicleScript) &&
                 city.postBasketballObjective.descriptor)
            overlay(city.postBasketballObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.basketballRouteScript && city.brawlNextObjective.descriptor)
            overlay(city.brawlNextObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.basketballTutorialVisible && city.basketballTutorial.descriptor)
            overlay(city.basketballTutorial.descriptor, screen, city.captionVertex, 6);
        else if (city.taggedFightScript && city.missionTitleIndex == 5 && city.basketballFightObjective.descriptor)
            overlay(city.basketballFightObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.taggedFightScript && city.cooperNextFightObjective.descriptor)
            overlay(city.cooperNextFightObjective.descriptor, screen, city.captionVertex, 6);
        else if (cooperNextDrive && city.hummerRouteMarker && city.cooperNextDriveObjective.descriptor)
            overlay(city.cooperNextDriveObjective.descriptor, screen, city.captionVertex, 6);
        else if (!cooperNextDrive && city.postLcIntroFinished && city.hummerRouteScript && city.postLcDriveObjective.descriptor)
            overlay(city.postLcDriveObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.locoCooperScript && city.locoCooperObjective.descriptor)
            overlay(city.locoCooperObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.locoGuardScript && city.locoNextObjective.descriptor)
            overlay(city.locoNextObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.postLcFightScript && city.postLcFightObjective.descriptor)
            overlay(city.postLcFightObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.postLcWalkScript && city.postLcConfrontationObjective.descriptor)
            overlay(city.postLcConfrontationObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.postLcAmbushFinished && city.postLcAmbushObjective.descriptor)
            overlay(city.postLcAmbushObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.weaponShopDrive && city.hummerRouteScript && city.weaponShopObjective.descriptor)
            overlay(city.weaponShopObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.playback.nextMissionVehicleActive && !city.playback.nextMissionVehicleAcquired &&
                 city.nextVehicleObjective.descriptor)
            overlay(city.nextVehicleObjective.descriptor, screen, city.captionVertex, 6);
        else if (city.payphoneGameplayMs && !city.payphoneRevealMs && city.payphoneObjective.descriptor &&
                 city.payphoneLockScript)
            overlay(city.payphoneObjective.descriptor, screen, city.captionVertex, 6);
        if (city.startup && city.startup->canSkip())
            overlay(city.cinematicSkip.descriptor, screen, city.cinematicSkipVertex, 6);
    }
    if (const auto bearing = failed ? std::nullopt : city.playback.payphoneBearing(city.opening)) {
        const auto cosine = std::cos(*bearing), sine = std::sin(*bearing);
        const auto aspect = float(size.width) / size.height;
        const CityCameraMatrix arrow{{{cosine/aspect, -sine/aspect, 0, 0}, {sine, cosine, 0, -0.85f},
                                       {0, 0, 1, 0}, {0, 0, 0, 1}}};
        overlay(city.textures.at("").descriptor, arrow, city.routeArrowVertex, 3);
    }
    if (city.weaponShopOpen) {
        const auto selected = city.weaponShopSelection;
        const auto& panel = city.weaponShopPurchaseDenied ? city.weaponShopInsufficientCash :
            city.weaponShopConfirmPurchase ? city.weaponShopConfirmation[selected] : city.weaponShopInventory[selected];
        overlay(panel.descriptor, screen, city.weaponShopInventoryVertex, 6);
    }
    const auto drawCash = [&](std::string_view label, uint32_t x, float y) {
        auto glyphTransform = screen;
        glyphTransform[1][3] = y / 160.f;
        for (const char character : label) {
            if (character == ' ') {
                x += 8;
                continue;
            }
            const auto glyph = retailCashCharacters.find(character);
            glyphTransform[0][3] = x / 240.f;
            overlay(city.cashGlyphs[glyph].descriptor, glyphTransform, city.cashGlyphVertices[glyph], 6,
                    VK_NULL_HANDLE, 1);
            x += city.cashGlyphWidths[glyph];
        }
    };
    if (city.repairShop == CityRender::RepairShop::confirm) {
        overlay(city.repairPaymentPrompt.descriptor, screen, city.weaponShopInventoryVertex, 6);
        if (city.cashGlyphs[0].descriptor) drawCash(formatRetailCash(city.repairQuote, city.cashLanguage), 196, 152.f);
    }
    if (city.cashGlyphs[0].descriptor && !failed &&
        (city.weaponShopOpen || (city.payphoneGameplayMs && !city.payphoneRevealMs && !postFightMs))) {
        if (city.displayedCash != city.playback.playerCash) {
            city.displayedCash = city.playback.playerCash;
            city.cashLabel = formatRetailCash(city.displayedCash, city.cashLanguage);
        }
        drawCash(city.cashLabel, 6, city.weaponShopOpen ? 8.f : 97.f);
    }
    if (city.missionRewardMs && *city.missionRewardMs < city.missionCashRewardEndMs() &&
        !failed && !postFightMs && !city.weaponShopOpen && city.missionRewardCash[city.missionRewardIndex].descriptor) {
        const double tick = *city.missionRewardMs * city.retailFrameRate / 2000;
        const auto end = std::upper_bound(city.missionRewardFrameEnds.begin(), city.missionRewardFrameEnds.end(), tick);
        const auto frame = std::min<size_t>(end - city.missionRewardFrameEnds.begin(), city.missionRewardFrames.size() - 1);
        overlay(city.missionRewardFrames[frame].descriptor, screen, city.cinematicBarsVertex, 6, VK_NULL_HANDLE, 1);
        if (end == city.missionRewardFrameEnds.end())
            overlay(city.missionRewardCash[city.missionRewardIndex].descriptor,
                    screen, city.cinematicBarsVertex, 6, VK_NULL_HANDLE, 1);
    }
    VK_CALL(vkCmdEndRenderPass)(vk.command);
    finishFrame(vk, index, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    return drewVehicleMarker;
}

class StartupFrame final : public StateBase {
public:
    explicit StartupFrame(std::function<void()> draw, double duration = 0)
        : drawFrame(std::move(draw)), remaining(duration) {}

    void draw(const StateMachine*) override { drawFrame(); }
    void update(StateMachine* machine) override { remaining -= machine->getTimeStamp(); }
    bool finished() const { return remaining <= 0; }

private:
    std::function<void()> drawFrame;
    double remaining;
};

Image blendSplash(const SplashFrames& splash, unsigned opacity)
{
    if (!opacity) return splash.background;
    Image frame = splash.background;
    for (size_t i = 0; i < frame.bgra.size(); ++i) {
        const auto foreground = splash.outgoing.bgra[i];
        if (!(foreground >> 24)) continue;
        const auto background = frame.bgra[i];
        uint32_t pixel = 0xff000000;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            const auto front = (foreground >> shift) & 255;
            const auto back = (background >> shift) & 255;
            pixel |= ((front * opacity + back * (256 - opacity) + 128) >> 8) << shift;
        }
        frame.bgra[i] = pixel;
    }
    return frame;
}

void presentImages(HWND window, const StartupLogo* logo, IntroMovie* movie, const Image& image, bool smokeTest,
                   const SplashFrames* splash = nullptr, Win32TouchInput* touch = nullptr,
                   ResourceFileSystem* assets = nullptr, RetailLanguage language = RetailLanguage::english,
                   bool openingSmokeTest = false)
{
    ShowWindow(window, SW_SHOW);
    Vulkan vk;
    openDevice(vk, window);
    const auto info = swapchainInfo(vk, window, logo != nullptr);
    check(VK_CALL(vkCreateSwapchainKHR)(vk.device, &info, nullptr, &vk.swapchain), "vkCreateSwapchainKHR");
    const VkExtent2D canvas{image.width, image.height};
    VkExtent2D textureSize = canvas;
    if (logo) {
        textureSize.width = std::max(textureSize.width, logo->atlas.width);
        textureSize.height = std::max(textureSize.height, logo->atlas.height);
    }
    if (movie) {
        textureSize.width = std::max(textureSize.width, movie->width());
        textureSize.height = std::max(textureSize.height, movie->height());
    }
    prepareUpload(vk, info.imageFormat, textureSize);
    prepareFrames(vk);
    const VkRect2D viewport{{0, 0}, info.imageExtent};
    const auto draw = [&](const Image& frame, VkExtent2D logicalSize, VkRect2D area,
                          const std::vector<SpriteRegion>& regions, const VkClearColorValue* clear) {
        uploadImage(vk, info, frame);
        present(vk, {frame.width, frame.height}, logicalSize, area, regions, clear);
    };
    VkPhysicalDeviceProperties adapter{};
    VK_CALL(vkGetPhysicalDeviceProperties)(vk.gpu, &adapter);
    const auto drawFull = [&](const Image& frame) {
        draw(frame, canvas, viewport, {{0, 0, 0, 0, frame.width, frame.height}}, nullptr);
    };
    const auto drawImage = [&] {
        drawFull(image);
        std::cout << "Presented image with x86 Vulkan on " << adapter.deviceName << ".\n" << std::flush;
    };
    // Application::LogoUpdate counts 100 frames. Use the retail game loop's frame rate.
    double logoDuration = 0;
    int32_t retailFrameRate = 30;
    if (logo) {
        const auto system = readRetailConstants(*assets, "system_pycst.bin");
        retailFrameRate = retailInteger(system, "GameLoop", "FramePerSecond");
        if (retailFrameRate <= 0) throw std::runtime_error("Invalid retail game loop frame rate.");
        logoDuration = 100.0 * 1000 / retailFrameRate;
    }
    StateMachine machine;
    StartupFrame* logoFrame = nullptr;
    std::optional<MovieFrame> nextFrame;
    std::optional<MovieFrame> currentFrame;
    double movieTime = 0;
    unsigned displayedFrames = 0;
    bool introFinished = false;
    std::unique_ptr<MenuRuntime> menu;
    std::unique_ptr<CityRender> cityRender;
    double nextMenuFrame = 0;
    std::optional<double> fadeTime;
    size_t shownFadeFrame = 0;
    const auto showSplash = [&] {
        machine._switchState(new StartupFrame(drawImage));
        machine.draw();
    };
    const auto showMovieFrame = [&] {
        const auto& pixels = currentFrame->image;
        const double scale = std::min(double(viewport.extent.width) / pixels.width,
                                      double(viewport.extent.height) / pixels.height);
        const auto width = std::max(1u, uint32_t(pixels.width * scale));
        const auto height = std::max(1u, uint32_t(pixels.height * scale));
        const VkRect2D area{{int32_t((viewport.extent.width - width) / 2),
                             int32_t((viewport.extent.height - height) / 2)}, {width, height}};
        const VkClearColorValue black{{0.f, 0.f, 0.f, 1.f}};
        draw(pixels, {pixels.width, pixels.height}, area, {{0, 0, 0, 0, pixels.width, pixels.height}}, &black);
        if (++displayedFrames == 1)
            std::cout << "Presented first intro.mp4 frame through Vulkan.\n" << std::flush;
    };
    if (logo) {
        logoFrame = new StartupFrame([&] {
            const VkClearColorValue white{{1.f, 1.f, 1.f, 1.f}};
            draw(logo->atlas, canvas, viewport, logo->regions, &white);
            std::cout << "Presented Gameloft startup sprite.\n" << std::flush;
        }, logoDuration);
        machine._switchState(logoFrame);
    } else {
        machine._switchState(new StartupFrame(drawImage));
    }
    machine.draw();
    const auto update = [&](double milliseconds, bool scriptedPress = false) {
        machine.update(milliseconds);
        if (cityRender) {
            if (!smokeTest) touch->pollGamepad(window);
            auto& playback = cityRender->playback;
            if (cityRender->hummerRemovalReady) {
                playback.vehicle.reset();
                playback.postLcVehicleActive = false;
                cityRender->hummerRemovalReady = false;
                cityRender->hummerRemovalPending = false;
            }
            const auto previous = playback.elapsedMs;
            const auto now = std::chrono::steady_clock::now();
            const auto delta = openingSmokeTest ? milliseconds :
                std::chrono::duration<double, std::milli>(now - cityRender->lastTick).count();
            cityRender->lastTick = now;
            const bool pressed = touch->takePress() || scriptedPress;
            bool skip = touch->takeCutsceneSkip();
            const bool interact = touch->takeVehiclePress();
            using RepairShop = CityRender::RepairShop;
            if (cityRender->repairShop == RepairShop::confirm || cityRender->repairShop == RepairShop::repairing) {
                const auto navigation = touch->takeMenuNavigation();
                touch->clearMenuPointerEvents();
                if (cityRender->repairShop == RepairShop::confirm) {
                    if (navigation.back || skip) {
                        activateCarDelivery(*cityRender);
                        touch->stopMenuPointer();
                    } else if (navigation.accept) {
                        playback.playerCash -= cityRender->repairQuote;
                        if (cityRender->achievements)
                            cityRender->achievements->recordRepair(cityRender->repairQuote);
                        cityRender->repairElapsedMs = 0;
                        cityRender->repairShop = RepairShop::repairing;
                    }
                } else {
                    const auto before = cityRender->repairElapsedMs;
                    cityRender->repairElapsedMs += delta;
                    if (before <= 250 && cityRender->repairElapsedMs > 250) {
                        auto& car = *playback.vehicle;
                        car.health = car.maxHealth;
                        car.destroyedMs = 0;
                        car.frontDeformation = 0;
                        car.rearDeformation = 0;
                    }
                    if (cityRender->repairElapsedMs > 1500) {
                        activateCarDelivery(*cityRender);
                        touch->stopMenuPointer();
                    }
                }
                presentCity(*cityRender, info.imageExtent);
                return false;
            }
            if (cityRender->weaponShopOpen) {
                const auto navigation = touch->takeMenuNavigation();
                touch->clearMenuPointerEvents();
                if (cityRender->weaponShopPurchaseDenied) {
                    if (navigation.accept || navigation.back)
                        cityRender->weaponShopPurchaseDenied = false;
                } else if (cityRender->weaponShopConfirmPurchase) {
                    if (navigation.back || navigation.accept) cityRender->weaponShopConfirmPurchase = false;
                    if (navigation.accept && !navigation.back) {
                        const auto selected = cityRender->weaponShopSelection;
                        const auto price = cityRender->weaponShopServicePrices[selected];
                        cityRender->weaponShopPurchaseDenied =
                            !playback.buyShopService(selected == 0 ? CityShopService::health : CityShopService::vest, price);
                        if (!cityRender->weaponShopPurchaseDenied && cityRender->achievements)
                            cityRender->achievements->recordCashSpent(static_cast<int32_t>(price));
                    }
                } else if (navigation.back || interact || skip) {
                    cityRender->weaponShopOpen = false;
                    cityRender->weaponShopSelection = 0;
                    touch->stopMenuPointer();
                } else {
                    if (navigation.step != 0) cityRender->weaponShopSelection = 1 - cityRender->weaponShopSelection;
                    if (navigation.accept) cityRender->weaponShopConfirmPurchase = true;
                }
                touch->takeTargetLockPress();
                touch->takePunchPress();
                touch->takeRadioStep();
                touch->takeCameraDrag();
                touch->takeCameraTaps();
                touch->takeCameraRecenter();
                presentCity(*cityRender, info.imageExtent);
                return false;
            }
            if (cityRender->missionRevealMs) {
                *cityRender->missionRevealMs += delta;
                if (retailMissionRevealBrightness(*cityRender->missionRevealMs) == 1.f)
                    cityRender->missionRevealMs.reset();
            }
            if (cityRender->payphoneRevealMs) {
                *cityRender->payphoneRevealMs += delta;
                if (*cityRender->payphoneRevealMs >= cityRender->opening.payphone->gameplayFadeMs)
                    cityRender->payphoneRevealMs.reset();
            }
            if (cityRender->payphoneGameplayMs) *cityRender->payphoneGameplayMs += delta;
            if (cityRender->returnToLcFadeMs) {
                const auto& mission = *cityRender->opening.payphone->postFight.nextCheckpoint;
                const auto exitMs = double(mission.returnToLcFadeMs) + mission.returnToLcWaitMs;
                const auto previousFade = *cityRender->returnToLcFadeMs;
                *cityRender->returnToLcFadeMs += delta;
                if (previousFade < exitMs && *cityRender->returnToLcFadeMs >= exitMs)
                    playback.scriptedVehicleExit = true;
            }
            if (cityRender->missionRewardMs && !cityRender->postFightCinematic) {
                *cityRender->missionRewardMs += delta;
                if (*cityRender->missionRewardMs >=
                    std::max(cityRender->missionCashRewardEndMs(), cityRender->missionObjectiveEndMs()))
                    cityRender->missionRewardMs.reset();
            }
            if (cityRender->nextMissionFadeMs) {
                *cityRender->nextMissionFadeMs += delta;
                const auto fadeMs = nextMissionFadeDuration(*cityRender);
                if (*cityRender->nextMissionFadeMs >= fadeMs) {
                    cityRender->nextMissionFadeMs.reset();
                    cityRender->missionTitleMs = -delta;
                }
            }
            const auto playCue = [&](const CityAudioCue& cue) {
                if (!cityRender->audio) return;
                try {
                    if (cue.action == CityAudioAction::playPriorityLoop && cityRender->radio)
                        cityRender->radio->stop();
                    cityRender->audio->playScriptCue(cue);
                } catch (const std::runtime_error& error) {
                    std::cerr << error.what() << '\n';
                }
            };
            if (cityRender->startup) {
                const auto& opening = cityRender->opening;
                for (const auto& event : touch->menuPointerEvents()) {
                    if (event.motion) continue;
                    const bool hit = cityRender->startup->canSkip() && event.x >= skipLeft && event.x < 480 &&
                        event.y >= 0 && event.y < skipHeight;
                    if (event.down) cityRender->skipArmed = hit;
                    else skip |= std::exchange(cityRender->skipArmed, false) && hit;
                }
                touch->clearMenuPointerEvents();
                if (cityRender->startup->advance(opening, delta, playCue, skip)) {
                    playback.elapsedMs = opening.handoffMs;
                    if (opening.missionTitle) {
                        if (cityRender->startup->beginMission(opening.nextScript) != opening.missionTitle->id)
                            throw std::runtime_error("Opening script selected a different mission title.");
                        cityRender->missionTitleMs = 0;
                    }
                    cityRender->startup.reset();
                    touch->stopMenuPointer();
                }
            } else if (cityRender->missionTitleMs) {
                *cityRender->missionTitleMs += delta;
                const auto completeMs = 500 + std::min(3000.,
                    cityRender->missionTitles[cityRender->missionTitleIndex].frameEnds.back() *
                        2000. / cityRender->retailFrameRate);
                if (*cityRender->missionTitleMs >= completeMs) {
                    cityRender->missionTitleMs.reset();
                    if (cityRender->missionTitleIndex != 0) {
                        const auto& next = *cityRender->opening.payphone->postFight.nextCheckpoint;
                        if (cityRender->missionTitleIndex == 8) {
                            cityRender->activeMissionCinematic = &next.vanMissionIntro;
                        } else if (cityRender->missionTitleIndex == 7) {
                            cityRender->activeMissionCinematic = &next.elToroIntro;
                        } else if (cityRender->missionTitleIndex == 6) {
                            cityRender->activeMissionCinematic = &next.postBasketballIntro;
                        } else if (cityRender->missionTitleIndex == 5) {
                            cityRender->activeMissionCinematic = &next.brawlNextIntro;
                        } else if (cityRender->missionTitleIndex >= 3) {
                            const auto& scene = cityRender->missionTitleIndex == 4 ? next.cooperNextIntro : next.locoNextIntro;
                            cityRender->activeMissionCinematic = &scene;
                            cityRender->hummerRemovalPending = cityRender->hummerRemovalReady = false;
                            cityRender->locoCooper.reset();
                            cityRender->cooperRemovalPending = false;
                            const auto& actor = bindHummerGeometry(*cityRender);
                            const auto placement = std::find_if(scene.placements.begin(), scene.placements.end(),
                                [&](const auto& cue) {
                                    return cue.visible && cue.actor == next.targetVehicle;
                                });
                            if (placement == scene.placements.end() ||
                                !playback.spawnVehicle(actor, cityRender->terrain, placement->position,
                                                       placement->rotationDegrees[2]))
                                throw std::runtime_error("Missing retail mission Hummer placement.");
                            playback.postLcVehicleActive = true;
                        } else cityRender->activeMissionCinematic =
                            cityRender->missionTitleIndex == 2 ? &next.postLcIntro : &next.cinematic;
                        cityRender->postFightCinematic.emplace();
                    } else cityRender->missionRevealMs = 0;
                }
            } else if (playback.missionFailure) {
                const auto& opening = cityRender->opening;
                const bool animationFinished = cityRender->failureAnimationFinished(playback.failureElapsedMs + delta);
                if (playback.acknowledgeFailure(opening, delta, pressed, animationFinished)) {
                    if (cityRender->audio) cityRender->audio->stopAll();
                    cityRender->fireSoundActive = false;
                    if (cityRender->missionTitleIndex >= 4) {
                        cityRender->basketballTutorialVisible = false;
                        cityRender->basketballRouteScript.reset();
                        cityRender->basketballMarker.reset();
                        cityRender->postBasketballRouteScript.reset();
                        cityRender->postBasketballMarker.reset();
                        cityRender->postBasketballVehicleScript.reset();
                        cityRender->postBasketballEscapeScript.reset();
                        cityRender->elToroDestinationScript.reset();
                        cityRender->elToroDestinationMarker.reset();
                        cityRender->elToroDriveScript.reset();
                        cityRender->elToroDriveMarker.reset();
                        cityRender->vanAcquisitionScript.reset();
                        cityRender->vanDriveScript.reset();
                        cityRender->vanDriveMarker.reset();
                        cityRender->vanSecondDrive = false;
                        cityRender->vanReentryScript.reset();
                        cityRender->vanProtectionScript.reset();
                        cityRender->vanHandshakeScript.reset();
                        cityRender->deliveryScript.reset();
                        cityRender->deliveryMarker.reset();
                        cityRender->repairDriveScript.reset();
                        cityRender->repairDriveMarker.reset();
                        cityRender->repairTutorialVisible = false;
                        cityRender->repairShop = CityRender::RepairShop::inactive;
                        cityRender->repairShopMarker.reset();
                        cityRender->taggedFightScript.reset();
                        cityRender->taggedFightNextScene = nullptr;
                        playback.locoOpponents.clear();
                        playback.locoLockTarget.reset();
                        playback.playerPunchMs.reset();
                        cityRender->pendingHummerCinematic = nullptr;
                        cityRender->hummerArrivalFadeMs.reset();
                        cityRender->hummerRouteScript.reset();
                        cityRender->hummerRouteMarker.reset();
                        playback.postLcVehicleActive = false;
                        activatePostLcCheckpoint(*cityRender);
                    } else if (cityRender->locoGuardScript) {
                        cityRender->locoGuardScript.reset();
                        playback.locoOpponents.clear();
                        playback.locoLockTarget.reset();
                        playback.playerPunchMs.reset();
                        playback.postLcVehicleActive = false;
                        activatePostLcCheckpoint(*cityRender);
                    } else if (cityRender->postLcIntroFinished) activatePostLcDrive(*cityRender);
                    else if (playback.nextMissionVehicleActive) activateNextMissionVehicle(*cityRender);
                    else {
                        cityRender->nextAudio = std::lower_bound(opening.audio.begin(), opening.audio.end(), opening.handoffMs,
                            [](const CityAudioCue& cue, uint32_t time) { return cue.timeMs < time; }) - opening.audio.begin();
                    }
                    cityRender->restoreVehicleGeometry = !cityRender->vehicleVertices.empty();
                    cityRender->lastMinimapTarget.reset();
                    for (auto& effect : cityRender->vehicleEffects) {
                        effect.elapsedMs = 0;
                        effect.simulation = {};
                    }
                }
            } else playback.advance(cityRender->opening, delta, pressed);
            if (cityRender->audio && !cityRender->missionTitleMs) {
                const auto& cues = cityRender->opening.audio;
                while (cityRender->nextAudio < cues.size() &&
                       cues[cityRender->nextAudio].timeMs <= playback.elapsedMs) {
                    playCue(cues[cityRender->nextAudio++]);
                }
                try {
                    cityRender->audio->update();
                } catch (const std::runtime_error& error) {
                    std::cerr << error.what() << '\n';
                }
            }
            const bool lockPressed = touch->takeTargetLockPress();
            const bool punchPressed = touch->takePunchPress();
            const int radioStep = touch->takeRadioStep();
            const auto cameraDrag = touch->takeCameraDrag();
            const auto cameraTaps = touch->takeCameraTaps();
            const bool recenterCamera = touch->takeCameraRecenter();
            const auto movement = cityRender->opening.movementMs;
            if (playback.payphoneReached && !playback.scriptedVehicleExit &&
                !(playback.vehicle && playback.vehicle->doorAction) &&
                !cityRender->payphoneCinematic && !cityRender->payphoneGameplayMs)
                cityRender->payphoneCinematic.emplace();
            const bool cinematic = cityRender->payphoneCinematic.has_value();
            if (cinematic && cityRender->payphoneCinematic->advance(*cityRender->opening.payphone,
                                                                   delta, pressed, playCue)) {
                const auto& route = *cityRender->opening.payphone;
                if (const auto* placement = cityPayphonePlacement(route, "Player", route.cinematicDurationMs);
                    placement && placement->visible) {
                    playback.player = CityPlayerPose{placement->position, placement->rotationDegrees[2]};
                    playback.player->followCameraYaw = placement->rotationDegrees[2];
                    if (const auto ground = cityRender->terrain.groundContact(placement->position)) {
                        playback.player->position[2] = ground->height;
                        playback.player->groundTriangle = ground->triangle;
                    }
                }
                cityRender->payphoneCinematic.reset();
                cityRender->payphoneGameplayMs = route.cinematicDurationMs;
                if (route.gameplayFadeMs) cityRender->payphoneRevealMs = 0;
                const CityScriptCommand lock{108, route.lockCommandPayload};
                cityRender->payphoneLockTarget = cityPayphonePlacement(route,
                    decodeCityWaitForLock(lock), route.cinematicDurationMs);
                if (cityRender->payphoneLockTarget) {
                    auto& script = cityRender->payphoneLockScript.emplace(
                        CityScriptProgram{{lock, {29, route.attackCommandPayload}, {87, route.killedCommandPayload},
                                           {23, route.disableCommandPayload}}});
                    script.unlock(0, 0, 0);
                }
            }
            if (cityRender->payphoneLockTarget && !cityRender->payphoneRevealMs) {
                if (lockPressed && playback.playerHealth > 0 && !(playback.vehicle && playback.vehicle->occupied))
                    cityRender->payphoneTargetLocked = !cityRender->payphoneTargetLocked;
                if (cityRender->payphoneLockScript) {
                    RetailMissionHooks mission;
                    mission.actorLocked = [&](std::string_view) { return cityRender->payphoneTargetLocked; };
                    mission.actorKilled = [&](std::string_view target) {
                        return target == cityRender->payphoneLockTarget->actor && playback.payphoneOpponentHealth == 0;
                    };
                    mission.clearKilledTarget = [&](std::string_view) {
                        cityRender->payphoneLockMarker = nullptr;
                        cityRender->payphoneTargetLocked = false;
                        cityRender->payphoneLockTarget = nullptr;
                    };
                    mission.actorAttack = [&](const CityActorAttack& attack) { playback.payphoneAttack = attack; };
                    mission.disableActor = [&](std::string_view actor, bool immediate) {
                        if (!playback.payphoneAttack || actor != playback.payphoneAttack->attacker) return;
                        playback.payphoneDisablePending = !immediate;
                        playback.payphoneOpponentDisabled = immediate;
                    };
                    mission.markLockTarget = [&](std::string_view, bool marked) {
                        cityRender->payphoneLockMarker = marked ? cityRender->payphoneLockTarget : nullptr;
                    };
                    auto& script = *cityRender->payphoneLockScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {},
                                   false, nullptr, {}, &mission);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->payphoneLockScript.reset();
                        cityRender->activeMissionCinematic = &cityRender->opening.payphone->postFight;
                        cityRender->postFightCinematic.emplace();
                    }
                }
            }
            const bool pendingBriefing = cityRender->pendingHummerCinematic && cityRender->opening.payphone &&
                cityRender->opening.payphone->postFight.nextCheckpoint &&
                cityRender->pendingHummerCinematic ==
                    &cityRender->opening.payphone->postFight.nextCheckpoint->postBasketballBriefing;
            if (pendingBriefing && !playback.missionFailure) {
                auto& fade = cityRender->hummerArrivalFadeMs;
                fade = fade.value_or(0) + delta;
                if (*fade >= hummerArrivalFadeDuration(*cityRender) && playback.vehicle && playback.vehicle->occupied)
                    playback.scriptedVehicleExit = true;
            }
            if (cityRender->pendingHummerCinematic && !playback.missionFailure && !playback.scriptedVehicleExit &&
                !(playback.vehicle && (playback.vehicle->occupied || playback.vehicle->doorAction))) {
                bool fadeComplete = true;
                if (const auto duration = hummerArrivalFadeDuration(*cityRender)) {
                    auto& fade = cityRender->hummerArrivalFadeMs;
                    if (!fade) fade = 0;
                    else if (!pendingBriefing) *fade += delta;
                    fadeComplete = *fade >= duration;
                }
                if (fadeComplete) {
                    if (cityRender->returnToLcFadeMs) {
                        cityRender->returnToLcFadeMs.reset();
                        playback.missionConditions.clear();
                    }
                    cityRender->hummerArrivalFadeMs.reset();
                    cityRender->activeMissionCinematic = std::exchange(cityRender->pendingHummerCinematic, nullptr);
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    if (cityRender->activeMissionCinematic == &route.elToroClosing)
                        playback.missionConditions.clear();
                    if (cityRender->activeMissionCinematic == &route.cooperNextArrival && playback.vehicle) {
                        auto& vehicle = *playback.vehicle;
                        const auto& checkpoint = *route.cooperNextDriveCheckpoint;
                        vehicle.position = checkpoint.position;
                        vehicle.yaw = checkpoint.rotationDegrees[2];
                        vehicle.speed = 0;
                        vehicle.groundTriangle.reset();
                    }
                    cityRender->postFightCinematic.emplace();
                }
            }
            if (cityRender->locoCooperPending && !playback.scriptedVehicleExit &&
                !(playback.vehicle && (playback.vehicle->occupied || playback.vehicle->doorAction))) {
                cityRender->locoCooperPending = false;
                cityRender->activeMissionCinematic = &cityRender->opening.payphone->postFight.nextCheckpoint->locoCooperIntro;
                cityRender->postFightCinematic.emplace();
            }
            const bool postFight = cityRender->postFightCinematic.has_value();
            if (postFight) {
                const auto& handoff = cityRender->opening.payphone->postFight;
                const auto& scene = *cityRender->activeMissionCinematic;
                playback.payphoneDefeatMs += delta;
                auto& cinematicPlayback = *cityRender->postFightCinematic;
                const auto before = cinematicPlayback.elapsedMs();
                const bool finished = cinematicPlayback.advance(scene, delta, pressed, playCue);
                for (const auto& [time, actor] : scene.deferredDisables) {
                    if (cinematicPlayback.elapsedMs() < time || cinematicPlayback.activeDialogue()) continue;
                    const auto found = playback.locoOpponents.find(actor);
                    if (found != playback.locoOpponents.end() && !found->second.disabled)
                        found->second.disablePending = true;
                    if (playback.vehicle && cityRender->opening.vehicleActor &&
                        cityRender->opening.actors.at(*cityRender->opening.vehicleActor).name == actor)
                        cityRender->hummerRemovalPending = true;
                }
                if (&scene == &handoff && handoff.missionOverMs &&
                    cinematicPlayback.elapsedMs() >= *handoff.missionOverMs && cityRender->opening.missionTitle)
                    completeOpeningMission(*cityRender, 0, *cityRender->opening.missionTitle);
                if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->basketballClosing) {
                    const auto& route = *handoff.nextCheckpoint;
                    if (cinematicPlayback.elapsedMs() >= route.basketballTutorialHideMs)
                        cityRender->basketballTutorialVisible = false;
                    if (cinematicPlayback.elapsedMs() >= route.basketballMissionOverMs &&
                        !cinematicPlayback.activeDialogue())
                        completeOpeningMission(*cityRender, 5, *route.brawlNextTitle);
                }
                if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->deliveryClosing &&
                    playback.vehicle && playback.vehicle->occupied) {
                    const auto placement = std::find_if(scene.placements.begin(), scene.placements.end(),
                        [](const auto& cue) { return cue.actor == "Player" && cue.visible; });
                    if (placement != scene.placements.end() && cinematicPlayback.elapsedMs() >= placement->timeMs) {
                        playback.vehicle->occupied = false;
                        playback.vehicle->doorAction.reset();
                        playback.vehicle->camera.reset();
                        playback.cameraTransition.reset();
                        playback.player = CityPlayerPose{placement->position, placement->rotationDegrees[2]};
                    }
                }
                if (playback.vehicle && cityRender->opening.vehicleActor) {
                    const auto& actor = cityRender->opening.actors[*cityRender->opening.vehicleActor];
                    for (const auto& cue : scene.placements) {
                        if (cue.actor != actor.name || !cue.visible || before >= cue.timeMs ||
                            cinematicPlayback.elapsedMs() < cue.timeMs) continue;
                        auto& vehicle = *playback.vehicle;
                        vehicle.position = cue.position;
                        vehicle.yaw = cue.rotationDegrees[2];
                        vehicle.speed = 0;
                        vehicle.groundTriangle.reset();
                    }
                }
                if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->vanHandshake &&
                    playback.vehicle && handoff.nextCheckpoint->vanHandshakeEntryMs) {
                    const auto entry = *handoff.nextCheckpoint->vanHandshakeEntryMs;
                    if (before < entry && cinematicPlayback.elapsedMs() >= entry) {
                        playback.vehicle->occupied = true;
                        playback.vehicle->speed = 0;
                        playback.vehicle->seatedMs = 0;
                        playback.vehicle->doorAction.reset();
                        playback.cameraTransition.reset();
                    }
                }
                const auto restoreAt = scene.followCameraMs.value_or(scene.durationMs);
                if (before < restoreAt && cinematicPlayback.elapsedMs() >= restoreAt) {
                    for (const auto& cue : scene.placements) {
                        if (cue.actor != "Player" || !cue.visible) continue;
                        const auto* motion = cityCinematicMotion(scene, cue.actor, restoreAt);
                        const auto position = motion ? cityCinematicTravelPosition(motion->travel, restoreAt) : cue.position;
                        playback.player = CityPlayerPose{position, cue.rotationDegrees[2]};
                        playback.player->followCameraYaw = cue.rotationDegrees[2];
                        if (const auto ground = cityRender->terrain.groundContact(position)) {
                            playback.player->position[2] = ground->height;
                            playback.player->groundTriangle = ground->triangle;
                        }
                    }
                }
                if (finished) {
                    cityRender->postFightCinematic.reset();
                    cityRender->activeMissionCinematic = nullptr;
                    if (&scene == &handoff && handoff.nextCheckpoint) {
                        activateNextMissionCheckpoint(*cityRender);
                        if (!smokeTest && playback.openingMissionsPaid[0] &&
                            handoff.unlockedScript == windowsOpeningCheckpointScript)
                            saveOpeningCheckpoint(*cityRender, *assets, windowsOpeningCheckpointScript);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->cinematic) {
                        activateNextMissionVehicle(*cityRender);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->pierCinematic &&
                               handoff.nextCheckpoint->weaponShopDriveCheckpoint) {
                        cityRender->weaponShopDrive = true;
                        auto& script = cityRender->hummerRouteScript.emplace(
                            CityScriptProgram{{{105, handoff.nextCheckpoint->weaponShopDriveWait}}});
                        script.unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->weaponShopCinematic &&
                               handoff.nextCheckpoint->weaponShopApproach) {
                        cityRender->weaponShopApproach = true;
                        auto& script = cityRender->nextMissionScript.emplace(
                            CityScriptProgram{{{64, handoff.nextCheckpoint->weaponShopApproachWait},
                                {47, handoff.nextCheckpoint->weaponShopTutorialCommand},
                                {77, handoff.nextCheckpoint->weaponShopUnlock},
                                {64, handoff.nextCheckpoint->weaponShopEntranceWait}, {48, {}}}});
                        script.unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->lcClosingCinematic) {
                        completeOpeningMission(*cityRender, 1, handoff.nextCheckpoint->title);
                        playback.vehicle.reset();
                        cityRender->returningToLc = false;
                        if (handoff.nextCheckpoint->lcUnlockedScript == windowsPostLcCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest) saveOpeningCheckpoint(*cityRender, *assets, windowsPostLcCheckpointScript);
                        }
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postLcIntro) {
                        activatePostLcDrive(*cityRender);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postLcAmbush) {
                        playback.vehicle.reset();
                        playback.postLcVehicleActive = false;
                        cityRender->postLcAmbushFinished = true;
                        cityRender->postLcTailMs = 0;
                        playback.missionConditions.clear();
                        for (const auto& [opcode, payload] : handoff.nextCheckpoint->postLcTailConditions)
                            playback.missionConditions.apply({opcode, payload});
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postLcConfrontation) {
                        const auto& waits = handoff.nextCheckpoint->postLcWalkWaits;
                        auto& script = cityRender->postLcWalkScript.emplace(CityScriptProgram{{{64, waits[0]}, {64, waits[1]}}});
                        script.unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postLcPhone) {
                        const auto& wait = handoff.nextCheckpoint->postLcWalkWaits[2];
                        auto& script = cityRender->postLcWalkScript.emplace(CityScriptProgram{{{64, wait}}});
                        script.unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->cooperNextClosing) {
                        const auto& route = *handoff.nextCheckpoint;
                        completeOpeningMission(*cityRender, 4, *route.cooperNextTitle);
                        playback.missionConditions.clear();
                        if (route.brawlUnlockedScript == windowsPostBrawlCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest) saveOpeningCheckpoint(*cityRender, *assets, windowsPostBrawlCheckpointScript);
                        }
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->basketballClosing) {
                        if (handoff.nextCheckpoint->basketballUnlockedScript == windowsPostBasketballCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest)
                                saveOpeningCheckpoint(*cityRender, *assets, windowsPostBasketballCheckpointScript);
                        }
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postBasketballAmbush) {
                        const auto& targets = handoff.nextCheckpoint->postBasketballAmbushTargets;
                        const auto time = scene.followCameraMs.value_or(scene.durationMs);
                        playback.locoOpponents.clear();
                        playback.locoLockTarget.reset();
                        playback.playerPunchMs.reset();
                        for (const auto& target : targets) {
                            const auto initial = std::find_if(scene.placements.rbegin(), scene.placements.rend(),
                                [&](const auto& cue) { return cue.actor == target && cue.timeMs <= time; });
                            if (initial == scene.placements.rend() || !initial->visible)
                                throw std::logic_error("Missing retail script 29 ambush placement.");
                            auto placement = *initial;
                            if (const auto* motion = cityCinematicMotion(scene, target, time))
                                placement.position = cityCinematicTravelPosition(motion->travel, time);
                            playback.spawnFightOpponent(cityRender->opening, cityRender->terrain, placement, false);
                            playback.locoOpponents.at(target).taggedForKill = true;
                        }
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : handoff.nextCheckpoint->postBasketballEscapeCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->postBasketballEscapeScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->deliveryClosing) {
                        const auto& route = *handoff.nextCheckpoint;
                        completeOpeningMission(*cityRender, 6, *route.postBasketballTitle);
                        if (!route.deliveryUnlockedScripts.empty() &&
                            route.deliveryUnlockedScripts.front() == windowsPostDeliveryCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest)
                                saveOpeningCheckpoint(*cityRender, *assets, windowsPostDeliveryCheckpointScript);
                        }
                        playback.vehicle.reset();
                        playback.postLcVehicleActive = false;
                        cityRender->hummerRemovalPending = cityRender->hummerRemovalReady = false;
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->vanMissionIntro) {
                        const auto& route = *handoff.nextCheckpoint;
                        const auto& actor = bindMissionVehicle(*cityRender, "aVan_00", cityRender->vanGeometry,
                            cityRender->vanFootprint, route.vanVehicleSeated, route.vanVehicleClips);
                        const auto staged = cityStagedVehiclePose(scene, actor.name, cityRender->terrain, scene.durationMs);
                        if (!staged || !playback.spawnVehicle(actor, cityRender->terrain, staged->position, staged->yaw))
                            throw std::runtime_error("Missing playable van after script 31 opening.");
                        playback.nextMissionVehicleActive = true;
                        playback.nextMissionVehicleAcquired = false;
                        playback.postLcVehicleActive = true;
                        playback.missionConditions.clear();
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : route.vanAcquisitionCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->vanAcquisitionScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->vanHandshake) {
                        const auto& route = *handoff.nextCheckpoint;
                        playback.missionConditions.clear();
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : route.vanHandshakeCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->vanHandshakeScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->vanSecondArrival) {
                        const auto& route = *handoff.nextCheckpoint;
                        playback.locoOpponents.clear();
                        playback.locoLockTarget.reset();
                        for (const auto& placement : route.vanProtectionApproach.placements)
                            playback.spawnFightOpponent(cityRender->opening, cityRender->terrain, placement, false);
                        for (const auto name : {"Juan_00", "YakuzaB_00"}) {
                            const auto& source = name == std::string_view("Juan_00") ? scene : route.vanArrival;
                            const auto placement = std::find_if(source.placements.rbegin(), source.placements.rend(),
                                [&](const auto& cue) { return cue.actor == name && cue.visible; });
                            if (placement == source.placements.rend())
                                throw std::logic_error("Missing retail protected actor placement.");
                            playback.spawnFightOpponent(cityRender->opening, cityRender->terrain, *placement, false);
                            playback.locoOpponents.at(name).disabled = name == std::string_view("YakuzaB_00");
                        }
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : route.vanProtectionCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->vanProtectionMs = 0;
                        cityRender->vanProtectionScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->vanArrival) {
                        const auto& route = *handoff.nextCheckpoint;
                        playback.nextMissionVehicleAcquired = false;
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : route.vanReentryCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->vanReentryScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint &&
                               (&scene == &handoff.nextCheckpoint->vanBriefing ||
                                &scene == &handoff.nextCheckpoint->vanReentryBriefing)) {
                        const auto& route = *handoff.nextCheckpoint;
                        cityRender->vanSecondDrive = &scene == &route.vanReentryBriefing;
                        const auto& wait = cityRender->vanSecondDrive ? route.vanSecondDriveWait : route.vanDriveWait;
                        cityRender->vanDriveScript.emplace(CityScriptProgram{{{105, wait}}}).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->elToroClosing) {
                        const auto& route = *handoff.nextCheckpoint;
                        completeOpeningMission(*cityRender, 7, *route.elToroTitle);
                        playback.vehicle.reset();
                        playback.postLcVehicleActive = false;
                        cityRender->hummerRemovalPending = cityRender->hummerRemovalReady = false;
                        if (route.elToroUnlockedScript == windowsPostCooperDeliveryCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest)
                                saveOpeningCheckpoint(*cityRender, *assets, windowsPostCooperDeliveryCheckpointScript);
                        }
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->elToroDeparture) {
                        const auto& route = *handoff.nextCheckpoint;
                        const auto& actor = bindMissionVehicle(*cityRender, "aCooper_00", cityRender->cooperGeometry,
                            cityRender->cooperFootprint, route.elToroCooperSeated, route.elToroCooperClips);
                        const auto staged = cityStagedVehiclePose(scene, actor.name, cityRender->terrain, scene.durationMs);
                        if (!staged || !playback.spawnVehicle(actor, cityRender->terrain, staged->position, staged->yaw))
                            throw std::runtime_error("Missing playable Cooper at script 30 departure.");
                        playback.vehicle->occupied = true;
                        playback.nextMissionVehicleActive = true;
                        playback.postLcVehicleActive = true;
                        playback.nextMissionVehicleAcquired = true;
                        playback.cameraTransition.reset();
                        playback.locoOpponents.clear();
                        playback.locoLockTarget.reset();
                        playback.missionConditions.clear();
                        playback.missionConditions.apply({70, route.elToroCooperProtection});
                        cityRender->elToroDriveScript.emplace(CityScriptProgram{{{105, route.elToroDriveWait}}}).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->elToroIntro) {
                        const auto& wait = handoff.nextCheckpoint->elToroDestinationWait;
                        cityRender->elToroDestinationScript.emplace(CityScriptProgram{{{64, wait}}}).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postBasketballBriefing) {
                        const auto& wait = handoff.nextCheckpoint->repairDriveWait;
                        cityRender->repairDriveScript.emplace(CityScriptProgram{{{105, wait}}}).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->repairArrival) {
                        cityRender->repairTutorialVisible = true;
                        cityRender->repairShop = CityRender::RepairShop::approach;
                        cityRender->repairShopMarker = cityRender->payphoneGameplayMs.value_or(0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postBasketballIntro) {
                        const auto& wait = handoff.nextCheckpoint->postBasketballDestinationWait;
                        cityRender->postBasketballRouteScript.emplace(CityScriptProgram{{{64, wait}}}).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->brawlNextIntro) {
                        const auto& wait = handoff.nextCheckpoint->basketballWait;
                        cityRender->basketballRouteScript.emplace(CityScriptProgram{{{64, wait}}}).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint &&
                               (&scene == &handoff.nextCheckpoint->basketballArrival ||
                                &scene == &handoff.nextCheckpoint->cooperNextArrival ||
                                &scene == &handoff.nextCheckpoint->elToroArrival ||
                                &scene == &handoff.nextCheckpoint->elToroInterlude)) {
                        const auto& route = *handoff.nextCheckpoint;
                        const bool basketball = &scene == &route.basketballArrival;
                        const bool elToro = &scene == &route.elToroArrival;
                        const bool secondFight = &scene == &route.elToroInterlude;
                        const auto& targets = secondFight ? route.elToroSecondFightTargets :
                            elToro ? route.elToroFightTargets :
                            basketball ? route.basketballFightTargets : route.cooperFightTargets;
                        const auto& commands = secondFight ? route.elToroSecondFightCommands :
                            elToro ? route.elToroFightCommands :
                            basketball ? route.basketballFightCommands : route.cooperFightCommands;
                        playback.locoOpponents.clear();
                        playback.locoLockTarget.reset();
                        playback.playerPunchMs.reset();
                        for (const auto& target : targets) {
                            const auto origin = std::find_if(scene.placements.rbegin(), scene.placements.rend(),
                                [&](const auto& cue) { return cue.actor == target; });
                            if (origin == scene.placements.rend() || !origin->visible)
                                throw std::logic_error("Missing retail fight actor placement.");
                            auto placement = *origin;
                            if (elToro) {
                                if (const auto* motion = cityCinematicMotion(scene, target, scene.durationMs))
                                    placement.position = cityCinematicTravelPosition(motion->travel, scene.durationMs);
                            }
                            playback.spawnFightOpponent(cityRender->opening, cityRender->terrain, placement, false);
                        }
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : commands)
                            program.front().push_back({opcode, payload});
                        cityRender->taggedFightScript.emplace(std::move(program)).unlock(0, 0, 0);
                        cityRender->taggedFightNextScene = elToro ? &route.elToroInterlude :
                            secondFight ? &route.elToroDeparture :
                            basketball ? &route.basketballClosing : &route.cooperNextClosing;
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->cooperNextIntro) {
                        playback.missionConditions.clear();
                        playback.nextMissionVehicleAcquired = false;
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : handoff.nextCheckpoint->cooperNextDriveCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->hummerRouteScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->locoNextIntro) {
                        activateLocoGuard(*cityRender);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->locoCooperIntro) {
                        const auto& route = *handoff.nextCheckpoint;
                        const auto& actor = cityRender->opening.actors.at(*cityRender->cooperActor);
                        cityRender->locoCooper = cityStagedVehiclePose(scene, actor.name, cityRender->terrain, scene.durationMs);
                        if (!cityRender->locoCooper || !route.cooperDriverPlacement)
                            throw std::runtime_error("Missing retail Cooper or its driver at the chase handoff.");
                        cityRender->locoCooper->occupied = true;
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : route.cooperEscapeCommands)
                            program.front().push_back({opcode, payload});
                        cityRender->locoCooperScript.emplace(std::move(program)).unlock(0, 0, 0);
                    } else if (handoff.nextCheckpoint && &scene == &handoff.nextCheckpoint->postLcFightIntro) {
                        CityScriptProgram program(1);
                        for (const auto& [opcode, payload] : handoff.nextCheckpoint->postLcFightCommands)
                            program.front().push_back({opcode, payload});
                        auto& script = cityRender->postLcFightScript.emplace(std::move(program));
                        script.unlock(0, 0, 0);
                    }
                }
            }
            const bool walking = movement && playback.elapsedMs >= *movement && !cityRender->terrain.ground.empty();
            const bool occupied = playback.vehicle && playback.vehicle->occupied;
            const bool scriptedArrival = cityRender->returnToLcFadeMs || cityRender->locoCooperPending ||
                hummerArrivalFadeDuration(*cityRender) != 0 ||
                (cityRender->missionTitleIndex >= 4 && cityRender->pendingHummerCinematic);
            const auto input = scriptedArrival ? std::array<float, 2>{} : touch->movement(occupied);
            if (walking && !playback.missionFailure && !cinematic && !postFight &&
                !cityRender->nextMissionFadeMs && !(cityRender->missionTitleIndex != 0 && cityRender->missionTitleMs)) {
                const bool reached = playback.timeAfterCheckpointMs.has_value();
                if (!scriptedArrival && !cityRender->hummerTutorialVisible && !occupied &&
                    playback.playerHealth > 0 && !playback.playerPunchMs &&
                    !(playback.vehicle && playback.vehicle->doorAction))
                    playback.walk(cityRender->opening, cityRender->terrain, input, delta);
                const auto* shopEntrance = cityRender->weaponShopUnlocked
                    ? &*cityRender->opening.payphone->postFight.nextCheckpoint->weaponShopEntranceMarker
                    : nullptr;
                const bool enterShop = !scriptedArrival && interact && shopEntrance &&
                    playback.canEnterShop(*shopEntrance);
                if (enterShop && (!cityRender->nextMissionScript || playback.openingMissionsPaid[1])) {
                    cityRender->weaponShopOpen = true;
                    touch->startMenuPointer();
                }
                const auto previousJump = playback.longestJumpMeters;
                const auto previousRamps = playback.visitedJumpRamps;
                const auto previousSpeed = playback.highestVehicleSpeedKmh;
                const auto previousDistance = playback.drivenKilometers;
                const auto doorSounds = playback.playerHealth > 0 && !playback.playerPunchMs &&
                    !cityRender->hummerTutorialVisible
                    ? playback.drive(cityRender->opening, cityRender->terrain, input, delta,
                                     interact && !enterShop && !scriptedArrival)
                    : CityDoorSounds{};
                playback.pursuePayphoneTarget(cityRender->opening, cityRender->terrain, delta);
                if (cityRender->postLcFightScript || cityRender->locoGuardScript || cityRender->taggedFightScript ||
                    cityRender->vanProtectionScript)
                    playback.punchLocoOpponent(cityRender->opening, cityRender->terrain, delta, punchPressed, lockPressed);
                else playback.punchPayphoneOpponent(cityRender->opening, cityRender->terrain, delta,
                                                   punchPressed, cityRender->payphoneTargetLocked);
                playback.updateLocoFight(cityRender->opening, cityRender->terrain, delta);
                if (cityRender->vanProtectionScript) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    auto& time = cityRender->vanProtectionMs;
                    time += std::clamp(delta, 0., 1000.);
                    for (const auto& motion : route.vanProtectionApproach.motions) {
                        auto& actor = playback.locoOpponents.at(motion.travel.actor);
                        if (actor.health == 0 || actor.rangedTarget) continue;
                        const auto oldPosition = actor.pose.position;
                        actor.pose.position = cityCinematicTravelPosition(motion.travel, time);
                        if (const auto ground = cityRender->terrain.groundContact(actor.pose.position, actor.pose.groundTriangle)) {
                            actor.pose.position[2] = ground->height;
                            actor.pose.groundTriangle = ground->triangle;
                        }
                        const auto dx = actor.pose.position[0] - oldPosition[0];
                        const auto dy = actor.pose.position[1] - oldPosition[1];
                        if (dx != 0 || dy != 0)
                            actor.pose.yaw = std::atan2(dx, -dy) * (180.f / 3.14159265f);
                        actor.pose.walking = time < motion.travel.durationMs;
                        actor.pose.clipTimeMs = time;
                    }
                    RetailMissionHooks fight;
                    fight.conditions = &playback.missionConditions;
                    fight.actorAttack = [&](const CityActorAttack& attack) {
                        playback.locoOpponents.at(attack.attacker).rangedTarget = attack.target;
                    };
                    fight.tagKillTarget = [&](std::string_view name) {
                        playback.locoOpponents.at(std::string(name)).taggedForKill = true;
                    };
                    fight.actorKilled = [&](std::string_view name) {
                        return playback.locoOpponents.at(std::string(name)).health == 0;
                    };
                    fight.clearKilledTarget = [&](std::string_view name) {
                        playback.locoOpponents.at(std::string(name)).taggedForKill = false;
                    };
                    auto& script = *cityRender->vanProtectionScript;
                    script.execute(0, static_cast<int32_t>(std::clamp(delta, 0., 1000.)), 0,
                                   [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, nullptr, {}, &fight);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->vanProtectionScript.reset();
                        playback.scriptedVehicleExit = playback.vehicle && playback.vehicle->occupied;
                        cityRender->pendingHummerCinematic = &route.vanHandshake;
                    }
                }
                if (cityRender->postBasketballEscapeScript && playback.player) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    RetailTravelHooks travel;
                    travel.escapePositions = [&](std::string_view name) -> std::optional<RetailEscapePositions> {
                        const auto& playerPosition = playback.vehicle && playback.vehicle->occupied
                            ? playback.vehicle->position : playback.player->position;
                        if (name == route.postBasketballEscapeMarker)
                            return RetailEscapePositions{playerPosition, route.postBasketballEscapePosition};
                        const auto enemy = playback.locoOpponents.find(name);
                        if (enemy == playback.locoOpponents.end()) return std::nullopt;
                        return RetailEscapePositions{playerPosition, enemy->second.pose.position};
                    };
                    RetailMissionHooks mission;
                    mission.disableActor = [&](std::string_view name, bool immediate) {
                        auto& enemy = playback.locoOpponents.at(std::string(name));
                        enemy.disabled = immediate;
                        enemy.disablePending = !immediate;
                        enemy.taggedForKill = false;
                    };
                    auto& script = *cityRender->postBasketballEscapeScript;
                    // The escape instructions can run before NPC weapon attacks are implemented.
                    script.execute(0, static_cast<int32_t>(std::clamp(delta, 0., 1000.)), 0,
                                   [](const auto& command, int32_t) { return command.opcode == 29; },
                                   [] {}, [] {}, false, &travel, {}, &mission);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->postBasketballEscapeScript.reset();
                        cityRender->pendingHummerCinematic = &route.postBasketballBriefing;
                    }
                }
                if (cityRender->postLcFightScript && playback.player) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto& scene = route.postLcFightIntro;
                    RetailTravelHooks travel;
                    travel.escapePositions = [&](std::string_view name) -> std::optional<RetailEscapePositions> {
                        const auto opponent = playback.locoOpponents.find(name);
                        if (opponent == playback.locoOpponents.end()) return std::nullopt;
                        const auto& position = playback.vehicle && playback.vehicle->occupied
                            ? playback.vehicle->position : playback.player->position;
                        return RetailEscapePositions{position, opponent->second.pose.position};
                    };
                    RetailMissionHooks fight;
                    fight.actorAttack = [&](const CityActorAttack& attack) {
                        const auto placement = std::find_if(scene.placements.rbegin(), scene.placements.rend(),
                            [&](const auto& cue) { return cue.actor == attack.attacker; });
                        if (placement == scene.placements.rend() || !placement->visible)
                            throw std::logic_error("Missing live Loco fight placement.");
                        playback.spawnFightOpponent(cityRender->opening, cityRender->terrain, *placement, true);
                    };
                    fight.disableActor = [&](std::string_view name, bool immediate) {
                        const auto opponent = playback.locoOpponents.find(name);
                        if (opponent == playback.locoOpponents.end())
                            throw std::logic_error("Missing live Loco opponent.");
                        opponent->second.disabled = immediate;
                        opponent->second.disablePending = !immediate;
                    };
                    fight.actorGetOut = [&](std::string_view name) {
                        if (name == "Player" && playback.vehicle && playback.vehicle->occupied)
                            playback.scriptedVehicleExit = true;
                    };
                    fight.finishMission = [&] {
                        completeOpeningMission(*cityRender, 2, *route.postLcTitle);
                        if (route.locoUnlockedScript == windowsPostLocoCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest) saveOpeningCheckpoint(*cityRender, *assets, windowsPostLocoCheckpointScript);
                        }
                    };
                    auto& script = *cityRender->postLcFightScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {},
                                   false, &travel, {}, &fight);
                    if (script.state(0) == RetailScriptState::locked) cityRender->postLcFightScript.reset();
                }
                if (cityRender->locoGuardScript) {
                    RetailMissionHooks guard;
                    guard.actorKilled = [&](std::string_view name) {
                        const auto found = playback.locoOpponents.find(name);
                        return found != playback.locoOpponents.end() && found->second.health == 0;
                    };
                    guard.clearKilledTarget = [&](std::string_view) { playback.locoLockTarget.reset(); };
                    guard.actorGetOut = [&](std::string_view name) {
                        if (name == "Player" && playback.vehicle && playback.vehicle->occupied)
                            playback.scriptedVehicleExit = true;
                    };
                    auto& script = *cityRender->locoGuardScript;
                    script.execute(0, static_cast<int32_t>(std::clamp(delta, 0., 1000.)), 0,
                                   [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, nullptr, {}, &guard);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->locoGuardScript.reset();
                        cityRender->locoCooperPending = true;
                    }
                }
                if (cityRender->taggedFightScript) {
                    const auto getOpponent = [&](std::string_view name) -> CityFightOpponent& {
                        const auto found = playback.locoOpponents.find(name);
                        if (found == playback.locoOpponents.end())
                            throw std::logic_error("Missing retail tagged fight target.");
                        return found->second;
                    };
                    RetailMissionHooks fight;
                    fight.tutorialsEnabled = [] { return true; };
                    fight.showTutorial = [&](const CityScriptTutorial&) { cityRender->basketballTutorialVisible = true; };
                    fight.clearActorAttacks = [&] {
                        for (auto& [name, opponent] : playback.locoOpponents) {
                            opponent.attacking = false;
                            opponent.punchMs.reset();
                            opponent.pose.walking = false;
                        }
                    };
                    fight.tagKillTarget = [&](std::string_view name) {
                        getOpponent(name).taggedForKill = true;
                    };
                    fight.actorKilled = [&](std::string_view name) {
                        return getOpponent(name).health == 0;
                    };
                    fight.clearKilledTarget = [&](std::string_view name) {
                        getOpponent(name).taggedForKill = false;
                    };
                    fight.disableActor = [&](std::string_view name, bool immediate) {
                        auto& opponent = getOpponent(name);
                        opponent.disabled = immediate;
                        opponent.disablePending = !immediate;
                    };
                    auto& script = *cityRender->taggedFightScript;
                    script.execute(0, static_cast<int32_t>(std::clamp(delta, 0., 1000.)), 0,
                                   [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, nullptr, {}, &fight);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->taggedFightScript.reset();
                        if (cityRender->taggedFightNextScene) {
                            playback.scriptedVehicleExit = playback.vehicle && playback.vehicle->occupied;
                            cityRender->pendingHummerCinematic = std::exchange(cityRender->taggedFightNextScene, nullptr);
                        }
                    }
                }
                if (cityRender->locoCooperScript && playback.player && cityRender->locoCooper) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    auto& cooper = *cityRender->locoCooper;
                    const auto& position = playback.vehicle && playback.vehicle->occupied
                        ? playback.vehicle->position : playback.player->position;
                    RetailTravelHooks travel;
                    travel.escapePositions = [&](std::string_view name) -> std::optional<RetailEscapePositions> {
                        if (name != route.cooperDriverPlacement->actor) return std::nullopt;
                        return RetailEscapePositions{position, cooper.position};
                    };
                    RetailMissionHooks escape;
                    escape.disableActor = [&](std::string_view name, bool) {
                        if (name == cityRender->opening.actors.at(*cityRender->cooperActor).name)
                            cityRender->cooperRemovalPending = true;
                        else if (name == route.targetVehicle) cityRender->hummerRemovalPending = true;
                    };
                    escape.finishMission = [&] {
                        completeOpeningMission(*cityRender, 3, *route.locoNextTitle);
                        if (route.cooperUnlockedScript == windowsPostCooperCheckpointScript) {
                            activatePostLcCheckpoint(*cityRender);
                            if (!smokeTest) saveOpeningCheckpoint(*cityRender, *assets, windowsPostCooperCheckpointScript);
                        }
                    };
                    auto& script = *cityRender->locoCooperScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {},
                                   false, &travel, {}, &escape);
                    if (script.state(0) == RetailScriptState::locked) cityRender->locoCooperScript.reset();
                    else pursueCityVehicle(cooper, cityRender->terrain, position,
                                           route.locoCooperIntro.vehicleDrives.front().speedCmPerMs, delta);
                }
                if (cityRender->nextMissionScript) {
                    const auto& next = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const bool postLc = playback.openingMissionsPaid[1];
                    const auto currentCheckpoint = [&]() -> const CityCheckpoint& {
                        if (playback.openingMissionsPaid[6]) return *next.elToroCheckpoint;
                        if (postLc) return next.marker;
                        if (cityRender->weaponShopUnlocked) return *next.weaponShopEntranceMarker;
                        return cityRender->weaponShopApproach ? *next.weaponShopApproach : next.marker;
                    };
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view name, bool active) {
                        if (name != currentCheckpoint().name) return;
                        if (active) cityRender->nextMissionMarker = cityRender->payphoneGameplayMs.value_or(0);
                        else if (!cityRender->weaponShopUnlocked || postLc) cityRender->nextMissionMarker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint& target) {
                        const auto& checkpoint = currentCheckpoint();
                        if (target.object != checkpoint.name || !playback.player || !playback.playerHealth)
                            return false;
                        if (cityRender->weaponShopUnlocked && !postLc) return enterShop;
                        const auto& position = playback.vehicle && playback.vehicle->occupied
                            ? playback.vehicle->position : playback.player->position;
                        return std::hypot(position[0] - checkpoint.position[0], position[1] - checkpoint.position[1],
                                          position[2] - checkpoint.position[2]) < checkpoint.radius;
                    };
                    auto& script = *cityRender->nextMissionScript;
                    RetailMissionHooks shop;
                    shop.tutorialsEnabled = [] { return true; };
                    shop.showTutorial = [&](const CityScriptTutorial&) { cityRender->weaponShopTutorialVisible = true; };
                    shop.hideTutorial = [&] { cityRender->weaponShopTutorialVisible = false; };
                    shop.setShopUnlocked = [&](std::string_view name, bool unlocked) {
                        if (name == next.weaponShopEntrance) cityRender->weaponShopUnlocked = unlocked;
                    };
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {},
                                   false, &travel, {}, &shop);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->nextMissionScript.reset();
                        if (postLc) {
                            cityRender->missionTitleIndex = playback.openingMissionsPaid[7] ? 8 :
                                playback.openingMissionsPaid[6] ? 7 :
                                playback.openingMissionsPaid[5] ? 6 :
                                playback.openingMissionsPaid[4] ? 5 :
                                playback.openingMissionsPaid[3] ? 4 :
                                playback.openingMissionsPaid[2] ? 3 : 2;
                            if (nextMissionFadeDuration(*cityRender)) cityRender->nextMissionFadeMs = 0;
                            else cityRender->missionTitleMs = 0;
                        } else if (cityRender->weaponShopUnlocked) {
                            cityRender->weaponShopOpen = true;
                            touch->startMenuPointer();
                            activateReturnToLc(*cityRender);
                        } else if (!cityRender->weaponShopApproach) {
                            cityRender->missionTitleIndex = 1;
                            if (next.fadeMs) cityRender->nextMissionFadeMs = 0;
                            else {
                                cityRender->missionTitleIndex = 1;
                                cityRender->missionTitleMs = 0;
                            }
                        }
                    }
                }
                if (cityRender->achievements) {
                    if (playback.longestJumpMeters > previousJump)
                        cityRender->achievements->recordJump(playback.longestJumpMeters);
                    if (playback.visitedJumpRamps != previousRamps)
                        cityRender->achievements->recordJumpRamps(playback.visitedJumpRamps);
                    if (playback.highestVehicleSpeedKmh > previousSpeed)
                        cityRender->achievements->recordVehicleSpeed(playback.highestVehicleSpeedKmh);
                    if (playback.drivenKilometers > previousDistance)
                        cityRender->achievements->recordKilometerDriven();
                }
                if (playback.vehicle) {
                    if (cityRender->achievements)
                        cityRender->achievements->recordDamage(playback.vehicle->peakCollisionDamage);
                    const auto& actor = cityRender->opening.actors.at(*cityRender->opening.vehicleActor);
                    updateCityVehicleSuspension(actor, *playback.vehicle, cityRender->wheels, cityRender->terrain, delta);
                }
                playback.orbitCamera(cityRender->opening, cameraDrag, delta);
                for (const auto tap : cameraTaps)
                    playback.releaseCameraTap(cityRender->opening, tap);
                if (recenterCamera) playback.recenterVehicleCamera(cityRender->opening);
                if (cityRender->audio && (doorSounds.closed || doorSounds.opening)) {
                    try {
                        if (doorSounds.closed) cityRender->audio->play(CityDoorSounds::closeId, false);
                        if (doorSounds.opening) cityRender->audio->play(CityDoorSounds::openId, false);
                    } catch (const std::runtime_error& error) {
                        std::cerr << error.what() << '\n';
                    }
                }
                const bool payphoneActive = playback.payphoneActive;
                const bool payphoneReached = playback.payphoneReached;
                playback.advancePayphone(cityRender->opening, delta);
                const bool nowOccupied = playback.vehicle && playback.vehicle->occupied;
                if (cityRender->repairShop == RepairShop::approach || cityRender->repairShop == RepairShop::entrance) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto inside = [](const CityCheckpoint& gate, const std::array<float, 3>& position) {
                        return std::hypot(position[0] - gate.position[0], position[1] - gate.position[1],
                                          position[2] - gate.position[2]) < gate.radius;
                    };
                    const auto& position = nowOccupied ? playback.vehicle->position : playback.player->position;
                    if (cityRender->repairShop == RepairShop::approach && inside(*route.repairShopApproach, position))
                        cityRender->repairShop = RepairShop::entrance;
                    if (cityRender->repairShop == RepairShop::entrance && nowOccupied &&
                        inside(*route.repairShopEntrance, position)) {
                        auto& car = *playback.vehicle;
                        if (car.health < car.maxHealth && car.maxHealth > 0 && !car.exploded) {
                            const auto loss = int64_t(car.maxHealth) - car.health;
                            const auto price = int64_t(route.repairPriceAtZeroHealth) * loss / car.maxHealth;
                            cityRender->repairQuote = static_cast<int32_t>(std::min<int64_t>(price, INT32_MAX));
                            if (cityRender->repairQuote <= playback.playerCash) {
                                car.speed = 0;
                                cityRender->repairShop = RepairShop::confirm;
                                touch->startMenuPointer();
                            }
                        } else if (car.health == car.maxHealth && !car.exploded) {
                            activateCarDelivery(*cityRender);
                        }
                        if (cityRender->repairShop != RepairShop::entrance) {
                            cityRender->repairTutorialVisible = false;
                            cityRender->repairShopMarker.reset();
                        }
                    }
                }
                if (cityRender->repairDriveScript || cityRender->deliveryScript || cityRender->elToroDriveScript) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const bool delivering = cityRender->deliveryScript.has_value();
                    const bool cooperDrive = cityRender->elToroDriveScript.has_value();
                    const auto& checkpoint = *(cooperDrive ? route.elToroDriveCheckpoint :
                        delivering ? route.deliveryCheckpoint : route.repairDriveCheckpoint);
                    auto& marker = cooperDrive ? cityRender->elToroDriveMarker :
                        delivering ? cityRender->deliveryMarker : cityRender->repairDriveMarker;
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view name, bool active) {
                        if (name != checkpoint.name) throw std::logic_error("Missing retail mission car drive marker.");
                        if (active) marker = cityRender->payphoneGameplayMs.value_or(0);
                        else marker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint& target) {
                        if (!nowOccupied || !playback.playerHealth || target.vehicle !=
                            cityRender->opening.actors.at(*cityRender->opening.vehicleActor).name) return false;
                        const auto& position = playback.vehicle->position;
                        return std::hypot(position[0] - checkpoint.position[0], position[1] - checkpoint.position[1],
                                          position[2] - checkpoint.position[2]) < checkpoint.radius;
                    };
                    auto& execution = cooperDrive ? cityRender->elToroDriveScript :
                        delivering ? cityRender->deliveryScript : cityRender->repairDriveScript;
                    auto& script = *execution;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel);
                    if (script.state(0) == RetailScriptState::locked) {
                        execution.reset();
                        if (cooperDrive) {
                            playback.scriptedVehicleExit = true;
                            cityRender->pendingHummerCinematic = &route.elToroClosing;
                        } else if (delivering) {
                            playback.missionConditions.clear();
                            playback.vehicle->speed = 0;
                            cityRender->activeMissionCinematic = &route.deliveryClosing;
                            cityRender->postFightCinematic.emplace();
                        } else {
                            playback.scriptedVehicleExit = true;
                            cityRender->pendingHummerCinematic = &route.repairArrival;
                        }
                    }
                }
                if (cityRender->elToroDestinationScript && playback.player) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto& target = *route.elToroDestination;
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view name, bool active) {
                        if (name != target.name) throw std::logic_error("Missing retail script 30 destination.");
                        if (active) cityRender->elToroDestinationMarker = cityRender->payphoneGameplayMs.value_or(0);
                        else cityRender->elToroDestinationMarker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint&) {
                        const auto& position = nowOccupied ? playback.vehicle->position : playback.player->position;
                        return playback.playerHealth > 0 && std::hypot(position[0] - target.position[0],
                            position[1] - target.position[1], position[2] - target.position[2]) < target.radius;
                    };
                    auto& script = *cityRender->elToroDestinationScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->elToroDestinationScript.reset();
                        playback.scriptedVehicleExit = nowOccupied;
                        cityRender->pendingHummerCinematic = &route.elToroArrival;
                    }
                }
                if (cityRender->postBasketballRouteScript && playback.player) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto& target = *route.postBasketballDestination;
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view name, bool active) {
                        if (name != target.name) throw std::logic_error("Missing retail script 29 destination.");
                        if (active) cityRender->postBasketballMarker = cityRender->payphoneGameplayMs.value_or(0);
                        else cityRender->postBasketballMarker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint&) {
                        const auto& position = nowOccupied ? playback.vehicle->position : playback.player->position;
                        if (playback.playerHealth <= 0 || std::hypot(position[0] - target.position[0],
                            position[1] - target.position[1], position[2] - target.position[2]) >= target.radius)
                            return false;
                        if (nowOccupied) playback.scriptedVehicleExit = true;
                        return !nowOccupied && !(playback.vehicle && playback.vehicle->doorAction);
                    };
                    auto& script = *cityRender->postBasketballRouteScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->postBasketballRouteScript.reset();
                        stagePostBasketballVehicle(*cityRender);
                    }
                }
                if (cityRender->vanAcquisitionScript || cityRender->vanReentryScript || cityRender->vanHandshakeScript) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    RetailTravelHooks travel;
                    travel.occupiedVehicle = [&]() -> std::string_view {
                        return nowOccupied ? cityRender->opening.actors.at(*cityRender->opening.vehicleActor).name
                                           : std::string_view{};
                    };
                    travel.trackVehicle = [&](std::string_view, bool tracked) { playback.nextMissionVehicleAcquired = !tracked; };
                    RetailMissionHooks mission;
                    mission.conditions = &playback.missionConditions;
                    mission.actorGetOut = [&](std::string_view name) {
                        if (name == "aVan_00" && nowOccupied) playback.scriptedVehicleExit = true;
                    };
                    const bool reentry = cityRender->vanReentryScript.has_value();
                    const bool afterHandshake = cityRender->vanHandshakeScript.has_value();
                    auto& execution = afterHandshake ? cityRender->vanHandshakeScript :
                        reentry ? cityRender->vanReentryScript : cityRender->vanAcquisitionScript;
                    auto& script = *execution;
                    script.execute(0, static_cast<int32_t>(std::clamp(delta, 0., 1000.)), 0,
                                   [&](const CityScriptCommand& command, int32_t) {
                                       if (!afterHandshake || command.opcode != 22) return false;
                                       const auto placement = decodeCityScriptActorPlacement(command);
                                       playback.locoOpponents.at(placement.actor).disabled = true;
                                       return true;
                                   }, [] {}, [] {}, false, &travel, {}, &mission);
                    if (script.state(0) == RetailScriptState::locked) {
                        execution.reset();
                        if (afterHandshake) {
                            cityRender->activeMissionCinematic = &route.vanHandshakeBriefing;
                            cityRender->postFightCinematic.emplace();
                        } else if (reentry) {
                            playback.vehicle->speed = 0;
                            cityRender->activeMissionCinematic = &route.vanReentryBriefing;
                            cityRender->postFightCinematic.emplace();
                        } else {
                            cityRender->pendingHummerCinematic = &route.vanBriefing;
                        }
                    }
                }
                if (cityRender->vanDriveScript) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto& checkpoint = *(cityRender->vanSecondDrive ? route.vanSecondDriveCheckpoint :
                                               route.vanDriveCheckpoint);
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view, bool active) {
                        if (active) cityRender->vanDriveMarker = cityRender->payphoneGameplayMs.value_or(0);
                        else cityRender->vanDriveMarker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint& target) {
                        if (!nowOccupied || !playback.playerHealth || target.vehicle !=
                            cityRender->opening.actors.at(*cityRender->opening.vehicleActor).name) return false;
                        const auto& position = playback.vehicle->position;
                        return std::hypot(position[0] - checkpoint.position[0], position[1] - checkpoint.position[1],
                                          position[2] - checkpoint.position[2]) < checkpoint.radius;
                    };
                    auto& script = *cityRender->vanDriveScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->vanDriveScript.reset();
                        playback.scriptedVehicleExit = true;
                        cityRender->pendingHummerCinematic = cityRender->vanSecondDrive
                            ? &route.vanSecondArrival : &route.vanArrival;
                    }
                }
                if (cityRender->postBasketballVehicleScript) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    RetailTravelHooks travel;
                    travel.occupiedVehicle = [&]() -> std::string_view {
                        return nowOccupied ? cityRender->opening.actors.at(*cityRender->opening.vehicleActor).name
                                           : std::string_view{};
                    };
                    travel.trackVehicle = [&](std::string_view name, bool tracked) {
                        if (name == route.postBasketballVehicle) playback.nextMissionVehicleAcquired = !tracked;
                    };
                    RetailMissionHooks mission;
                    mission.conditions = &playback.missionConditions;
                    auto& script = *cityRender->postBasketballVehicleScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {},
                                   false, &travel, {}, &mission);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->postBasketballVehicleScript.reset();
                        playback.vehicle->speed = 0;
                        cityRender->activeMissionCinematic = &route.postBasketballAmbush;
                        cityRender->postFightCinematic.emplace();
                    }
                }
                if (cityRender->basketballRouteScript && playback.player) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto& target = *route.basketballCheckpoint;
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view name, bool active) {
                        if (name != target.name) throw std::logic_error("Missing retail basketball marker.");
                        if (active) cityRender->basketballMarker = cityRender->payphoneGameplayMs.value_or(0);
                        else cityRender->basketballMarker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint&) {
                        const auto& position = nowOccupied ? playback.vehicle->position : playback.player->position;
                        return playback.playerHealth > 0 && std::hypot(position[0] - target.position[0],
                            position[1] - target.position[1], position[2] - target.position[2]) < target.radius;
                    };
                    auto& script = *cityRender->basketballRouteScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->basketballRouteScript.reset();
                        playback.scriptedVehicleExit = nowOccupied;
                        cityRender->pendingHummerCinematic = &route.basketballArrival;
                    }
                }
                if (cityRender->postLcWalkScript) {
                    const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const auto& checkpoints = route.postLcWalkCheckpoints;
                    size_t releasedCheckpoint = 0;
                    RetailTravelHooks travel;
                    travel.setActive = [&](std::string_view name, bool active) {
                        const auto found = std::find_if(checkpoints.begin(), checkpoints.end(),
                            [&](const auto& checkpoint) { return checkpoint && checkpoint->name == name; });
                        if (found == checkpoints.end()) throw std::logic_error("Missing retail Loco walking checkpoint.");
                        if (active) {
                            cityRender->postLcWalkMarker = CityRender::WalkingMarker{
                                static_cast<size_t>(found - checkpoints.begin()), cityRender->payphoneGameplayMs.value_or(0)};
                            cityRender->lastMinimapTarget.reset();
                        } else {
                            releasedCheckpoint = static_cast<size_t>(found - checkpoints.begin());
                            cityRender->postLcWalkMarker.reset();
                        }
                    };
                    travel.reached = [&](const CityScriptCheckpoint&) {
                        if (!playback.player || !playback.playerHealth || nowOccupied || !cityRender->postLcWalkMarker)
                            return false;
                        const auto& checkpoint = *checkpoints[cityRender->postLcWalkMarker->index];
                        const auto& position = playback.player->position;
                        return std::hypot(position[0] - checkpoint.position[0], position[1] - checkpoint.position[1],
                                          position[2] - checkpoint.position[2]) < checkpoint.radius;
                    };
                    auto& script = *cityRender->postLcWalkScript;
                    script.execute(0, 0, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel);
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->postLcWalkScript.reset();
                        playback.missionConditions.clear();
                        cityRender->activeMissionCinematic = releasedCheckpoint == 1 ? &route.postLcPhone : &route.postLcFightIntro;
                        cityRender->postFightCinematic.emplace();
                    }
                }
                if (cityRender->hummerRouteScript) {
                    const auto& mission = *cityRender->opening.payphone->postFight.nextCheckpoint;
                    const bool cooperNextDrive = cityRender->missionTitleIndex == 4;
                    const auto& checkpoint = *(cooperNextDrive ? mission.cooperNextDriveCheckpoint :
                        cityRender->postLcIntroFinished ? mission.postLcDriveCheckpoint :
                        cityRender->returningToLc ? mission.returnToLcCheckpoint :
                        cityRender->weaponShopDrive ? mission.weaponShopDriveCheckpoint : mission.vehicleCheckpoint);
                    const auto& vehicleName = cityRender->opening.actors.at(*cityRender->opening.vehicleActor).name;
                    RetailTravelHooks travel;
                    travel.occupiedVehicle = [&]() -> std::string_view { return nowOccupied ? vehicleName : std::string_view{}; };
                    travel.trackVehicle = [&](std::string_view name, bool tracked) {
                        if (name == mission.targetVehicle && !tracked) playback.nextMissionVehicleAcquired = true;
                    };
                    travel.setActive = [&](std::string_view name, bool active) {
                        if (name != checkpoint.name) return;
                        if (active) cityRender->hummerRouteMarker = cityRender->payphoneGameplayMs.value_or(0);
                        else cityRender->hummerRouteMarker.reset();
                    };
                    travel.reached = [&](const CityScriptCheckpoint& target) {
                        if (!nowOccupied || target.vehicle != vehicleName || target.object != checkpoint.name)
                            return false;
                        const auto& position = playback.vehicle->position;
                        return std::hypot(position[0] - checkpoint.position[0], position[1] - checkpoint.position[1],
                                          position[2] - checkpoint.position[2]) < checkpoint.radius;
                    };
                    auto& script = *cityRender->hummerRouteScript;
                    RetailMissionHooks tutorials;
                    tutorials.conditions = &playback.missionConditions;
                    tutorials.tutorialsEnabled = [] { return true; };
                    tutorials.blockingTutorial = [&](const CityScriptTutorial&) {
                        const bool dismissed = cityRender->hummerTutorialVisible && (pressed || interact || skip);
                        cityRender->hummerTutorialVisible = !dismissed;
                        return dismissed;
                    };
                    script.execute(0, static_cast<int32_t>(std::clamp(delta, 0., 1000.)), 0,
                                   [](const auto&, int32_t) { return false; }, [] {}, [] {}, false, &travel, {}, &tutorials);
                    if (nowOccupied && vehicleName == mission.targetVehicle)
                        playback.nextMissionVehicleAcquired = true;
                    if (script.state(0) == RetailScriptState::locked) {
                        cityRender->hummerRouteScript.reset();
                        if (cooperNextDrive) {
                            playback.scriptedVehicleExit = true;
                            cityRender->pendingHummerCinematic = &mission.cooperNextArrival;
                        } else if (cityRender->postLcIntroFinished) {
                            playback.scriptedVehicleExit = true;
                            cityRender->pendingHummerCinematic = &mission.postLcAmbush;
                        } else if (cityRender->returningToLc) {
                            cityRender->returnToLcFadeMs = 0;
                            cityRender->pendingHummerCinematic = &mission.lcClosingCinematic;
                            playback.vehicle->speed = 0;
                        } else {
                            playback.scriptedVehicleExit = true;
                            cityRender->pendingHummerCinematic = cityRender->weaponShopDrive
                                ? &mission.weaponShopCinematic : &mission.pierCinematic;
                        }
                    }
                }
                if (cityRender->radio) {
                    try {
                        if (occupied != nowOccupied) {
                            if (nowOccupied) {
                                const auto& vehicle = cityRender->opening.actors[*cityRender->opening.vehicleActor];
                                if (vehicle.radioStation) cityRender->radio->start(*vehicle.radioStation);
                            } else {
                                cityRender->radio->stop();
                            }
                        }
                        if (nowOccupied) {
                            cityRender->radio->change(radioStep);
                            cityRender->radio->update();
                        }
                    } catch (const std::runtime_error& error) {
                        cityRender->radio->stop();
                        std::cerr << error.what() << '\n';
                    }
                }
                if (!reached && playback.timeAfterCheckpointMs)
                    std::cout << "Reached retail checkpoint " << cityRender->opening.checkpoint->name << ".\n" << std::flush;
                if (occupied != nowOccupied)
                    std::cout << (nowOccupied ? "Entered " : "Exited ")
                              << cityRender->opening.actors[*cityRender->opening.vehicleActor].name << ".\n" << std::flush;
                if (!payphoneActive && playback.payphoneActive)
                    std::cout << "Activated retail payphone checkpoint.\n" << std::flush;
                if (!payphoneReached && playback.payphoneReached)
                    std::cout << "Reached retail checkpoint " << cityRender->opening.payphone->destination.name << ".\n" << std::flush;
            }
            const bool missionAlreadyFailed = playback.missionFailure.has_value();
            const bool vehicleAlreadyExploded = playback.vehicle && playback.vehicle->exploded;
            if (cityRender->postLcTailMs) {
                const auto& route = *cityRender->opening.payphone->postFight.nextCheckpoint;
                const auto& tail = route.postLcTail;
                auto& time = *cityRender->postLcTailMs;
                if (!playback.missionFailure && playback.player) {
                    time = std::min(time + (postFight ? 0 : delta), double(tail.durationMs));
                    if (time >= tail.durationMs) {
                        playback.missionConditions.clear();
                        cityRender->spyGauge.reset();
                        cityRender->postLcTailMs.reset();
                        cityRender->postLcAmbushFinished = false;
                        cityRender->activeMissionCinematic = &route.postLcConfrontation;
                        cityRender->postFightCinematic.emplace();
                    } else {
                        const auto lookup = [&](std::string_view actor) -> std::optional<RetailMissionTargetState> {
                            const auto position = cityTailingActorPosition(tail, actor, cityRender->terrain, time);
                            if (position) return RetailMissionTargetState{*position};
                            return std::nullopt;
                        };
                        const auto result = playback.missionConditions.update(static_cast<int32_t>(delta), false,
                                                                               playback.player->position, lookup);
                        cityRender->spyGauge = result.spyGauge;
                        playback.missionFailure = result.failureCause;
                        if (playback.missionFailure) ++playback.missionFailureCount;
                    }
                }
            } else playback.updateMission(cityRender->opening, delta);
            if (!playback.nextMissionVehicleActive) updateVehicleEffects(*cityRender, delta);
            if (cityRender->audio && cityRender->opening.vehicleActor) {
                const auto& actor = cityRender->opening.actors.at(*cityRender->opening.vehicleActor);
                const auto& car = playback.vehicle;
                const bool running = actor.gearProfile && actor.v6EngineSpeedRange && car &&
                    car->occupied && !car->exploded && !playback.missionFailure;
                try {
                    cityRender->audio->updateV6Engine(running,
                        running ? car->v6EngineRpm(*actor.gearProfile, *actor.v6EngineSpeedRange) : 1000,
                        running && input[1] * car->speed >= 0 ? std::abs(input[1]) : 0);
                } catch (const std::runtime_error& error) {
                    std::cerr << error.what() << '\n';
                }
            }
            const bool onFire = playback.vehicle &&
                playback.vehicle->damageEffect() == CityVehiclePose::DamageEffect::fire;
            if (cityRender->audio && cityRender->fireSoundActive != onFire) {
                cityRender->fireSoundActive = onFire;
                try {
                    if (onFire) cityRender->audio->play(CityVehiclePose::fireSoundId, true);
                    else cityRender->audio->stop(CityVehiclePose::fireSoundId);
                } catch (const std::runtime_error& error) {
                    std::cerr << error.what() << '\n';
                }
            }
            if (!vehicleAlreadyExploded && playback.vehicle && playback.vehicle->exploded && cityRender->audio) {
                try {
                    cityRender->audio->play(CityVehiclePose::explosionSoundId, false);
                } catch (const std::runtime_error& error) {
                    std::cerr << error.what() << '\n';
                }
            }
            if ((cityRender->postLcFightScript || cityRender->locoGuardScript || cityRender->taggedFightScript ||
                 cityRender->vanProtectionScript) &&
                !playback.missionFailure &&
                playback.playerHealth == 0) {
                playback.missionFailure = -1;
                ++playback.missionFailureCount;
            }
            if (!missionAlreadyFailed && playback.missionFailure) {
                if (cityRender->radio) cityRender->radio->stop();
                try {
                    if (cityRender->audio) cityRender->audio->play(22, false);
                } catch (const std::runtime_error& error) {
                    std::cerr << error.what() << '\n';
                }
                std::cout << "Opening mission failed with cause " << *playback.missionFailure
                          << " (attempt " << playback.missionFailureCount << ").\n" << std::flush;
            }
            if (cityRender->payphoneGameplayMs && !cityRender->postFightCinematic)
                cityRender->healthBlink.update(playback.playerHealth, delta);
            presentCity(*cityRender, info.imageExtent);
            if (previous < cityRender->opening.handoffMs && playback.elapsedMs >= cityRender->opening.handoffMs)
                std::cout << "Started the visual track of retail script " << cityRender->opening.nextScript
                          << " at " << cityRender->opening.handoffMs << " ms.\n" << std::flush;
            return playback.elapsedMs >= cityRender->opening.durationMs && !walking;
        }
        if (menu) {
            for (const auto& event : touch->menuPointerEvents())
                menu->pointer(event.x, event.y, event.down);
            touch->clearMenuPointerEvents();
            if (!smokeTest) touch->pollGamepad(window);
            menu->navigate(touch->takeMenuNavigation());
            menu->advance(milliseconds);
            if (const auto selected = menu->takeLanguageRequest()) {
                nextMenuFrame = 0;
                try {
                    menu->applyLanguage(*selected);
                    language = *selected;
                    saveRetailLanguagePreference(*assets, language);
                } catch (const std::runtime_error& error) {
                    std::cerr << "Could not apply or save language: " << error.what() << '\n';
                }
            }
            if (menu->takeExitRequest()) {
                PostQuitMessage(0);
                return true;
            }
            const bool continueGame = menu->takeContinueRequest();
            if (openingSmokeTest || continueGame || menu->takeNewGameRequest()) {
                try {
                    std::optional<RetailSavedState> checkpoint;
                    if (continueGame) {
                        checkpoint = readWindowsOpeningCheckpoint(*assets);
                        if (!checkpoint) throw std::runtime_error("The Windows checkpoint is missing.");
                    }
                    const auto city = loadCityAssets(*assets);
                    const auto opening = loadCityOpening(*assets, city, language);
                    const auto size = info.imageExtent;
                    auto geometry = prepareCityView(city, opening);
                    auto render = std::make_unique<CityRender>(vk);
                    render->opening = opening;
                    try {
                        render->achievements.emplace(readRetailAchievements(*assets));
                    } catch (const std::runtime_error& error) {
                        std::cerr << "Could not load retail achievements. " << error.what() << '\n';
                    }
                    render->startup.emplace(loadCityScriptProgram(*assets), opening.startupScript);
                    render->retailFrameRate = retailFrameRate;
                    const bool hasRadio = opening.vehicleActor && opening.actors[*opening.vehicleActor].radioStation;
                    render->audio = std::make_unique<RetailAudio>(*assets);
                    try {
                        const std::unique_ptr<IFileStream> file(assets->openSavedFile(retailPreferencesFilename));
                        if (file) {
                            const auto preferences = readRetailAudioPreferences(*file);
                            render->audio->setBusVolumes(preferences.effects, preferences.music);
                        }
                    } catch (const std::runtime_error& error) {
                        std::cerr << "Could not load retail audio preferences: " << error.what() << '\n';
                    }
                    if (openingSmokeTest) render->audio->setBusVolumes(0, 0);
                    if (hasRadio) {
                        try {
                            render->radio = std::make_unique<RadioPlayback>(*render->audio, loadRetailRadio(*assets));
                        } catch (const std::runtime_error& error) {
                            std::cerr << error.what() << '\n';
                        }
                    }
                    render->checkpointFirstSurface = geometry.checkpointFirstSurface;
                    render->payphoneFirstSurface = geometry.payphoneFirstSurface;
                    render->vehicleObjectiveFirstSurface = geometry.vehicleObjectiveFirstSurface;
                    render->nextMissionFirstSurface = geometry.nextMissionFirstSurface;
                    render->hummerRouteFirstSurface = geometry.hummerRouteFirstSurface;
                    render->weaponShopDriveFirstSurface = geometry.weaponShopDriveFirstSurface;
                    render->weaponShopApproachFirstSurface = geometry.weaponShopApproachFirstSurface;
                    render->weaponShopEntranceFirstSurface = geometry.weaponShopEntranceFirstSurface;
                    render->returnToLcFirstSurface = geometry.returnToLcFirstSurface;
                    render->postLcDriveFirstSurface = geometry.postLcDriveFirstSurface;
                    render->cooperNextDriveFirstSurface = geometry.cooperNextDriveFirstSurface;
                    render->basketballFirstSurface = geometry.basketballFirstSurface;
                    render->postBasketballFirstSurface = geometry.postBasketballFirstSurface;
                    render->repairDriveFirstSurface = geometry.repairDriveFirstSurface;
                    render->repairShopFirstSurface = geometry.repairShopFirstSurface;
                    render->deliveryFirstSurface = geometry.deliveryFirstSurface;
                    render->elToroFirstSurface = geometry.elToroFirstSurface;
                    render->elToroDestinationFirstSurface = geometry.elToroDestinationFirstSurface;
                    render->elToroDriveFirstSurface = geometry.elToroDriveFirstSurface;
                    render->vanDriveFirstSurface = geometry.vanDriveFirstSurface;
                    render->vanSecondDriveFirstSurface = geometry.vanSecondDriveFirstSurface;
                    render->postLcWalkFirstSurfaces = geometry.postLcWalkFirstSurfaces;
                    const auto appendOverlay = [&](float left, float right, float top, float bottom) {
                        const auto vertex = static_cast<uint32_t>(geometry.vertices.size());
                        const CityVertex a{{left, top, 0}, {0, 0}}, b{{right, top, 0}, {1, 0}};
                        const CityVertex c{{left, bottom, 0}, {0, 1}}, d{{right, bottom, 0}, {1, 1}};
                        geometry.vertices.insert(geometry.vertices.end(), {a, b, c, c, b, d});
                        return vertex;
                    };
                    if (render->startup || !opening.dialogue.empty() || opening.vehicleProtection)
                        render->cinematicBarsVertex = appendOverlay(-1, 1, -1, 1);
                    render->cinematicSkipVertex = appendOverlay(2.f * skipLeft / 480 - 1, 1, -1,
                                                                2.f * skipHeight / 320 - 1);
                    if (opening.missionTitle)
                        render->missionTitleLabelVertex = appendOverlay(-0.9f, 0.9f, 0.45f, 0.875f);
                    if (opening.missionTitle && !opening.missionTitle->completionText.empty())
                        render->missionCompleteObjectiveVertex = appendOverlay(-0.9f, 0.9f,
                                                                                132.f / 160 - 1, 0.f);
                    if (opening.vehicleProtection && !opening.vehicleProtection->failureTitle.empty())
                        render->failureTitleVertex = appendOverlay(-0.9f, 0.9f, -0.2125f, 0.2125f);
                    if (opening.vehicleProtection && !opening.vehicleProtection->failurePrompt.empty())
                        render->failurePromptVertex = appendOverlay(-0.9f, 0.9f, 0.5125f, 0.9375f);
                    if (!opening.dialogue.empty() || opening.checkpoint) {
                        constexpr float left = -0.9f, right = 0.9f, top = 0.5125f, bottom = 0.9375f;
                        render->captionVertex = appendOverlay(left, right, top, bottom);
                        render->dialogueCaptionVertex = appendOverlay(2.f * 104 / 480 - 1, right, top, bottom);
                    }
                    if (opening.payphone && opening.payphone->postFight.nextCheckpoint)
                        render->weaponShopInventoryVertex = appendOverlay(-2.f / 3, 2.f / 3, -0.8f, 0.8f);
                    std::optional<RetailMinimapAssets> minimap;
                    if (opening.movementMs) {
                        try {
                            minimap = loadRetailMinimap(*assets);
                            render->trackOpeningVehicle = opening.vehicleObjective &&
                                                          opening.vehicleObjective->icon == minimap->checkpointAnimationId;
                        } catch (const std::runtime_error& error) {
                            std::cerr << "Could not load the retail city minimap. " << error.what() << '\n';
                        }
                    }
                    if (minimap) {
                        const auto& frame = minimap->frames.at("MINIMAP_BG");
                        const auto& player = minimap->frames.at("MINIMAP_PLAYER");
                        const auto [left, right, top, bottom] = frame.bounds;
                        constexpr float inset = 18;
                        const auto appendHud = [&](float x0, float y0, float x1, float y1) {
                            return appendOverlay(x0 / 240 - 1, x1 / 240 - 1, y0 / 160 - 1, y1 / 160 - 1);
                        };
                        render->minimapWidth = minimap->map.width;
                        render->minimapHeight = minimap->map.height;
                        render->minimapHalfWidth = (right - left) / 2.f - inset;
                        render->minimapHalfHeight = (bottom - top) / 2.f - inset;
                        const auto centerX = (left + right) / 2.f, centerY = (top + bottom) / 2.f;
                        const auto halfPlayerWidth = player.image.width / 2.f, halfPlayerHeight = player.image.height / 2.f;
                        render->minimapVertices[0] = appendHud(left, top, right, bottom);
                        render->minimapVertices[1] = appendHud(left + inset, top + inset, right - inset, bottom - inset);
                        render->minimapVertices[2] = appendHud(centerX - halfPlayerWidth, centerY - halfPlayerHeight,
                                                             centerX + halfPlayerWidth, centerY + halfPlayerHeight);
                        render->minimapCheckpointAnimation = minimap->checkpointAnimation;
                        int32_t maxX = 0, maxY = 0;
                        for (const auto& step : render->minimapCheckpointAnimation) {
                            const auto& b = minimap->frames.at(step.frame).bounds;
                            render->minimapCheckpointVertices.push_back(
                                appendHud(centerX + b.left, centerY + b.top, centerX + b.right, centerY + b.bottom));
                            maxX = std::max({maxX, std::abs(b.left), std::abs(b.right)});
                            maxY = std::max({maxY, std::abs(b.top), std::abs(b.bottom)});
                        }
                        render->minimapMarkerLimits = {
                            std::max(1.f, render->minimapHalfWidth - maxX),
                            std::max(1.f, render->minimapHalfHeight - maxY)
                        };
                    }
                    if (opening.payphone) {
                        render->routeArrowVertex = static_cast<uint32_t>(geometry.vertices.size());
                        geometry.vertices.insert(geometry.vertices.end(), {
                            {{-0.051f, 0.043f, 0}, {}}, {{0, -0.065f, 0}, {}}, {{0.051f, 0.043f, 0}, {}}
                        });
                    }
                    std::optional<RetailStatusHud> statusHud;
                    if (opening.payphone) {
                        statusHud = loadRetailStatusHud(*assets);
                        for (size_t i = 0; i < statusHud->gauges.size(); ++i) {
                            const auto& source = statusHud->gauges[i];
                            auto& gauge = render->statusGauges[i];
                            for (size_t j = 0; j < source.layers.size(); ++j) {
                                const auto& layer = source.layers[j];
                                // The retail gauges are authored for the right edge of a 568px canvas.
                                const auto x = layer.x - (568 - 480);
                                gauge.vertices[j] = appendOverlay(x / 240.f - 1,
                                    (x + int32_t(layer.image.width)) / 240.f - 1, layer.y / 160.f - 1,
                                    (layer.y + int32_t(layer.image.height)) / 160.f - 1);
                            }
                        }
                        for (size_t i = 0; i < statusHud->spyLayers.size(); ++i) {
                            const auto& layer = statusHud->spyLayers[i];
                            render->spyVertices[i] = appendOverlay(layer.x / 240.f - 1,
                                (layer.x + int32_t(layer.image.width)) / 240.f - 1, layer.y / 160.f - 1,
                                (layer.y + int32_t(layer.image.height)) / 160.f - 1);
                        }
                        for (size_t i = 0; i < statusHud->cashGlyphs.size(); ++i) {
                            const auto& glyph = statusHud->cashGlyphs[i];
                            render->cashGlyphVertices[i] = appendOverlay(-1, glyph.width / 240.f - 1,
                                                                          -1, glyph.height / 160.f - 1);
                        }
                    }
                    prepareVehicleEffects(*render, *assets, geometry);
                    render->vehicleLights = std::move(geometry.vehicleLights);
                    prepareCityTargets(*render, info.imageFormat, size);
                    prepareCityTextures(*render, *assets, geometry, minimap ? &*minimap : nullptr,
                                        statusHud ? &*statusHud : nullptr, language);
                    prepareCityPipeline(*render, size);
                    uploadCity(*render, geometry);
                    render->vehicleFirstVertex = geometry.vehicleFirstVertex;
                    const auto firstVehicleVertex = geometry.vertices.begin() + geometry.vehicleFirstVertex;
                    render->vehicleVertices.assign(firstVehicleVertex, firstVehicleVertex + geometry.vehicleVertexCount);
                    render->wheels = std::move(geometry.wheels);
                    render->vehicleMorphs = std::move(geometry.vehicleMorphs);
                    render->nextVehicleGeometry = std::move(geometry.nextVehicleGeometry);
                    render->nextVehicleFootprint = geometry.nextVehicleFootprint;
                    render->postBasketballVehicleGeometry = std::move(geometry.postBasketballVehicleGeometry);
                    render->vanGeometry = std::move(geometry.vanGeometry);
                    render->vanFootprint = geometry.vanFootprint;
                    render->postBasketballVehicleFootprint = geometry.postBasketballVehicleFootprint;
                    render->cooperActor = geometry.cooperActor;
                    render->cooperGeometry = std::move(geometry.cooperGeometry);
                    render->cooperFootprint = geometry.cooperFootprint;
                    if (render->cooperGeometry) {
                        const auto& cooper = *render->cooperGeometry;
                        const auto first = geometry.vertices.begin() + cooper.vehicleFirstVertex;
                        render->cooperVertices.assign(first, first + cooper.vehicleVertexCount);
                    }
                    render->checkpointMeshes = std::move(geometry.checkpointMeshes);
                    render->lowDetailFirstSurface = geometry.districtSurfaceCount;
                    render->lowDetailEndSurface = geometry.lowDetailEndSurface;
                    render->billboards = std::move(geometry.billboards);
                    render->vertices = std::move(geometry.vertices);
                    render->terrain = std::move(geometry.terrain);
                    if (checkpoint) restoreOpeningCheckpoint(*render, *checkpoint);
                    cityRender = std::move(render);
                    size_t scenes = 0;
                    size_t triangles = 0;
                    for (const auto& district : city.districts) {
                        for (const auto* scene : {&district.scene, &district.lowDetailScene}) {
                            if (scene->bytes.empty()) continue;
                            ++scenes;
                            for (const auto& mesh : scene->meshes)
                                for (const auto& buffer : mesh.buffers)
                                    triangles += buffer.indices.size() / 3;
                        }
                    }
                    std::cout << "Loaded " << city.districts.size() << " retail city districts and " << scenes
                              << " BRES scene files. Decoded " << triangles << " district triangles and resolved "
                              << city.geometryLibraries.size() << " geometry libraries.\n" << std::flush;
                    std::cout << "Opened " << city.districts[opening.district].objectsPath << " with "
                              << opening.durationMs << " ms of the retail cinematic visual track and "
                              << opening.actors.size() << " scripted actor placements.\n" << std::flush;
                } catch (const std::runtime_error& error) {
                    if (openingSmokeTest) throw;
                    std::cerr << (continueGame ? "Continue" : "New Game") << " setup failed: "
                              << error.what() << '\n' << std::flush;
                }
                if (cityRender) {
                    presentCity(*cityRender, info.imageExtent);
                    cityRender->lastTick = std::chrono::steady_clock::now();
                    std::cout << "Presented " << cityRender->indexCount / 3
                              << " retail opening scene triangles with Vulkan depth testing.\n" << std::flush;
                    touch->clearMenuPointerEvents();
                    if (continueGame) touch->stopMenuPointer();
                    else touch->startMenuPointer();
                    touch->takePress();
                    touch->takeCutsceneSkip();
                    touch->takeVehiclePress();
                    touch->takeRadioStep();
                    menu.reset();
                    return false;
                }
            }
            nextMenuFrame -= milliseconds;
            if (nextMenuFrame <= 0) {
                drawFull(menu->draw(splash->background));
                nextMenuFrame = splash->frameMs;
            }
            return false;
        }
        if (introFinished) {
            if (!fadeTime) {
                if (!touch->takePress()) return false;
                fadeTime = 0;
                machine._switchState(new StartupFrame([&] {
                    drawFull(blendSplash(*splash, splash->opacities.at(shownFadeFrame)));
                }));
                machine.draw();
                return false;
            }
            *fadeTime += milliseconds;
            const auto last = splash->opacities.size() - 1;
            const auto frame = std::min(last, size_t(*fadeTime / splash->frameMs));
            if (frame != shownFadeFrame) {
                shownFadeFrame = frame;
                machine.draw();
            }
            if (frame != last) return false;
            std::cout << "Completed widgetSplash hide timeline and displayed the main-menu background.\n" << std::flush;
            menu = std::make_unique<MenuRuntime>(*assets, splash->exports, language);
            touch->startMenuPointer();
            drawFull(menu->draw(splash->background));
            std::cout << "Presented the GameSWF menu through Vulkan.\n" << std::flush;
            return false;
        }

        if (logoFrame) {
            if (!logoFrame->finished()) return false;
            logoFrame = nullptr;
            nextFrame = movie->next();
            if (!nextFrame) throw std::runtime_error("The intro movie has no video frames.");
            movieTime = nextFrame->timestampMs;
            if (!smokeTest) movie->startAudio();
            machine._switchState(new StartupFrame(showMovieFrame));
        } else {
            const auto audio = movie->audioPositionMs();
            movieTime = audio ? std::max(movieTime, *audio) : movieTime + milliseconds;
        }

        bool redraw = false;
        while (nextFrame && nextFrame->timestampMs <= movieTime) {
            currentFrame = std::move(nextFrame);
            nextFrame = movie->next();
            redraw = true;
        }
        if (redraw) machine.draw();
        if (nextFrame && !smokeTest) return false;
        if (!smokeTest && movieTime < movie->durationMs()) return false;
        movie->stopAudio();
        std::cout << "Presented " << displayedFrames << " intro video frames.\n";
        showSplash();
        if (smokeTest) return true;
        introFinished = true;
        touch->takePress();
        return false;
    };
    if (smokeTest) {
        if (logo) {
            update(logoDuration);
            menu = std::make_unique<MenuRuntime>(*assets, splash->exports, language);
            menu->advance(1000.0 / 30);
            drawFull(menu->draw(splash->background));
            std::cout << "Presented the GameSWF menu through Vulkan.\n" << std::flush;
            if (openingSmokeTest) {
                if (menu->canContinue()) {
                    const auto checkpoint = readWindowsOpeningCheckpoint(*assets);
                    if (!checkpoint) throw std::runtime_error("The Continue smoke test checkpoint is missing.");
                    const auto savedAchievements = loadMenuAchievementState(*assets, &*checkpoint);
                    menu->navigate({0, true, false});
                    update(0);
                    if (!cityRender || cityRender->startup)
                        throw std::runtime_error("Continue did not enter the saved gameplay state.");
                    update(16);
                    if (cityRender->playback.playerCash != checkpoint->cash ||
                        cityRender->playback.playerArmor != checkpoint->armor[0] ||
                        !cityRender->playback.openingMissionsPaid[0] || !cityRender->nextMissionMarker ||
                        !cityRender->achievements || !savedAchievements ||
                        cityRender->achievements->stat(49) != savedAchievements->stat(49))
                        throw std::runtime_error("Continue did not restore the saved player, stats, and next objective.");
                    const auto script = checkpoint->completedScripts.front().script;
                    const bool postCooperDelivery = script == windowsPostCooperDeliveryCheckpointScript;
                    const bool postDelivery = postCooperDelivery || script == windowsPostDeliveryCheckpointScript;
                    const bool postBasketball = postDelivery || script == windowsPostBasketballCheckpointScript;
                    const bool postBrawl = postBasketball || script == windowsPostBrawlCheckpointScript;
                    const bool postCooper = postBrawl || script == windowsPostCooperCheckpointScript;
                    const bool postLoco = postCooper || script == windowsPostLocoCheckpointScript;
                    const bool postLc = postLoco || script == windowsPostLcCheckpointScript;
                    if (cityRender->playback.openingMissionsPaid[7] != postCooperDelivery ||
                        cityRender->playback.openingMissionsPaid[6] != postDelivery ||
                        cityRender->playback.openingMissionsPaid[5] != postBasketball ||
                        cityRender->playback.openingMissionsPaid[4] != postBrawl ||
                        cityRender->playback.openingMissionsPaid[3] != postCooper ||
                        cityRender->playback.openingMissionsPaid[2] != postLoco ||
                        cityRender->playback.openingMissionsPaid[1] != postLc ||
                        (postLc && (cityRender->playback.vehicle || !cityRender->weaponShopUnlocked)))
                        throw std::runtime_error("Continue restored the wrong opening mission handoff.");
                    if (postLc && !postLoco) {
                        auto& render = *cityRender;
                        const auto& mission = *render.opening.payphone->postFight.nextCheckpoint;
                        render.playback.player->position = mission.marker.position;
                        update(0);
                        if (render.missionTitleIndex != 2 || !render.nextMissionFadeMs)
                            throw std::runtime_error("Script 25 marker did not start its retail fade.");
                        update(mission.postLcFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[2].label.descriptor)
                            throw std::runtime_error("Script 25 did not prepare its retail mission title.");
                        update(1000);
                        update(4000);
                        if (render.activeMissionCinematic != &mission.postLcIntro || !render.postFightCinematic)
                            throw std::runtime_error("Script 25 title did not start its retail cinematic.");
                        bool finished = false;
                        for (int step = 0; step < 24 && !finished; ++step) {
                            finished = render.postFightCinematic->advance(mission.postLcIntro, 1000, true, {});
                            presentCity(render, info.imageExtent);
                        }
                        if (!finished) throw std::runtime_error("Script 25 cinematic smoke frames did not finish.");
                        update(0);
                        if (!render.postLcIntroFinished || !render.postLcDriveObjective.descriptor)
                            throw std::runtime_error("Script 25 did not hand off to its first objective.");
                        auto& playback = render.playback;
                        if (!playback.vehicle || playback.vehicle->occupied || !render.hummerRouteScript ||
                            !render.postLcDriveFirstSurface)
                            throw std::runtime_error("Script 25 did not spawn the playable Hummer and route.");
                        playback.drive(render.opening, render.terrain, {}, 0, true);
                        update(0);
                        if (!playback.vehicle->occupied || !render.hummerRouteMarker)
                            throw std::runtime_error("Returning to the Hummer did not activate the script 25 checkpoint.");
                        auto& hummer = *playback.vehicle;
                        hummer.position = mission.postLcDriveCheckpoint->position;
                        const auto ground = render.terrain.groundContact(hummer.position, std::nullopt, 300, 300);
                        if (!ground) throw std::runtime_error("Script 25 checkpoint ground is missing.");
                        hummer.position[2] = ground->height;
                        hummer.groundTriangle = ground->triangle;
                        hummer.airborne = false;
                        update(0);
                        if (render.hummerRouteScript || render.hummerRouteMarker || !playback.scriptedVehicleExit)
                            throw std::runtime_error("Script 25 drive did not request the retail vehicle exit.");
                        for (int step = 0; step < 600 && !render.hummerArrivalFadeMs; ++step) update(16);
                        if (!render.hummerArrivalFadeMs || render.postFightCinematic)
                            throw std::runtime_error("Script 25 exit did not start the arrival fade.");
                        update(mission.postLcArrivalFadeMs);
                        if (render.activeMissionCinematic != &mission.postLcAmbush || !render.postFightCinematic)
                            throw std::runtime_error("Script 25 fade did not start the Loco cinematic.");
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(1000, true);
                        if (!render.postLcAmbushFinished || !render.postLcAmbushObjective.descriptor ||
                            playback.vehicle || playback.postLcVehicleActive || !render.postLcTailMs ||
                            !render.spyGauge || !render.spyWidth || playback.missionFailure)
                            throw std::runtime_error("Script 25 Loco cinematic did not hand off to its live tailing objective.");
                        const auto beforeTail = cityTailingActorPosition(mission.postLcTail, "Loco_00", render.terrain, 0);
                        update(mission.postLcTail.motions.front().travel.startMs);
                        update(200);
                        const auto duringTail = cityTailingActorPosition(mission.postLcTail, "Loco_00", render.terrain,
                                                                        *render.postLcTailMs);
                        if (!beforeTail || !duringTail || duringTail == beforeTail || !render.spyGauge)
                            throw std::runtime_error("Script 25 Loco did not advance along its tailing route.");
                        const auto& tail = mission.postLcTail;
                        *render.postLcTailMs = tail.fades.back().startMs;
                        const auto lastTarget = cityTailingActorPosition(tail, "Loco_00", render.terrain,
                                                                         tail.durationMs - 1.);
                        if (!lastTarget) throw std::runtime_error("Script 25 lost its tailing actor during the fade.");
                        playback.player->position = *lastTarget;
                        playback.player->position[0] += 500;
                        update(tail.fades.back().durationMs - 1.);
                        if (!render.spyGauge || playback.missionFailure ||
                            cityTrackBrightness(tail.fades, *render.postLcTailMs) >= 0.01f)
                            throw std::runtime_error("Script 25 did not render the final tailing fade.");
                        update(1);
                        if (render.postLcTailMs || render.spyGauge || playback.missionFailure ||
                            render.activeMissionCinematic != &mission.postLcConfrontation)
                            throw std::runtime_error("Script 25 did not clear the tailing state and start the confrontation.");
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(1000, true);
                        if (!render.postLcWalkScript || !render.postLcConfrontationObjective.descriptor ||
                            render.postFightCinematic || playback.missionFailure)
                            throw std::runtime_error("Script 25 confrontation did not hand off to its next objective.");
                        const auto& checkpoints = mission.postLcWalkCheckpoints;
                        playback.player->position = checkpoints[1]->position;
                        update(0);
                        if (!render.postLcWalkMarker || render.postLcWalkMarker->index != 0)
                            throw std::runtime_error("Script 25 accepted the walking checkpoints out of order.");
                        playback.player->position = checkpoints[0]->position;
                        update(0);
                        if (!render.postLcWalkMarker || render.postLcWalkMarker->index != 1)
                            throw std::runtime_error("Script 25 did not activate its second walking checkpoint.");
                        playback.player->position = checkpoints[1]->position;
                        update(0);
                        if (render.postLcWalkScript || render.postLcWalkMarker || playback.missionFailure ||
                            render.activeMissionCinematic != &mission.postLcPhone || !render.postFightCinematic)
                            throw std::runtime_error("Script 25 did not start the phone scene after its second checkpoint.");
                        const auto phonePosition = playback.player->position;
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(1000, true);
                        update(0);
                        if (!render.postLcWalkMarker || render.postLcWalkMarker->index != 2 ||
                            render.postFightCinematic || playback.missionFailure || playback.player->position != phonePosition)
                            throw std::runtime_error("Script 25 phone scene did not restore control and activate C2M204.");
                        playback.player->position = checkpoints[2]->position;
                        update(0);
                        if (render.postLcWalkMarker || render.postLcWalkScript || !render.postFightCinematic ||
                            render.activeMissionCinematic != &mission.postLcFightIntro)
                            throw std::runtime_error("Script 25 did not start the fight introduction at C2M204.");
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(1000, true);
                        const auto fightStart = std::find_if(mission.postLcFightIntro.placements.rbegin(),
                            mission.postLcFightIntro.placements.rend(), [](const auto& cue) {
                                return cue.actor == "Player" && cue.visible;
                            });
                        update(0);
                        if (!render.postLcFightScript || playback.locoOpponents.size() != 2 ||
                            !render.postLcFightObjective.descriptor ||
                            render.postFightCinematic || playback.missionFailure ||
                            fightStart == mission.postLcFightIntro.placements.rend() ||
                            playback.player->position[0] != fightStart->position[0] ||
                            playback.player->position[1] != fightStart->position[1])
                            throw std::runtime_error("Script 25 did not restore the player at the fight objective.");
                        auto& firstLoco = playback.locoOpponents.at("Loco_00");
                        auto& secondLoco = playback.locoOpponents.at("Loco_01");
                        firstLoco.pose.position = secondLoco.pose.position = playback.player->position;
                        const auto startingVitals = playback.playerHealth + playback.playerArmor;
                        for (int step = 0; step < 30 && playback.playerHealth + playback.playerArmor == startingVitals;
                             ++step) update(50);
                        if (!firstLoco.punchMs || !secondLoco.punchMs ||
                            playback.playerHealth + playback.playerArmor >= startingVitals || !render.postLcFightScript)
                            throw std::runtime_error("Script 25 live opponents did not attack within range.");
                        firstLoco.pose.position[0] += 4000;
                        update(0);
                        if ((!firstLoco.disabled && !firstLoco.disablePending) || secondLoco.disabled ||
                            secondLoco.disablePending || !render.postLcFightScript)
                            throw std::runtime_error("Script 25 did not resolve the first escape gate independently.");
                        const auto cashBeforeReward = playback.playerCash;
                        secondLoco.pose.position[0] -= 4000;
                        update(0);
                        if ((!secondLoco.disabled && !secondLoco.disablePending) || render.postLcFightScript)
                            throw std::runtime_error("Script 25 did not resolve the last escape gate.");
                        update(0);
                        if (!playback.openingMissionsPaid[2] || playback.playerCash != cashBeforeReward + 200 ||
                            !render.missionRewardCash[2].descriptor || !render.missionCompleteObjectives[2].descriptor ||
                            !render.missionRewardMs || render.missionRewardIndex != 2 || !render.nextMissionMarker ||
                            (render.achievements && render.achievements->stat(49) != 3))
                            throw std::runtime_error("Script 25 did not award the retail reward once or activate script 26.");
                    }
                    if (postLc && !postCooper) {
                        auto& render = *cityRender;
                        const auto& route = *render.opening.payphone->postFight.nextCheckpoint;
                        render.playback.player->position = route.marker.position;
                        update(0);
                        if (render.missionTitleIndex != 3 || !render.nextMissionFadeMs)
                            throw std::runtime_error("Script 26 did not start at the retail LC checkpoint.");
                        update(route.locoNextFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[3].label.descriptor)
                            throw std::runtime_error("Script 26 did not present its authored title.");
                        update(4000);
                        if (render.activeMissionCinematic != &route.locoNextIntro || !render.postFightCinematic)
                            throw std::runtime_error("Script 26 did not enter its opening cinematic.");
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(1000, true);
                        if (!render.locoGuardScript || !render.locoNextObjective.descriptor ||
                            !render.playback.vehicle || !render.playback.postLcVehicleActive)
                            throw std::runtime_error("Script 26 did not hand off its authored Hummer and objective.");
                        auto& playback = render.playback;
                        auto& guard = playback.locoOpponents.at(route.locoGuardPlacement->actor);
                        if (guard.health != 100 || guard.attacking)
                            throw std::runtime_error("Script 26 guard did not start in its authored state.");
                        playback.player->position = guard.pose.position;
                        playback.punchLocoOpponent(render.opening, render.terrain, 0, true, true);
                        for (int step = 0; step < 20 && !guard.attacking; ++step) update(50);
                        if (!guard.attacking || guard.health >= 100 || !render.locoGuardScript)
                            throw std::runtime_error("Script 26 guard did not take melee damage or react.");
                        // Keep a fixed combat fixture while checking the kill and wait gates.
                        for (int hit = 0; hit < 16 && guard.health > 0; ++hit) {
                            playback.punchLocoOpponent(render.opening, render.terrain, 0, true, false);
                            for (int step = 0; step < 20; ++step)
                                playback.punchLocoOpponent(render.opening, render.terrain, 50, false, false);
                        }
                        if (guard.health != 0) throw std::runtime_error("The script 26 guard survived lethal damage.");
                        update(0);
                        if (!render.locoGuardScript) throw std::runtime_error("Guard gate skipped its retail delay.");
                        update(500);
                        if (!render.locoGuardScript) throw std::runtime_error("Guard gate ended before its wait expired.");
                        update(1);
                        if (render.locoGuardScript || guard.defeatMs < 501)
                            throw std::runtime_error("Script 26 did not release the guard gate after the retail wait.");
                        update(0);
                        const auto& cooperScene = route.locoCooperIntro;
                        if (render.activeMissionCinematic != &cooperScene || !render.postFightCinematic ||
                            !render.cooperGeometry || !render.cooperActor || render.cooperVertices.empty())
                            throw std::runtime_error("Script 26 did not start the Cooper cinematic after the guard.");
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || !render.locoCooperScript || !render.locoCooper ||
                            !render.locoCooperObjective.descriptor || (!guard.disablePending && !guard.disabled) ||
                            !playback.vehicle)
                            throw std::runtime_error("Script 26 did not hand off the Cooper objective.");
                        const auto chaseStart = render.locoCooper->position;
                        const int cashBeforeEscape = playback.playerCash;
                        playback.player->position = chaseStart;
                        playback.player->position[0] += 19999;
                        update(0);
                        if (!render.locoCooperScript || playback.playerCash != cashBeforeEscape ||
                            !render.locoCooper->occupied)
                            throw std::runtime_error("Script 26 did not wait for the live Cooper escape distance.");
                        playback.player->position[0] += 1;
                        update(0);
                        update(0);
                        if (render.locoCooperScript || !playback.openingMissionsPaid[3] ||
                            playback.playerCash != cashBeforeEscape + 300 ||
                            !render.missionRewardCash[3].descriptor || !render.missionCompleteObjectives[3].descriptor ||
                            !render.missionRewardMs || render.missionRewardIndex != 3 || !render.nextMissionMarker ||
                            (render.achievements && render.achievements->stat(49) != 4))
                            throw std::runtime_error("Script 26 did not pay the retail escape reward once.");
                    }
                    if (postLc && !postBrawl) {
                        auto& render = *cityRender;
                        auto& playback = render.playback;
                        const auto& route = *render.opening.payphone->postFight.nextCheckpoint;
                        playback.player->position = route.marker.position;
                        update(0);
                        if (render.missionTitleIndex != 4 || !render.nextMissionFadeMs)
                            throw std::runtime_error("Script 27 did not start at the LC checkpoint.");
                        update(route.cooperNextFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[4].label.descriptor)
                            throw std::runtime_error("Script 27 did not present its retail title.");
                        update(4000);
                        if (render.activeMissionCinematic != &route.cooperNextIntro || !render.postFightCinematic)
                            throw std::runtime_error("Script 27 did not enter its opening cinematic.");
                        for (int step = 0; step < 64 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (!render.hummerRouteScript || render.hummerRouteMarker || !playback.vehicle ||
                            !render.cooperNextDriveObjective.descriptor || !render.cooperNextDriveFirstSurface)
                            throw std::runtime_error("Script 27 did not wait for entry into the protected Hummer.");
                        playback.player->position = playback.vehicle->position;
                        playback.drive(render.opening, render.terrain, {}, 0, true);
                        update(0);
                        if (!render.hummerRouteMarker)
                            throw std::runtime_error("Script 27 did not activate its drive checkpoint upon Hummer entry.");
                        auto& hummer = *playback.vehicle;
                        hummer.position = route.cooperNextDriveCheckpoint->position;
                        const auto ground = render.terrain.groundContact(hummer.position, std::nullopt, 300, 300);
                        if (!ground) throw std::runtime_error("Script 27 checkpoint ground is missing.");
                        hummer.position[2] = ground->height;
                        hummer.groundTriangle = ground->triangle;
                        hummer.airborne = false;
                        update(0);
                        if (render.hummerRouteScript || render.hummerRouteMarker || !playback.scriptedVehicleExit)
                            throw std::runtime_error("Script 27 did not release the drive checkpoint and request exit.");
                        for (int step = 0; step < 600 && !render.hummerArrivalFadeMs; ++step) update(16);
                        if (!render.hummerArrivalFadeMs || render.postFightCinematic || playback.scriptedVehicleExit)
                            throw std::runtime_error("Script 27 did not start fading after the Hummer exit.");
                        update(route.cooperNextArrivalFadeMs);
                        if (render.activeMissionCinematic != &route.cooperNextArrival || !render.postFightCinematic)
                            throw std::runtime_error("Script 27 did not play the C2M501 arrival cinematic.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || !render.taggedFightScript ||
                            !render.cooperNextFightObjective.descriptor || playback.missionFailure)
                            throw std::runtime_error("Script 27 did not reach the next objective after C2M501.");
                        update(0);
                        if (playback.locoOpponents.size() != 3)
                            throw std::runtime_error("Script 27 did not spawn all three kill targets.");
                        for (const auto& [name, opponent] : playback.locoOpponents) {
                            if (!opponent.taggedForKill || opponent.health <= 0 || opponent.disabled)
                                throw std::runtime_error("Script 27 did not tag an active fight target.");
                        }
                        playback.locoOpponents.at("Loco_00").health = 0;
                        playback.locoOpponents.at("Loco_01").health = 0;
                        auto& lastOpponent = playback.locoOpponents.at("Loco_02");
                        update(1000);
                        if (!render.taggedFightScript || !lastOpponent.taggedForKill ||
                            lastOpponent.disablePending || lastOpponent.disabled)
                            throw std::runtime_error("Script 27 released the fight while a target was alive.");
                        playback.player->position = lastOpponent.pose.position;
                        lastOpponent.health = 1;
                        playback.punchLocoOpponent(render.opening, render.terrain, 0, true, true);
                        for (int step = 0; step < 20 && lastOpponent.health > 0; ++step)
                            playback.punchLocoOpponent(render.opening, render.terrain, 50, false, false);
                        if (lastOpponent.health != 0) throw std::runtime_error("Script 27 last target survived lethal damage.");
                        update(0);
                        if (!render.taggedFightScript || lastOpponent.taggedForKill)
                            throw std::runtime_error("Script 27 did not clear tags before its retail wait.");
                        for (int step = 0; step < 3; ++step) update(1000);
                        if (!render.taggedFightScript) throw std::runtime_error("Script 27 fight wait ended too early.");
                        update(1);
                        if (render.taggedFightScript) throw std::runtime_error("Script 27 fight did not finish its retail wait.");
                        const auto cashBeforeClosing = playback.playerCash;
                        update(0);
                        if (render.activeMissionCinematic != &route.cooperNextClosing || !render.postFightCinematic ||
                            playback.playerCash != cashBeforeClosing)
                            throw std::runtime_error("Script 27 did not start its closing cinematic after the fight.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !playback.openingMissionsPaid[4] ||
                            playback.playerCash != cashBeforeClosing + 400 ||
                            !render.missionRewardCash[4].descriptor || !render.missionCompleteObjectives[4].descriptor ||
                            !render.missionRewardMs || render.missionRewardIndex != 4 || !render.nextMissionMarker ||
                            (render.achievements && render.achievements->stat(49) != 5))
                            throw std::runtime_error("Script 27 did not award its retail payout once and hand off to script 28.");
                    }
                    if (postLc && !postBasketball) {
                        auto& render = *cityRender;
                        auto& playback = render.playback;
                        const auto& route = *render.opening.payphone->postFight.nextCheckpoint;
                        playback.player->position = route.marker.position;
                        update(0);
                        if (render.missionTitleIndex != 5 || !render.nextMissionFadeMs)
                            throw std::runtime_error("Script 28 did not start at the retail LC checkpoint.");
                        update(route.brawlNextFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[5].label.descriptor)
                            throw std::runtime_error("Script 28 did not present its retail title.");
                        update(4000);
                        if (render.activeMissionCinematic != &route.brawlNextIntro || !render.postFightCinematic)
                            throw std::runtime_error("Script 28 did not play its opening scene.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !render.basketballRouteScript || !render.basketballMarker ||
                            !render.brawlNextObjective.descriptor || !render.basketballFirstSurface)
                            throw std::runtime_error("Script 28 did not activate its basketball checkpoint.");
                        playback.player->position = route.basketballCheckpoint->position;
                        update(0);
                        if (render.basketballMarker || render.basketballRouteScript ||
                            render.pendingHummerCinematic != &route.basketballArrival)
                            throw std::runtime_error("Script 28 did not release the basketball marker.");
                        update(0);
                        if (render.activeMissionCinematic != &route.basketballArrival || !render.postFightCinematic)
                            throw std::runtime_error("Script 28 did not start the basketball cutscene.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || !render.taggedFightScript ||
                            !render.basketballFightObjective.descriptor || playback.locoOpponents.size() != 3)
                            throw std::runtime_error("Script 28 did not spawn the basketball fight opponents.");
                        update(0);
                        update(1000);
                        if (render.basketballTutorialVisible)
                            throw std::runtime_error("Script 28 showed the basketball tutorial before the first wait expired.");
                        update(1);
                        if (!render.basketballTutorialVisible || !render.basketballTutorial.descriptor ||
                            playback.locoOpponents.at("LocoGuard_00").taggedForKill)
                            throw std::runtime_error("Script 28 did not show its tutorial before tagging the guards.");
                        update(1000);
                        update(1000);
                        auto& lastGuard = playback.locoOpponents.at("LocoGuard_02");
                        for (auto& [name, guard] : playback.locoOpponents) {
                            if (!guard.taggedForKill || guard.disabled || guard.health <= 0)
                                throw std::runtime_error("Script 28 did not tag each authored guard.");
                            if (&guard != &lastGuard) guard.health = 0;
                        }
                        update(16);
                        if (!render.taggedFightScript || !lastGuard.taggedForKill)
                            throw std::runtime_error("Script 28 cleared the fight before the last guard fell.");
                        playback.player->position = lastGuard.pose.position;
                        lastGuard.health = 1;
                        playback.punchLocoOpponent(render.opening, render.terrain, 0, true, true);
                        for (int step = 0; step < 20 && lastGuard.health > 0; ++step)
                            playback.punchLocoOpponent(render.opening, render.terrain, 50, false, false);
                        if (lastGuard.health != 0) throw std::runtime_error("Script 28 last guard survived lethal damage.");
                        update(0);
                        if (render.taggedFightScript || lastGuard.taggedForKill ||
                            render.pendingHummerCinematic != &route.basketballClosing)
                            throw std::runtime_error("Script 28 did not clear the tags and queue its closing scene.");
                        const auto cashBeforeClosing = playback.playerCash;
                        update(0);
                        if (render.activeMissionCinematic != &route.basketballClosing ||
                            !render.basketballTutorialVisible || playback.playerCash != cashBeforeClosing ||
                            route.basketballClosing.cameras.size() != 2 ||
                            route.basketballClosing.dialogue.size() != 3 || route.basketballUnlockedScript != 29)
                            throw std::runtime_error("Script 28 did not start the authored basketball closing scene.");
                        for (int step = 0; step < 120 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || render.basketballTutorialVisible ||
                            !playback.openingMissionsPaid[5] || playback.playerCash != cashBeforeClosing + 500 ||
                            !render.nextMissionScript || !render.nextMissionMarker ||
                            !render.missionRewardCash[5].descriptor || !render.missionCompleteObjectives[5].descriptor ||
                            render.missionRewardIndex != 5 || (render.achievements && render.achievements->stat(49) != 6))
                            throw std::runtime_error("Script 28 did not clear the tutorial and award its retail payout once.");
                    }
                    if (postBasketball && !postDelivery) {
                        auto& render = *cityRender;
                        const auto& route = *render.opening.payphone->postFight.nextCheckpoint;
                        render.playback.player->position = route.marker.position;
                        update(0);
                        if (render.missionTitleIndex != 6 || !render.nextMissionFadeMs)
                            throw std::runtime_error("Script 29 did not start at the retail LC checkpoint.");
                        update(route.postBasketballFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[6].label.descriptor)
                            throw std::runtime_error("Script 29 did not present its retail title.");
                        update(4000);
                        if (render.activeMissionCinematic != &route.postBasketballIntro ||
                            !render.postFightCinematic || route.postBasketballIntro.dialogue.empty() ||
                            !route.postBasketballDestination)
                            throw std::runtime_error("Script 29 did not start the authored opening scene.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !render.postBasketballRouteScript ||
                            !render.postBasketballMarker || !render.postBasketballObjective.descriptor ||
                            !render.postBasketballFirstSurface || render.checkpointMeshes[15].empty())
                            throw std::runtime_error("Script 29 did not activate its first objective.");
                        auto& playback = render.playback;
                        playback.player->position = route.postBasketballDestination->position;
                        update(0);
                        if (render.postBasketballRouteScript || render.postBasketballMarker ||
                            !render.postBasketballVehicleScript || !playback.vehicle ||
                            playback.locoOpponents.size() != 4 ||
                            render.opening.actors.at(*render.opening.vehicleActor).name != route.postBasketballVehicle)
                            throw std::runtime_error("Script 29 did not stage its car and guards.");
                        playback.vehicle->occupied = true;
                        update(0);
                        if (render.postBasketballVehicleScript || !playback.nextMissionVehicleAcquired ||
                            render.activeMissionCinematic != &route.postBasketballAmbush || !render.postFightCinematic ||
                            route.postBasketballAmbush.cameras.size() != 4 || route.postBasketballAmbush.motions.size() != 3)
                            throw std::runtime_error("Script 29 did not start the authored ambush from its required vehicle.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || !render.postBasketballEscapeScript ||
                            !render.postBasketballAmbushObjective.descriptor || !playback.vehicle->occupied ||
                            playback.locoOpponents.size() != 5 ||
                            !std::all_of(playback.locoOpponents.begin(), playback.locoOpponents.end(),
                                         [](const auto& entry) { return entry.second.taggedForKill; }))
                            throw std::runtime_error("Script 29 ambush did not restore the car and tag its enemies.");
                        update(400);
                        if (!render.postBasketballEscapeScript || render.pendingHummerCinematic)
                            throw std::runtime_error("Script 29 escape advanced before leaving the enemies.");
                        auto& car = *playback.vehicle;
                        const auto ground = render.terrain.groundContact(route.marker.position);
                        if (!ground) throw std::runtime_error("Missing escape test ground.");
                        car.position = route.marker.position;
                        car.position[2] = ground->height;
                        car.groundTriangle = ground->triangle;
                        update(0);
                        if (render.postBasketballEscapeScript ||
                            render.pendingHummerCinematic != &route.postBasketballBriefing || !car.occupied)
                            throw std::runtime_error("Script 29 did not queue its escape briefing.");
                        update(route.postBasketballBriefingFadeMs);
                        for (int step = 0; step < 240 && !render.postFightCinematic; ++step) update(16);
                        if (render.activeMissionCinematic != &route.postBasketballBriefing ||
                            !render.postFightCinematic || playback.vehicle->occupied ||
                            route.postBasketballBriefing.dialogue.size() != 2)
                            throw std::runtime_error("Script 29 did not exit the car and start its briefing.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (!render.repairDriveScript || !render.repairDriveMarker ||
                            !render.repairDriveFirstSurface || !render.repairDriveObjective.descriptor)
                            throw std::runtime_error("Script 29 did not activate the repair drive.");
                        const auto& repair = *route.repairDriveCheckpoint;
                        const auto repairGround = render.terrain.groundContact(repair.position);
                        if (!repairGround) throw std::runtime_error("Missing repair drive test ground.");
                        car.position = repair.position;
                        car.position[2] = repairGround->height;
                        car.groundTriangle = repairGround->triangle;
                        playback.player->position = car.position;
                        update(0);
                        if (!render.repairDriveMarker)
                            throw std::runtime_error("Script 29 admitted the player to the repair checkpoint on foot.");
                        car.occupied = true;
                        update(0);
                        if (render.repairDriveScript || render.repairDriveMarker ||
                            render.pendingHummerCinematic != &route.repairArrival)
                            throw std::runtime_error("Script 29 did not accept the required car at the repair checkpoint.");
                        for (int step = 0; step < 240 && !render.postFightCinematic; ++step) update(16);
                        if (render.activeMissionCinematic != &route.repairArrival ||
                            route.repairArrival.cameras.size() != 2 || route.repairArrival.dialogue.size() != 1)
                            throw std::runtime_error("Script 29 did not start the repair arrival cinematic.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        if (!render.repairTutorialVisible || !render.repairTutorial.descriptor || car.occupied ||
                            !render.repairShopMarker || !render.repairShopFirstSurface || render.checkpointMeshes[17].empty())
                            throw std::runtime_error("Script 29 did not show the retail repair tutorial.");
                        const auto& shop = *route.repairShopEntrance;
                        const auto shopGround = render.terrain.groundContact(shop.position);
                        if (!shopGround) throw std::runtime_error("Missing repair shop ground.");
                        car.position = shop.position;
                        car.position[2] = shopGround->height;
                        car.groundTriangle = shopGround->triangle;
                        car.occupied = true;
                        car.health = car.maxHealth / 2;
                        car.frontDeformation = 0.5f;
                        const auto cashBeforeRepair = playback.playerCash;
                        update(0);
                        if (render.repairShop != CityRender::RepairShop::confirm ||
                            !render.repairPaymentPrompt.descriptor || render.repairShopMarker || render.repairQuote <= 0 ||
                            playback.playerCash != cashBeforeRepair)
                            throw std::runtime_error("Script 29 did not quote the damaged car repair.");
                        touch->message(window, WM_KEYDOWN, VK_RETURN, 0);
                        update(0);
                        touch->message(window, WM_KEYUP, VK_RETURN, 0);
                        if (render.repairShop != CityRender::RepairShop::repairing ||
                            playback.playerCash != cashBeforeRepair - render.repairQuote || car.health == car.maxHealth)
                            throw std::runtime_error("Script 29 did not charge exactly once before repair.");
                        update(250);
                        update(1);
                        update(1250);
                        if (render.repairShop != CityRender::RepairShop::inactive || car.health != car.maxHealth ||
                            car.frontDeformation != 0 || playback.playerCash != cashBeforeRepair - render.repairQuote ||
                            render.achievements->stat(13) != 1 || render.achievements->stat(19) != render.repairQuote)
                            throw std::runtime_error("Script 29 did not complete the paid repair and record its stats.");
                        update(0);
                        if (!render.deliveryScript || !render.deliveryMarker || !render.deliveryObjective.descriptor ||
                            !render.deliveryFirstSurface || render.checkpointMeshes[18].empty())
                            throw std::runtime_error("Script 29 did not activate the authored delivery checkpoint.");
                        const auto& destination = *route.deliveryCheckpoint;
                        const auto deliveryGround = render.terrain.groundContact(destination.position);
                        if (!deliveryGround) throw std::runtime_error("Missing delivery test ground.");
                        car.position = destination.position;
                        car.position[2] = deliveryGround->height;
                        car.groundTriangle = deliveryGround->triangle;
                        const auto cashBeforeDelivery = playback.playerCash;
                        update(0);
                        if (render.deliveryScript || render.deliveryMarker || !render.postFightCinematic ||
                            render.activeMissionCinematic != &route.deliveryClosing ||
                            route.deliveryClosing.dialogue.size() != 3 || route.deliveryClosing.cameras.size() != 3)
                            throw std::runtime_error("Script 29 did not start the delivery closing scene.");
                        for (int step = 0; step < 120 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || playback.vehicle || !playback.openingMissionsPaid[6] ||
                            playback.playerCash != cashBeforeDelivery + route.postBasketballTitle->cashReward ||
                            render.missionRewardIndex != 6 || !render.missionRewardCash[6].descriptor ||
                            render.achievements->stat(49) != 7 ||
                            route.deliveryUnlockedScripts != std::vector<uint32_t>{30, 21} ||
                            !render.nextMissionMarker || !render.nextMissionScript)
                            throw std::runtime_error("Script 29 did not pay the delivery reward and finish.");
                    }
                    if (postDelivery && !postCooperDelivery) {
                        auto& render = *cityRender;
                        const auto& route = *render.opening.payphone->postFight.nextCheckpoint;
                        if (!render.elToroFirstSurface || render.checkpointMeshes[19].empty())
                            throw std::runtime_error("Missing retail El Toro checkpoint geometry.");
                        render.playback.player->position = route.marker.position;
                        update(0);
                        if (!render.nextMissionMarker || render.nextMissionFadeMs)
                            throw std::runtime_error("Script 30 incorrectly started at the old LC checkpoint.");
                        render.playback.player->position = route.elToroCheckpoint->position;
                        update(0);
                        if (render.missionTitleIndex != 7 || !render.nextMissionFadeMs || render.nextMissionMarker)
                            throw std::runtime_error("Script 30 did not start at the retail El Toro checkpoint.");
                        update(route.elToroFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[7].label.descriptor)
                            throw std::runtime_error("Script 30 did not present its retail title.");
                        update(4000);
                        if (render.activeMissionCinematic != &route.elToroIntro ||
                            !render.postFightCinematic || route.elToroIntro.dialogue.size() != 2 ||
                            !route.elToroDestination || route.elToroObjective.empty())
                            throw std::runtime_error("Script 30 did not start the authored opening scene.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !render.elToroDestinationScript ||
                            !render.elToroDestinationMarker || !render.elToroObjective.descriptor ||
                            !render.elToroDestinationFirstSurface || render.checkpointMeshes[20].empty())
                            throw std::runtime_error("Script 30 did not activate the authored destination.");
                        auto& playback = render.playback;
                        playback.player->position = route.elToroDestination->position;
                        update(0);
                        if (render.elToroDestinationMarker || render.elToroDestinationScript ||
                            render.pendingHummerCinematic != &route.elToroArrival)
                            throw std::runtime_error("Script 30 did not release its destination marker.");
                        update(0);
                        if (render.activeMissionCinematic != &route.elToroArrival || !render.postFightCinematic ||
                            route.elToroArrival.motions.size() != 5 || route.elToroArrival.dialogue.size() != 6)
                            throw std::runtime_error("Script 30 did not start the authored arrival scene.");
                        for (int step = 0; step < 160 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || !render.taggedFightScript ||
                            !render.elToroFightObjective.descriptor || playback.locoOpponents.size() != 2)
                            throw std::runtime_error("Script 30 did not stage the two authored opponents.");
                        update(1000);
                        update(201);
                        auto& first = playback.locoOpponents.at("Loco_00");
                        auto& last = playback.locoOpponents.at("Loco_01");
                        if (!first.taggedForKill || !last.taggedForKill)
                            throw std::runtime_error("Script 30 did not tag both opponents.");
                        first.health = 0;
                        update(16);
                        if (!render.taggedFightScript || !last.taggedForKill)
                            throw std::runtime_error("Script 30 cleared the fight before both opponents fell.");
                        last.health = 0;
                        update(16);
                        update(1000);
                        if (render.taggedFightScript || last.taggedForKill ||
                            render.pendingHummerCinematic != &route.elToroInterlude)
                            throw std::runtime_error("Script 30 did not queue the interlude after its authored wait.");
                        update(0);
                        if (render.activeMissionCinematic != &route.elToroInterlude || !render.postFightCinematic ||
                            route.elToroInterlude.cameras.size() != 2 || route.elToroInterlude.dialogue.size() != 3)
                            throw std::runtime_error("Script 30 did not start Latisha's interlude.");
                        for (int step = 0; step < 120 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !render.taggedFightScript || playback.locoOpponents.size() != 3 ||
                            !std::all_of(playback.locoOpponents.begin(), playback.locoOpponents.end(),
                                         [](const auto& entry) { return entry.second.taggedForKill && entry.second.health > 0; }))
                            throw std::runtime_error("Script 30 did not restage and tag the second group.");
                        auto& lastSecond = playback.locoOpponents.at("Loco_02");
                        playback.locoOpponents.at("Loco_00").health = 0;
                        playback.locoOpponents.at("Loco_01").health = 0;
                        update(16);
                        if (!render.taggedFightScript || !lastSecond.taggedForKill)
                            throw std::runtime_error("Script 30 cleared the second fight before all three fell.");
                        lastSecond.health = 0;
                        update(16);
                        update(1000);
                        if (render.taggedFightScript || lastSecond.taggedForKill ||
                            render.pendingHummerCinematic != &route.elToroDeparture)
                            throw std::runtime_error("Script 30 did not queue the Cooper departure after the second fight.");
                        update(0);
                        if (render.activeMissionCinematic != &route.elToroDeparture || !render.postFightCinematic ||
                            route.elToroDeparture.motions.size() != 4 || route.elToroDeparture.dialogue.size() != 1)
                            throw std::runtime_error("Script 30 did not start the authored Cooper departure.");
                        for (int step = 0; step < 120 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !playback.vehicle || !playback.vehicle->occupied ||
                            render.opening.vehicleActor != render.cooperActor || !render.elToroDriveScript ||
                            !render.elToroDriveMarker || !render.elToroDriveObjective.descriptor ||
                            !render.elToroDriveFirstSurface || render.checkpointMeshes[21].empty())
                            throw std::runtime_error("Script 30 did not stage the occupied Cooper and its drive objective.");
                        auto& car = *playback.vehicle;
                        const auto& gate = *route.elToroDriveCheckpoint;
                        const auto ground = render.terrain.groundContact(gate.position);
                        if (!ground) throw std::runtime_error("Missing retail Cooper drive test ground.");
                        car.position = gate.position;
                        car.position[2] = ground->height;
                        car.groundTriangle = ground->triangle;
                        playback.player->position = car.position;
                        car.occupied = false;
                        update(0);
                        if (!render.elToroDriveMarker)
                            throw std::runtime_error("Script 30 accepted the Cooper destination on foot.");
                        car.occupied = true;
                        update(0);
                        if (render.elToroDriveMarker || render.elToroDriveScript || !playback.scriptedVehicleExit)
                            throw std::runtime_error("Script 30 did not accept the Cooper at its destination.");
                        const auto cashBeforeFinale = playback.playerCash;
                        for (int step = 0; step < 320 && !render.postFightCinematic; ++step) update(16);
                        if (render.activeMissionCinematic != &route.elToroClosing || !render.postFightCinematic ||
                            (playback.vehicle && playback.vehicle->occupied) || route.elToroClosing.dialogue.size() != 6)
                            throw std::runtime_error("Script 30 did not start the Cooper delivery finale.");
                        for (int step = 0; step < 180 && render.postFightCinematic; ++step) update(250, true);
                        if (render.postFightCinematic || playback.vehicle || !playback.openingMissionsPaid[7] ||
                            playback.playerCash != cashBeforeFinale + route.elToroTitle->cashReward ||
                            render.missionRewardIndex != 7 || !render.missionRewardCash[7].descriptor ||
                            render.achievements->stat(49) != 8 || !render.nextMissionScript || !render.nextMissionMarker)
                            throw std::runtime_error("Script 30 did not pay and unlock its successor.");
                    }
                    if (postCooperDelivery) {
                        auto& render = *cityRender;
                        const auto& route = *render.opening.payphone->postFight.nextCheckpoint;
                        render.playback.player->position = route.elToroCheckpoint->position;
                        update(0);
                        if (render.missionTitleIndex != 8 || !render.nextMissionFadeMs)
                            throw std::runtime_error("Script 31 did not start at El Toro after Continue.");
                        update(route.vanEntranceFadeMs);
                        if (!render.missionTitleMs || !render.missionTitles[8].label.descriptor)
                            throw std::runtime_error("Script 31 did not present its retail title.");
                        update(4000);
                        if (render.activeMissionCinematic != &route.vanMissionIntro || !render.postFightCinematic ||
                            route.vanMissionIntro.dialogue.size() != 2 || route.vanMissionObjective.empty())
                            throw std::runtime_error("Script 31 did not start its authored opening.");
                        for (int step = 0; step < 120 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        auto& playback = render.playback;
                        if (render.postFightCinematic || !render.vanAcquisitionScript || !playback.vehicle ||
                            playback.vehicle->occupied || playback.nextMissionVehicleAcquired ||
                            !render.vanMissionObjective.descriptor ||
                            render.opening.actors.at(*render.opening.vehicleActor).name != "aVan_00")
                            throw std::runtime_error("Script 31 did not stage and track the required van.");
                        playback.player->position = playback.vehicle->position;
                        touch->message(window, WM_KEYDOWN, 'E', 0);
                        update(0);
                        touch->message(window, WM_KEYUP, 'E', 0);
                        if (!playback.vehicle->occupied || !playback.nextMissionVehicleAcquired || !render.vanAcquisitionScript)
                            throw std::runtime_error("Script 31 did not accept entry to its van.");
                        for (int step = 0; step < 7; ++step) update(1000);
                        if (!render.vanAcquisitionScript || playback.scriptedVehicleExit || render.pendingHummerCinematic)
                            throw std::runtime_error("Script 31 skipped the authored van wait.");
                        update(1);
                        if (render.vanAcquisitionScript || !playback.scriptedVehicleExit ||
                            render.pendingHummerCinematic != &route.vanBriefing)
                            throw std::runtime_error("Script 31 did not queue its van briefing.");
                        for (int step = 0; step < 320 && !render.postFightCinematic; ++step) update(16);
                        if (render.activeMissionCinematic != &route.vanBriefing || !render.postFightCinematic ||
                            playback.vehicle->occupied || route.vanBriefing.dialogue.size() != 1)
                            throw std::runtime_error("Script 31 did not exit the van and start the briefing.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (!render.vanDriveScript || !render.vanDriveMarker || !render.vanDriveObjective.descriptor ||
                            !render.vanDriveFirstSurface || render.checkpointMeshes[22].empty())
                            throw std::runtime_error("Script 31 did not activate its drive objective.");
                        const auto& gate = *route.vanDriveCheckpoint;
                        const auto ground = render.terrain.groundContact(gate.position);
                        if (!ground) throw std::runtime_error("Missing retail van drive test ground.");
                        auto& van = *playback.vehicle;
                        van.position = gate.position;
                        van.position[2] = ground->height;
                        van.groundTriangle = ground->triangle;
                        playback.player->position = van.position;
                        update(0);
                        if (!render.vanDriveMarker)
                            throw std::runtime_error("Script 31 accepted its van checkpoint on foot.");
                        touch->message(window, WM_KEYDOWN, 'E', 0);
                        update(0);
                        touch->message(window, WM_KEYUP, 'E', 0);
                        if (render.vanDriveMarker || render.vanDriveScript || !playback.scriptedVehicleExit)
                            throw std::runtime_error("Script 31 did not accept the required van at its destination.");
                        for (int step = 0; step < 360 && !render.postFightCinematic; ++step) update(16);
                        if (render.activeMissionCinematic != &route.vanArrival || !render.postFightCinematic ||
                            van.occupied || route.vanArrival.cameras.size() != 5 || route.vanArrival.motions.size() != 1)
                            throw std::runtime_error("Script 31 did not start the authored van arrival.");
                        for (int step = 0; step < 160 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !render.vanReentryScript || van.occupied)
                            throw std::runtime_error("Script 31 did not wait for reentry to the van.");
                        playback.player->position = van.position;
                        touch->message(window, WM_KEYDOWN, 'E', 0);
                        update(0);
                        touch->message(window, WM_KEYUP, 'E', 0);
                        update(100);
                        if (!render.vanReentryScript || render.postFightCinematic || !van.occupied)
                            throw std::runtime_error("Script 31 skipped the delay after reentry.");
                        update(1);
                        if (render.vanReentryScript || !render.postFightCinematic ||
                            render.activeMissionCinematic != &route.vanReentryBriefing ||
                            route.vanReentryBriefing.dialogue.size() != 1 || !route.vanSecondDriveCheckpoint)
                            throw std::runtime_error("Script 31 did not play the retail reentry briefing.");
                        for (int step = 0; step < 96 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (!render.vanSecondDrive || !render.vanDriveScript || !render.vanDriveMarker ||
                            !render.vanSecondDriveFirstSurface || render.checkpointMeshes[23].empty())
                            throw std::runtime_error("Script 31 did not activate the second van checkpoint.");
                        const auto& secondGate = *route.vanSecondDriveCheckpoint;
                        const auto secondGround = render.terrain.groundContact(secondGate.position);
                        if (!secondGround) throw std::runtime_error("Missing second van test ground.");
                        van.position = secondGate.position;
                        van.position[2] = secondGround->height;
                        van.groundTriangle = secondGround->triangle;
                        update(0);
                        if (render.pendingHummerCinematic != &route.vanSecondArrival ||
                            render.vanDriveMarker || !playback.scriptedVehicleExit)
                            throw std::runtime_error("Script 31 did not accept its second van checkpoint.");
                        for (int step = 0; step < 360 && !render.postFightCinematic; ++step) update(16);
                        if (render.activeMissionCinematic != &route.vanSecondArrival || !render.postFightCinematic ||
                            van.occupied || route.vanSecondArrival.cameras.size() != 2 ||
                            route.vanSecondArrival.motions.size() != 2 || route.vanProtectObjective.empty())
                            throw std::runtime_error("Script 31 did not start Juan's second van scene.");
                        for (int step = 0; step < 160 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (render.postFightCinematic || !render.vanProtectionScript ||
                            !render.vanProtectObjective.descriptor || playback.locoOpponents.size() != 4)
                            throw std::runtime_error("Script 31 did not start the protection encounter.");
                        auto& first = playback.locoOpponents.at("Loco_00");
                        auto& second = playback.locoOpponents.at("Loco_01");
                        auto& juan = playback.locoOpponents.at("Juan_00");
                        const auto approachStart = first.pose.position;
                        for (int step = 0; step < 8; ++step) update(1000);
                        if (first.rangedTarget || first.taggedForKill || first.pose.position == approachStart)
                            throw std::runtime_error("Script 31 skipped the attackers' retail approach.");
                        update(1);
                        if (first.rangedTarget != "Juan_00" || second.rangedTarget != "Juan_00" ||
                            !first.taggedForKill || !second.taggedForKill)
                            throw std::runtime_error("Script 31 did not direct and tag both attackers.");
                        const auto healthBeforeShot = juan.health;
                        update(16);
                        if (juan.health >= healthBeforeShot || playback.missionFailure)
                            throw std::runtime_error("Script 31 attackers did not damage Juan.");
                        first.health = 0;
                        update(16);
                        if (!render.vanProtectionScript || !second.taggedForKill)
                            throw std::runtime_error("Script 31 released its protection gate too early.");
                        second.health = 0;
                        update(16);
                        update(1000);
                        if (render.vanProtectionScript || second.taggedForKill ||
                            render.pendingHummerCinematic != &route.vanHandshake)
                            throw std::runtime_error("Script 31 did not queue the handshake after the protection fight.");
                        update(0);
                        if (render.activeMissionCinematic != &route.vanHandshake || !render.postFightCinematic ||
                            route.vanHandshake.cameras.size() != 2 || route.vanHandshake.motions.size() != 3)
                            throw std::runtime_error("Script 31 did not start the authored handshake.");
                        for (int step = 0; step < 160 && render.postFightCinematic; ++step) update(250, true);
                        update(0);
                        if (!render.vanHandshakeScript || !van.occupied || !first.disabled || !second.disabled)
                            throw std::runtime_error("Script 31 did not board the van and hide the defeated attackers.");
                        update(100);
                        if (render.postFightCinematic)
                            throw std::runtime_error("Script 31 skipped the post-handshake wait.");
                        update(1);
                        if (render.vanHandshakeScript || !render.postFightCinematic ||
                            render.activeMissionCinematic != &route.vanHandshakeBriefing || !route.vanThirdDriveCheckpoint)
                            throw std::runtime_error("Script 31 did not start the next van briefing.");
                    }
                    std::cout << "Activated Continue from the main menu and restored the saved objective through Vulkan.\n"
                              << std::flush;
                    return;
                }
                const auto menuBeforeNavigation = menu->draw(splash->background);
                menu->navigate({1, false, false});
                menu->navigate({1, true, false});
                if (menu->draw(splash->background).bgra == menuBeforeNavigation.bgra)
                    throw std::runtime_error("Menu navigation did not present Options.");
                menu->navigate({0, true, false}, false);
                const auto volumeBefore = menu->draw(splash->background);
                menu->navigate({0, false, false, -1}, false);
                const auto volumeLowered = menu->draw(splash->background);
                menu->navigate({0, false, false, 1}, false);
                if (menu->draw(splash->background).bgra == volumeLowered.bgra &&
                    volumeLowered.bgra == volumeBefore.bgra)
                    throw std::runtime_error("Menu navigation did not adjust the volume slider.");
                menu->navigate({0, false, true});
                const auto optionsBeforeLanguage = menu->draw(splash->background);
                menu->navigate({2, true, false});
                menu->navigate({2, true, false});
                menu->navigate({1, true, false});
                const auto selectedLanguage = menu->takeLanguageRequest();
                if (!selectedLanguage || *selectedLanguage == language)
                    throw std::runtime_error("Menu navigation did not select another language.");
                menu->applyLanguage(*selectedLanguage);
                menu->advance(1000. / 30);
                if (menu->draw(splash->background).bgra == optionsBeforeLanguage.bgra)
                    throw std::runtime_error("Applying a language did not update the Options screen.");
                menu->applyLanguage(language);
                menu->navigate({0, false, true});
                menu->navigate({0, true, false});
                if (!menu->takeNewGameRequest())
                    throw std::runtime_error("Menu navigation did not activate New Game.");
                const auto marked = rasterCaption({0, 0, {}, "White \\0yellow\\1"}, true, true);
                const auto yellow = [](uint32_t pixel) {
                    return (pixel >> 24) && ((pixel >> 16) & 255) > (pixel & 255) + 30;
                };
                const auto white = [](uint32_t pixel) {
                    return (pixel >> 24) && (pixel & 0xffffff) == 0xffffff;
                };
                if (std::none_of(marked.bgra.begin(), marked.bgra.end(), yellow) ||
                    std::none_of(marked.bgra.begin(), marked.bgra.end(), white))
                    throw std::runtime_error("Retail caption failed to preserve its inline colors.");
                update(0);
                if (!cityRender || !cityRender->startup)
                    throw std::runtime_error("Opening smoke test did not enter New Game.");
                auto distantDraws = cityRender->distantDraws;
                bool barsPresented = false;
                for (unsigned frame = 0; frame < 160 && cityRender->startup; ++frame) {
                    update(250);
                    distantDraws = std::max(distantDraws, cityRender->distantDraws);
                    if (cityRender->startup && cityRender->startup->showsBars() &&
                        cityRender->cinematicBars.descriptor && cityRender->startup->brightness() > 0 &&
                        cityRender->startup->canSkip() && cityRender->cinematicSkip.descriptor) {
                        barsPresented = true;
                        RECT client{};
                        if (!GetClientRect(window, &client))
                            throw std::runtime_error("Could not locate the Skip control for the opening smoke test.");
                        const auto position = MAKELPARAM((480 - skipArtworkWidth / 2) * client.right / 480,
                                                         (skipHeight / 2) * client.bottom / 320);
                        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, position);
                        SendMessageW(window, WM_LBUTTONUP, 0, position);
                        update(0);
                        break;
                    }
                }
                if (cityRender->lowDetailEndSurface > cityRender->lowDetailFirstSurface) {
                    if (distantDraws == 0)
                        throw std::runtime_error("Opening smoke test did not draw any distant city surfaces.");
                    std::cout << "Submitted up to " << distantDraws
                              << " distant city surfaces in a startup frame.\n";
                }
                if (cityRender->startup || !barsPresented)
                    throw std::runtime_error("Opening smoke test did not present the bars and reach the script handoff via Skip.");
                std::cout << "Presented the opening cinematic bars and reached the script handoff via Skip.\n" << std::flush;
                update(500);
                if (!cityRender->missionTitleMs || !cityRender->missionTitles[0].label.descriptor)
                    throw std::runtime_error("Opening smoke test did not present the mission title.");
                std::cout << "Presented the retail mission title through Vulkan.\n" << std::flush;
                for (unsigned frame = 0; frame < 16 && cityRender->missionTitleMs; ++frame)
                    update(250);
                if (!cityRender->missionRevealMs || *cityRender->missionRevealMs != 0)
                    throw std::runtime_error("Opening smoke test did not reach the mission reveal.");
                update(500);
                if (!cityRender->missionRevealMs || *cityRender->missionRevealMs != 500 ||
                    cityOpeningBrightness(cityRender->opening, cityRender->playback.elapsedMs) <= 0)
                    throw std::runtime_error("Opening smoke test did not present the mission reveal.");
                std::cout << "Presented the retail mission reveal through Vulkan.\n" << std::flush;
                for (unsigned frame = 0; frame < 40 && !cityRender->playback.activeDialogue; ++frame)
                    update(250);
                const auto firstLine = cityRender->playback.activeDialogue;
                if (firstLine != 0 || cityRender->playback.player ||
                    !cityRender->portraits.at(cityRender->opening.dialogue.front().portrait).descriptor)
                    throw std::runtime_error("Opening smoke test did not present the first dialogue portrait.");
                std::cout << "Presented the first retail dialogue portrait through Vulkan.\n" << std::flush;

                // Render a fixture in place without advancing the mission or sending movement input.
                auto& render = *cityRender;
                const auto& opening = render.opening;
                if (!opening.checkpoint || !opening.vehicleActor)
                    throw std::runtime_error("Opening smoke test did not load the vehicle objective.");
                auto& playback = render.playback;
                const auto& car = opening.actors.at(*opening.vehicleActor);
                render.missionRevealMs.reset();
                playback.activeDialogue.reset();
                playback.elapsedMs = opening.durationMs;
                playback.vehicle = CityVehiclePose{car.position, car.rotationDegrees[2]};
                updateCityVehicleSuspension(car, *playback.vehicle, render.wheels, render.terrain, 16);
                if (playback.vehicle->suspension.size() != 4)
                    throw std::runtime_error("Opening smoke test did not update all four wheel suspensions.");
                const auto cameraYaw = car.rotationDegrees[2];
                playback.player = CityPlayerPose{car.position, cameraYaw};
                playback.player->followCameraYaw = cameraYaw;
                const auto activation = opening.checkpoint->nextTutorial.startMs;
                if (presentCity(render, info.imageExtent))
                    throw std::runtime_error("Vehicle objective was drawn before activation.");
                playback.timeAfterCheckpointMs = activation;
                if (!presentCity(render, info.imageExtent))
                    throw std::runtime_error("Vulkan did not draw the active vehicle objective.");
                playback.enteredVehicle = true;
                if (presentCity(render, info.imageExtent))
                    throw std::runtime_error("Vehicle objective was drawn after entry.");
                std::cout << "Presented the active vehicle objective through Vulkan and checked both hidden states.\n"
                          << std::flush;

                size_t lightDraws = 0;
                presentCity(render, info.imageExtent, &lightDraws);
                if (lightDraws != 0) throw std::runtime_error("Vehicle lights were drawn in the reset state.");
                for (const float speed : {100.f, -100.f}) {
                    playback.vehicle->speed = speed;
                    playback.vehicle->updateLights(true);
                    playback.vehicle->speed = 0;
                    playback.vehicle->updateLights(true, 100);
                    presentCity(render, info.imageExtent, &lightDraws);
                    if (lightDraws != 2) throw std::runtime_error("Vulkan did not draw the selected light pair.");
                }
                playback.vehicle->speed = 0;
                std::cout << "Submitted the retail forward-braking and reverse light pairs through Vulkan.\n" << std::flush;
                playback.vehicle->frontDeformation = 0.6f;
                playback.vehicle->rearDeformation = 0.4f;
                playback.vehicle->exploded = true;
                const auto& explosion = render.vehicleEffects.back();
                for (unsigned frame = 0; frame < 20 && explosion.simulation.particles().empty(); ++frame)
                    updateVehicleEffects(render, 16);
                if (!render.explosionPipeline || explosion.simulation.particles().empty())
                    throw std::runtime_error("Could not render the retail additive explosion fixture.");
                presentCity(render, info.imageExtent, &lightDraws);
                if (lightDraws != 0) throw std::runtime_error("Vehicle lights were drawn on the wreck.");
                std::cout << "Presented the retail explosion with additive blending.\n" << std::flush;
                auto& payphone = render.payphoneCinematic.emplace();
                const auto& route = *opening.payphone;
                payphone.advance(route, route.cinematicDialogue.front().startMs, false, {});
                if (payphone.activeDialogue() != 0 ||
                    !render.portraits.at(route.cinematicDialogue.front().portrait).descriptor ||
                    !render.captions[render.missionCaptionBases.at(&route.postFight) -
                                     route.cinematicDialogue.size()].descriptor)
                    throw std::runtime_error("Payphone smoke frame did not load its first dialogue.");
                presentCity(render, info.imageExtent);
                render.payphoneCinematic.reset();
                std::cout << "Submitted the retail payphone camera and portrait through Vulkan.\n" << std::flush;
                render.payphoneGameplayMs = route.cinematicDurationMs;
                if (!render.payphoneObjective.descriptor)
                    throw std::runtime_error("Payphone gameplay handoff did not load the fight objective.");
                render.payphoneLockScript.emplace(CityScriptProgram{{{108, route.lockCommandPayload}}});
                presentCity(render, info.imageExtent);
                render.payphoneLockScript.reset();
                render.activeMissionCinematic = &route.postFight;
                auto& postFight = render.postFightCinematic.emplace();
                postFight.advance(route.postFight, route.postFight.dialogue.front().startMs, false, {});
                if (postFight.activeDialogue() != 0 ||
                    !render.captions[render.missionCaptionBases.at(&route.postFight)].descriptor)
                    throw std::runtime_error("Post-fight smoke frame did not load its opening dialogue.");
                presentCity(render, info.imageExtent);
                render.postFightCinematic.reset();
                render.activeMissionCinematic = nullptr;
                std::cout << "Submitted the post-fight camera, actors, and dialogue through Vulkan.\n" << std::flush;
                if (!render.missionCompleteObjectives[0].descriptor || render.missionRewardFrames.size() != 6 ||
                    render.missionObjectiveAnimations[0].textures.size() != 6 ||
                    render.missionObjectiveAnimations[1].textures.size() != 3 ||
                    !render.missionRewardCash[0].descriptor)
                    throw std::runtime_error("Mission reward smoke frame did not prepare the retail presentation.");
                render.missionRewardMs = 0;
                presentCity(render, info.imageExtent);
                render.missionRewardMs = 1000;
                presentCity(render, info.imageExtent);
                render.missionRewardMs = render.missionObjectiveHoldEndMs() + 100;
                presentCity(render, info.imageExtent);
                render.missionRewardMs.reset();
                std::cout << "Submitted the opening mission completion message and reward through Vulkan.\n" << std::flush;
                if (!route.postFight.nextCheckpoint || !render.nextMissionFirstSurface ||
                    render.checkpointMeshes[3].empty())
                    throw std::runtime_error("Post-fight smoke frame could not resolve the next mission checkpoint.");
                render.nextMissionMarker = render.payphoneGameplayMs;
                presentCity(render, info.imageExtent);
                render.nextMissionMarker.reset();
                if (!render.missionTitles[1].label.descriptor)
                    throw std::runtime_error("Next mission smoke frame did not load the retail title.");
                render.missionTitleIndex = 1;
                render.missionTitleMs = 750;
                presentCity(render, info.imageExtent);
                render.missionTitleIndex = 0;
                render.missionTitleMs.reset();
                const auto& nextScene = route.postFight.nextCheckpoint->cinematic;
                render.activeMissionCinematic = &nextScene;
                auto& nextPlayback = render.postFightCinematic.emplace();
                nextPlayback.advance(nextScene, nextScene.dialogue.front().startMs, false, {});
                if (nextPlayback.activeDialogue() != 0 ||
                    !render.captions[render.missionCaptionBases.at(&nextScene)].descriptor)
                    throw std::runtime_error("Next mission opening dialogue was not prepared.");
                presentCity(render, info.imageExtent);
                render.postFightCinematic.reset();
                render.activeMissionCinematic = nullptr;
                std::cout << "Submitted the LC mission opening cinematic through Vulkan.\n" << std::flush;
                if (!render.nextVehicleObjective.descriptor)
                    throw std::runtime_error("LC mission vehicle objective was not prepared.");
                activateNextMissionVehicle(render);
                const auto& hummer = render.opening.actors.at(*render.opening.vehicleActor);
                if (hummer.name != "aHummer_00" || render.wheels.size() != 4 ||
                    !render.opening.vehicleCharacterClips || !render.opening.vehicleSeated)
                    throw std::runtime_error("LC mission did not activate the Hummer driving data.");
                playback.vehicle = CityVehiclePose{hummer.position, hummer.rotationDegrees[2]};
                presentCity(render, info.imageExtent);
                if (!render.hummerRouteFirstSurface || render.checkpointMeshes[4].empty())
                    throw std::runtime_error("LC mission pier smoke frame is missing its animated marker.");
                render.hummerRouteMarker = render.payphoneGameplayMs;
                playback.nextMissionVehicleAcquired = true;
                presentCity(render, info.imageExtent);
                render.hummerRouteMarker.reset();
                std::cout << "Submitted the retail Hummer pier marker through Vulkan.\n" << std::flush;
                const auto& pier = route.postFight.nextCheckpoint->pierCinematic;
                render.activeMissionCinematic = &pier;
                auto& pierPlayback = render.postFightCinematic.emplace();
                pierPlayback.advance(pier, pier.dialogue.front().startMs, false, {});
                if (pierPlayback.activeDialogue() != 0 ||
                    !render.captions[render.missionCaptionBases.at(&pier)].descriptor)
                    throw std::runtime_error("Pier cinematic smoke frame did not load its opening dialogue.");
                presentCity(render, info.imageExtent);
                std::cout << "Submitted the retail pier cinematic through Vulkan.\n" << std::flush;
                pierPlayback.advance(pier, 0, true, {});
                const auto& run = pier.motions.front().travel;
                pierPlayback.advance(pier, run.startMs - pierPlayback.elapsedMs() + run.durationMs / 2., false, {});
                presentCity(render, info.imageExtent);
                std::cout << "Submitted Matadore's running pose through Vulkan.\n" << std::flush;
                render.postFightCinematic.reset();
                render.activeMissionCinematic = nullptr;
                if (!render.weaponShopObjective.descriptor || !render.weaponShopDriveFirstSurface ||
                    render.checkpointMeshes[5].empty())
                    throw std::runtime_error("Weapon shop drive smoke frame is missing its objective or marker.");
                render.weaponShopDrive = true;
                render.hummerRouteMarker = render.payphoneGameplayMs;
                presentCity(render, info.imageExtent);
                std::cout << "Submitted the retail weapon shop drive marker through Vulkan.\n" << std::flush;
                render.hummerRouteMarker.reset();
                const auto& shop = route.postFight.nextCheckpoint->weaponShopCinematic;
                render.activeMissionCinematic = &shop;
                auto& shopPlayback = render.postFightCinematic.emplace();
                shopPlayback.advance(shop, shop.dialogue.front().startMs, false, {});
                if (shopPlayback.activeDialogue() != 0 ||
                    !render.captions[render.missionCaptionBases.at(&shop)].descriptor)
                    throw std::runtime_error("Weapon shop cinematic smoke frame did not prepare its dialogue.");
                presentCity(render, info.imageExtent);
                std::cout << "Submitted the retail weapon shop cinematic through Vulkan.\n" << std::flush;
                render.postFightCinematic.reset();
                render.activeMissionCinematic = nullptr;
                if (!render.weaponShopApproachFirstSurface || render.checkpointMeshes[6].empty())
                    throw std::runtime_error("Weapon shop smoke frame did not prepare the approach arrow.");
                render.weaponShopApproach = true;
                render.nextMissionMarker = render.payphoneGameplayMs;
                presentCity(render, info.imageExtent);
                std::cout << "Submitted the retail weapon shop approach arrow through Vulkan.\n" << std::flush;
                if (!render.weaponShopEntranceFirstSurface || !render.weaponShopTutorial.descriptor)
                    throw std::runtime_error("Weapon shop entrance smoke frame did not prepare the marker and tutorial.");
                render.weaponShopUnlocked = true;
                render.weaponShopTutorialVisible = true;
                presentCity(render, info.imageExtent);
                std::cout << "Submitted the retail weapon shop entrance and tutorial through Vulkan.\n" << std::flush;
                if (!render.weaponShopInventory[0].descriptor || !render.weaponShopConfirmation[0].descriptor ||
                    !render.weaponShopInventory[1].descriptor || !render.weaponShopConfirmation[1].descriptor ||
                    !render.weaponShopInsufficientCash.descriptor)
                    throw std::runtime_error("Weapon shop smoke frame did not prepare all shop panels.");
                render.weaponShopTutorialVisible = false;
                render.weaponShopOpen = true;
                if (!render.cashGlyphs[0].descriptor)
                    throw std::runtime_error("Cash HUD smoke frame did not prepare retail glyphs.");
                render.playback.playerCash = 12345;
                presentCity(render, info.imageExtent);
                render.weaponShopConfirmPurchase = true;
                presentCity(render, info.imageExtent);
                render.weaponShopSelection = 1;
                presentCity(render, info.imageExtent);
                render.weaponShopConfirmPurchase = false;
                presentCity(render, info.imageExtent);
                render.weaponShopSelection = 0;
                render.weaponShopPurchaseDenied = true;
                render.playback.playerCash = 50;
                presentCity(render, info.imageExtent);
                if (render.displayedCash != 50)
                    throw std::runtime_error("Cash HUD smoke frame did not pick up a changed balance.");
                render.weaponShopPurchaseDenied = false;
                render.weaponShopOpen = false;
                std::cout << "Submitted the weapon shop inventory through Vulkan.\n" << std::flush;
                if (!render.statusGauges[armorGauge].fillWidth || !render.statusGauges[lifeGauge].fillWidth)
                    throw std::runtime_error("Status gauge smoke frame did not prepare the retail fills.");
                if (render.playback.vehicle) render.playback.vehicle->occupied = false;
                render.playback.playerArmor = 100;
                render.playback.playerHealth = 120;
                presentCity(render, info.imageExtent);
                render.healthBlink.update(render.playback.playerHealth, 500);
                presentCity(render, info.imageExtent);
                render.playback.playerArmor = 0;
                render.playback.playerHealth = CityPlayback::playerMaxHealth;
                std::cout << "Submitted the retail armor and blinking health gauges through Vulkan.\n" << std::flush;
                if (!render.returnToLcFirstSurface || render.checkpointMeshes[8].empty() ||
                    !render.returnToLcObjectives[0].descriptor || !render.returnToLcObjectives[1].descriptor)
                    throw std::runtime_error("LC return smoke frame did not prepare the objectives and destination.");
                activateReturnToLc(render);
                presentCity(render, info.imageExtent);
                render.hummerRouteMarker = render.payphoneGameplayMs;
                presentCity(render, info.imageExtent);
                render.hummerRouteScript.reset();
                render.hummerRouteMarker.reset();
                const auto& closing = route.postFight.nextCheckpoint->lcClosingCinematic;
                render.returnToLcFadeMs = route.postFight.nextCheckpoint->returnToLcFadeMs / 2.;
                presentCity(render, info.imageExtent);
                render.returnToLcFadeMs.reset();
                render.activeMissionCinematic = &closing;
                auto& lcPlayback = render.postFightCinematic.emplace();
                lcPlayback.advance(closing, closing.dialogue.front().startMs, false, {});
                if (!lcPlayback.activeDialogue() || !render.missionCaptionBases.count(&closing))
                    throw std::runtime_error("LC closing smoke frame did not prepare the first dialogue.");
                presentCity(render, info.imageExtent);
                const auto beforeCash = render.playback.playerCash;
                const auto beforeMissions = render.achievements ? render.achievements->stat(49) : 0;
                bool closingFinished = false;
                for (int step = 0; step < 20 && !closingFinished; ++step)
                    closingFinished = lcPlayback.advance(closing, 1000, true, {});
                if (!closingFinished) throw std::runtime_error("LC closing smoke test did not finish its dialogue.");
                update(0);
                if (render.postFightCinematic || !render.playback.openingMissionsPaid[1] ||
                    render.playback.playerCash != beforeCash + 100 || render.missionRewardIndex != 1 ||
                    !render.missionRewardCash[1].descriptor || !render.missionCompleteObjectives[1].descriptor ||
                    !render.achievements || render.achievements->stat(49) != beforeMissions + 1 ||
                    render.playback.vehicle || !render.nextMissionMarker)
                    throw std::runtime_error("LC closing did not apply the reward, statistics, and Hummer cleanup.");
                update(1000);
                if (render.playback.vehicle)
                    throw std::runtime_error("The Hummer respawned after LC mission completion.");
                std::cout << "Submitted the retail return-to-LC checkpoint, fade, and closing scene through Vulkan.\n"
                          << std::flush;
                render.payphoneGameplayMs.reset();
                std::cout << "Submitted the retail payphone gameplay handoff through Vulkan.\n" << std::flush;
                const auto& placements = route.postFight.placements;
                const auto player = std::find_if(placements.rbegin(), placements.rend(), [](const auto& cue) {
                    return cue.actor == "Player" && cue.visible;
                });
                if (player == placements.rend())
                    throw std::runtime_error("Opening checkpoint smoke test did not find a retail player placement.");
                RetailSavedState checkpoint;
                checkpoint.cash = 250;
                checkpoint.armor[0] = 75;
                checkpoint.position = player->position;
                checkpoint.completedScripts = {{windowsOpeningCheckpointScript, -1}};
                render.weaponShopUnlocked = false;
                render.weaponShopApproach = false;
                render.weaponShopDrive = false;
                render.nextMissionMarker.reset();
                restoreOpeningCheckpoint(render, checkpoint);
                update(16);
                if (render.playback.playerCash != 250 || render.playback.playerArmor != 75 ||
                    !render.playback.openingMissionsPaid[0] || !render.nextMissionMarker)
                    throw std::runtime_error("Opening checkpoint did not restore the next mission marker and player.");
                std::cout << "Restored the opening checkpoint and presented the next objective through Vulkan.\n"
                          << std::flush;
            }
        }
        return;
    }

    constexpr UINT_PTR startupTimer = 1;
    auto lastTick = std::chrono::steady_clock::now();
    if (logo && !SetTimer(window, startupTimer, 16, nullptr))
        throw std::runtime_error("Could not start the Win32 startup timer.");
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.hwnd == window && message.message == WM_TIMER && message.wParam == startupTimer) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration<double, std::milli>(now - lastTick);
            lastTick = now;
            if (update(elapsed.count())) KillTimer(window, startupTimer);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result < 0) throw std::runtime_error("Could not read the Win32 message queue.");
}
}

void presentImage(HWND window, const Image& image, bool smokeTest)
{
    presentImages(window, nullptr, nullptr, image, smokeTest);
}

void presentStartup(HWND window, const StartupLogo& logo, IntroMovie& movie, const SplashFrames& splash,
                    Win32TouchInput& touch, ResourceFileSystem& assets, bool smokeTest, RetailLanguage language,
                    bool openingSmokeTest)
{
    presentImages(window, &logo, &movie, splash.show, smokeTest, &splash, &touch, &assets, language, openingSmokeTest);
}
