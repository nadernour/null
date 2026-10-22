#pragma once

#include <string>
#include <volk/volk.h>

namespace ne {

class Window;
class Instance;
class Device;
class FrameManager;

class ImGuiManager {
public:
  ImGuiManager(Window* iWindow, Instance* iInstance, Device* iDevice, const FrameManager* iFrameManager);
  ~ImGuiManager();

  ImGuiManager(const ImGuiManager&) = delete;
  ImGuiManager& operator=(const ImGuiManager&) = delete;
  ImGuiManager(ImGuiManager&&) = delete;
  ImGuiManager& operator=(ImGuiManager&&) = delete;

  void beginFrame();
  void endFrame();
  void draw(VkCommandBuffer iCommandBuffer);

  bool wantsCaptureMouse() const;
  bool wantsCaptureKeyboard() const;

private:
  void setupIO();
  void setupStyle();

  Window* mWindow{nullptr};
  std::string mIniPath;
};

} // namespace ne
