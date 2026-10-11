#include <volk/volk.h> // Before VMA, which only defines vmaImportVulkanFunctionsFromVolk() when volk is included

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
