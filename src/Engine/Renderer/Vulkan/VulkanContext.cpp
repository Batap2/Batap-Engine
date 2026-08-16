#include "VulkanContext.h"

#define VMA_IMPLEMENTATION
#include "VulkanMemory.h"

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#endif
#include "VkBootstrap.h"
#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

#include <iostream>
#include <stdexcept>

namespace batap
{

namespace
{
#if defined(_DEBUG)
constexpr bool UseValidation = true;
#else
constexpr bool UseValidation = false;
#endif
}  // namespace

void VulkanContext::init()
{
    if (volkInitialize() != VK_SUCCESS)
        throw std::runtime_error("Unable to find Vulkan loader (volkInitialize)");

    auto instanceResult = vkb::InstanceBuilder()
                              .set_app_name("Batap")
                              .require_api_version(1, 3, 0)
                              .request_validation_layers(UseValidation)
                              .use_default_debug_messenger()
                              .build();
    if (!instanceResult)
        throw std::runtime_error("VkInstance : " + instanceResult.error().message());

    vkb::Instance vkbInstance = instanceResult.value();
    instance_ = vkbInstance.instance;
    debugMessenger_ = vkbInstance.debug_messenger;
    volkLoadInstance(instance_);

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

    auto selection = vkb::PhysicalDeviceSelector(vkbInstance)
                         .set_minimum_version(1, 3)
                         .set_required_features_13(features13)
                         .set_required_features_12(features12)
                         .require_present(false)
                         .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                         .select();
    if (!selection)
        throw std::runtime_error("No Vulkan 1.3 compatible GPU" +
                                 selection.error().message());

    vkb::PhysicalDevice vkbPhysicalDevice = selection.value();
    physicalDevice_ = vkbPhysicalDevice.physical_device;
    std::cout << "[Vulkan] GPU : " << vkbPhysicalDevice.name << "\n";

    auto deviceResult = vkb::DeviceBuilder(vkbPhysicalDevice).build();
    if (!deviceResult)
        throw std::runtime_error("VkDevice : " + deviceResult.error().message());

    vkb::Device vkbDevice = deviceResult.value();
    device_ = vkbDevice.device;
    volkLoadDevice(device_);

    graphicsQueue_ = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily_ = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaVulkanFunctions vmaFunctions{};
    vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.pVulkanFunctions = &vmaFunctions;
    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS)
        throw std::runtime_error("vmaCreateAllocator");
}

void VulkanContext::shutdown()
{
    if (allocator_)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
    if (device_)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (debugMessenger_)
    {
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (instance_)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

}  // namespace batap
