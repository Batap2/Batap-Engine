#pragma once

// Always include this header, never <vk_mem_alloc.h> directly. By default VMA
// resolves the Vulkan symbols at link time, which fails here: volk loads them
// at runtime and we never link the loader. The defines below switch VMA to the
// same scheme (it bootstraps from the two entry points passed in
// VulkanContext::init). volk must come first, so that vulkan.h is pulled in
// with VK_NO_PROTOTYPES.
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#endif
#include <vk_mem_alloc.h>
#if defined(__clang__)
  #pragma clang diagnostic pop
#endif
