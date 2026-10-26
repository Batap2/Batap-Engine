#include "Renderer/Device/VulkanContext.h"

#define VMA_IMPLEMENTATION
#include "Renderer/Device/Memory.h"

#include "VkBootstrap.h"

#include "Platform/PlatformWindow.h"

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

VulkanContext::VulkanContext()
{
    if (volkInitialize() != VK_SUCCESS)
        throw std::runtime_error("Unable to find Vulkan loader (volkInitialize)");

    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Batap")
        .require_api_version(1, 3, 0)
        .request_validation_layers(UseValidation)
        .use_default_debug_messenger();
    static constexpr VkBool32 kEnabled = VK_TRUE;
    const bool syncValidation = UseValidation &&
                                platformEnvVar("BATAP_SYNC_VALIDATION").value_or("0") != "0";
    if (syncValidation)
    {
        instanceBuilder.add_layer_setting({"VK_LAYER_KHRONOS_validation", "validate_sync",
                                           VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &kEnabled});
        instanceBuilder.add_layer_setting({"VK_LAYER_KHRONOS_validation",
                                           "syncval_shader_accesses_heuristic",
                                           VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &kEnabled});
    }
    auto instanceResult = instanceBuilder.build();
    if (!instanceResult)
        throw std::runtime_error("VkInstance : " + instanceResult.error().message());

    vkb::Instance vkbInstance = instanceResult.value();
    instance_ = vkbInstance.instance;
    debugMessenger_ = vkbInstance.debug_messenger;
    volkLoadInstance(instance_);
    if (syncValidation)
        std::cout << "[Vulkan] validation de synchronisation : oui\n";

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;

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

    VkPhysicalDeviceFeatures features10{};
    features10.samplerAnisotropy = VK_TRUE;
    // Shadow pass: a caster before the near plane flattens onto it, not clipped.
    features10.depthClamp = VK_TRUE;

    auto selection = vkb::PhysicalDeviceSelector(vkbInstance)
                         .set_minimum_version(1, 3)
                         .set_required_features_13(features13)
                         .set_required_features_12(features12)
                         .set_required_features(features10)
                         .require_present(false)
                         .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                         .select();
    if (!selection)
        throw std::runtime_error("No Vulkan 1.3 compatible GPU" + selection.error().message());

    vkb::PhysicalDevice vkbPhysicalDevice = selection.value();
    physicalDevice_ = vkbPhysicalDevice.physical_device;
    std::cout << "[Vulkan] GPU : " << vkbPhysicalDevice.name << "\n";

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    accelerationStructureFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.rayQuery = VK_TRUE;

    VkPhysicalDeviceFeatures rtFeatures{};
    rtFeatures.shaderInt64 = VK_TRUE;
    rtFeatures.fragmentStoresAndAtomics = VK_TRUE;

    VkPhysicalDeviceVulkan12Features rtFeatures12{};
    rtFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    rtFeatures12.shaderBufferInt64Atomics = VK_TRUE;

    rtSupported_ =
        vkbPhysicalDevice.enable_features_if_present(rtFeatures) &&
        vkbPhysicalDevice.enable_extension_features_if_present(rtFeatures12) &&
        vkbPhysicalDevice.enable_extensions_if_present(
            {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
             VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME}) &&
        vkbPhysicalDevice.enable_extension_features_if_present(accelerationStructureFeatures) &&
        vkbPhysicalDevice.enable_extension_features_if_present(rayQueryFeatures);
    std::cout << "[Vulkan] RT : " << (rtSupported_ ? "oui" : "non") << "\n";

    if (rtSupported_)
    {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{};
        asProperties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &asProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice_, &properties);
        asScratchAlignment_ = asProperties.minAccelerationStructureScratchOffsetAlignment;
    }
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        timestampPeriodNs_ = properties.limits.timestampPeriod;
    }

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

VulkanContext::~VulkanContext()
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
