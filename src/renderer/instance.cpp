#include "renderer/instance.h"
#include "core/assert.h"
#include "core/defines.h"
#include "core/logger.h"
#include "renderer/utils.h"

// std
#include <algorithm>
#include <cstring>

// pUserData points at Instance::mHasValidationErrors
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT iMessageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT iMessageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
  NE_UNUSED(iMessageType);
  NE_UNUSED(pCallbackData);

  if (iMessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    NE_WARN("validation layer: {}", pCallbackData->pMessage);
  } else if (iMessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    *static_cast<bool*>(pUserData) = true;
    NE_ERROR("validation layer: {}", pCallbackData->pMessage);
  }

  return VK_FALSE;
}

namespace ne {

Instance::Instance(const Config& iConfig) {
  createInstance(iConfig);
  setupDebugMessenger();
}

Instance::~Instance() {
  NE_LOG("Destroying Vulkan Instance and deallocating resources...");
  if (mDebugMessenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
  }
  // Checked after the messenger is gone so errors raised while tearing down the device are included
  NE_ASSERT(!mHasValidationErrors, "Vulkan validation layer reported errors (see log above)");
  vkDestroyInstance(mInstance, nullptr);
  NE_LOG("Vulkan Instance destroyed successfully.");
}

void Instance::createInstance(const Config& iConfig) {
  VK_CHECK(volkInitialize());

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = iConfig.appName.c_str();
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = iConfig.engineName.c_str();
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = kApiVersion;

  uint32_t availableExtensionCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr);
  std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);
  vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, availableExtensions.data());

#if defined(NE_BUILD_DEBUG)
  uint32_t availableLayerCount = 0;
  vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
  std::vector<VkLayerProperties> availableLayers(availableLayerCount);
  vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data());
  mValidationEnabled = std::any_of(availableLayers.begin(), availableLayers.end(), [](const VkLayerProperties& iLayer) {
    return strcmp(iLayer.layerName, kValidationLayerName) == 0;
  });
  if (!mValidationEnabled) {
    NE_WARN("Validation layer {} is not installed; running without validation. It ships with the Vulkan SDK and with "
            "distro packages such as vulkan-validationlayers.",
            kValidationLayerName);
  }
#endif

  std::vector<const char*> windowExtensions = iConfig.requiredExtensions;
  if (mValidationEnabled) {
    windowExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  for (const char* windowExtension : windowExtensions) {
    bool extensionFound = false;
    for (const VkExtensionProperties& availableExtension : availableExtensions) {
      if (strcmp(windowExtension, availableExtension.extensionName) == 0) {
        extensionFound = true;
        break;
      }
    }
    NE_ASSERT(extensionFound, "Required window extension not supported: {}", windowExtension);
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(windowExtensions.size());
  createInfo.ppEnabledExtensionNames = windowExtensions.data();

  if (mValidationEnabled) {
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = &kValidationLayerName;

    // Synchronization validation reports hazards from missing barriers or semaphores
    const VkBool32 validateSync = VK_TRUE;
    VkLayerSettingEXT validateSyncSetting{};
    validateSyncSetting.pLayerName = kValidationLayerName;
    validateSyncSetting.pSettingName = "validate_sync";
    validateSyncSetting.type = VK_LAYER_SETTING_TYPE_BOOL32_EXT;
    validateSyncSetting.valueCount = 1;
    validateSyncSetting.pValues = &validateSync;

    VkLayerSettingsCreateInfoEXT layerSettingsInfo{};
    layerSettingsInfo.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
    layerSettingsInfo.settingCount = 1;
    layerSettingsInfo.pSettings = &validateSyncSetting;
    createInfo.pNext = &layerSettingsInfo;
  }

  VK_CHECK(vkCreateInstance(&createInfo, nullptr, &mInstance));

  volkLoadInstance(mInstance);
}

void Instance::setupDebugMessenger() {
  if (!mValidationEnabled)
    return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.pNext = nullptr;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  createInfo.pUserData = &mHasValidationErrors;

  VK_CHECK(vkCreateDebugUtilsMessengerEXT(mInstance, &createInfo, nullptr, &mDebugMessenger));
}

} // namespace ne
