#include "renderer/imgui_manager.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/logger.h"
#include "platform/window.h"
#include "renderer/device.h"
#include "renderer/frame_renderer.h"
#include "renderer/instance.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace ne {

ImGuiManager::ImGuiManager(Window* iWindow, Instance* iInstance, Device* iDevice, const FrameRenderer* iFrameRenderer)
    : mWindow(iWindow) {
  NE_ASSERT(mWindow && iInstance && iDevice && iFrameRenderer);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  setupIO();
  setupStyle();

  ImGui_ImplGlfw_InitForVulkan(mWindow->getGLFWwindow(), true);

  VkFormat colorFormat = iFrameRenderer->getColorFormat();
  VkFormat depthFormat = iFrameRenderer->getDepthFormat();

  VkPipelineRenderingCreateInfoKHR renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  renderingInfo.pNext = nullptr;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &colorFormat;
  renderingInfo.depthAttachmentFormat = depthFormat;
  renderingInfo.stencilAttachmentFormat = depthFormat;

  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.ApiVersion = iInstance->getApiVersion();
  initInfo.Instance = iInstance->getInstance();
  initInfo.PhysicalDevice = iDevice->getPhysicalDevice();
  initInfo.Device = iDevice->getDevice();
  initInfo.QueueFamily = iDevice->getQueueFamilyIndex();
  initInfo.Queue = iDevice->getQueue();
  initInfo.PipelineCache = VK_NULL_HANDLE;
  initInfo.DescriptorPool = VK_NULL_HANDLE;
  initInfo.DescriptorPoolSize = 128;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = iFrameRenderer->getImageCount();
  initInfo.UseDynamicRendering = true;
  initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
  initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  initInfo.CheckVkResultFn = [](VkResult iResult) {
    if (iResult != VK_SUCCESS) {
      NE_ERROR("ImGui Vulkan Error: {}", static_cast<int32_t>(iResult));
    }
  };

  ImGui_ImplVulkan_Init(&initInfo);
  NE_LOG("ImGuiManager initialized successfully with Dynamic Rendering.");
}

ImGuiManager::~ImGuiManager() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  NE_LOG("ImGuiManager destroyed successfully.");
}

void ImGuiManager::beginFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiManager::endFrame() { ImGui::Render(); }

void ImGuiManager::draw(VkCommandBuffer iCommandBuffer) {
  ImDrawData* drawData = ImGui::GetDrawData();
  if (drawData && drawData->DisplaySize.x > 0.0f && drawData->DisplaySize.y > 0.0f) {
    ImGui_ImplVulkan_RenderDrawData(drawData, iCommandBuffer);
  }
}

bool ImGuiManager::wantsCaptureMouse() const { return ImGui::GetIO().WantCaptureMouse; }

bool ImGuiManager::wantsCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

void ImGuiManager::setupIO() {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.ConfigNavCaptureKeyboard = false;

  mIniPath = fs::resolveSavedPath("imgui.ini");
  fs::ensureParentDirectoryExists(mIniPath);
  io.IniFilename = mIniPath.c_str();
}

void ImGuiManager::setupStyle() {
  ImGui::StyleColorsDark();

  ImGuiStyle& style = ImGui::GetStyle();

  float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(mWindow->getPrimaryMonitor());
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;
}

} // namespace ne
