#pragma once

#include <volk/volk.h>

#include <memory>
#include <string>
#include <vector>

namespace ne {

class Instance {
public:
  struct Config {
    std::string engineName = "Null Engine";
    std::string appName = "Null App";
    std::vector<const char*> requiredExtensions;
  };

  Instance(const Config& iConfig);
  ~Instance();

  Instance(const Instance&) = delete;
  Instance& operator=(const Instance&) = delete;
  Instance(Instance&&) = delete;
  Instance& operator=(Instance&&) = delete;

  VkInstance getInstance() const { return mInstance; }
  uint32_t getApiVersion() const { return kApiVersion; }

private:
  void createInstance(const Config& iConfig);
  void setupDebugMessenger();

  static constexpr uint32_t kApiVersion = VK_API_VERSION_1_4;
  static constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

  // Requested in Debug builds; stays off when the validation layer is not installed
  bool mValidationEnabled = false;

  VkInstance mInstance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;

  // Set by debugCallback on the thread issuing the Vulkan call. Plain bool while all Vulkan calls are made from one
  // thread; make it std::atomic<bool> once Vulkan calls are issued from multiple threads.
  bool mHasValidationErrors = false;
};

} // namespace ne
