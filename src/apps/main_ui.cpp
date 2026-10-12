#include "apps/main_ui.h"
#include "apps/basic_app.h"
#include "components/camera_component.h"
#include "components/transform_component.h"
#include "core/ecs.h"
#include "core/logger.h"
#include "core/time.h"
#include "platform/window.h"
#include "platform/input.h"

#include <imgui.h>

namespace ne {

void MainUI::draw(BasicApp& iApp) {
  if (Input::isKeyPressed(KeyCode::H) && !ImGui::GetIO().WantTextInput) {
    toggleVisible();
  }

  // ImGui unfocuses windows on a left click in empty space; do the same for right clicks
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
    ImGui::SetWindowFocus(nullptr);
  }

  if (mVisible) {
    drawMainMenuBar(iApp);

    if (mShowDiagnostics) {
      drawDiagnostics(iApp);
    }

    if (mShowCameraSettings) {
      drawCameraSettings(iApp);
    }

    if (mShowAboutModal) {
      drawAboutModal();
    }

    if (mShowDemoWindow) {
      ImGui::ShowDemoWindow(&mShowDemoWindow);
    }
  }
}

void MainUI::drawMainMenuBar(BasicApp& iApp) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Exit", "Alt+F4")) {
        if (auto* win = iApp.getWindow()) {
          win->setShouldClose(true);
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Diagnostics / Stats", nullptr, &mShowDiagnostics);
      ImGui::MenuItem("Camera Controls", nullptr, &mShowCameraSettings);
      ImGui::Separator();
      if (ImGui::MenuItem("Hide UI Overlay", "H")) {
        mVisible = false;
        NE_VERBOSE("UI Overlay visibility: Hidden");
      }
      ImGui::MenuItem("ImGui Demo Window", nullptr, &mShowDemoWindow);
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Camera")) {
      if (ImGui::MenuItem("Reset Position & Orientation")) {
        resetCamera(iApp);
      }
      ImGui::Separator();
      float currentSpeed = iApp.getCameraController().getMoveSpeed();
      if (ImGui::MenuItem("Speed: Slow (1.0 m/s)", nullptr, currentSpeed == 1.0f)) {
        iApp.getCameraController().setMoveSpeed(1.0f);
      }
      if (ImGui::MenuItem("Speed: Normal (2.0 m/s)", nullptr, currentSpeed == 2.0f)) {
        iApp.getCameraController().setMoveSpeed(2.0f);
      }
      if (ImGui::MenuItem("Speed: Fast (5.0 m/s)", nullptr, currentSpeed == 5.0f)) {
        iApp.getCameraController().setMoveSpeed(5.0f);
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About Null Engine...")) {
        mShowAboutModal = true;
      }
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

void MainUI::drawDiagnostics(BasicApp& iApp) {
  ImGui::SetNextWindowSize(ImVec2(340, 240), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Diagnostics", &mShowDiagnostics)) {
    float currentFrameTime = Time::getUnscaledDeltaTime();
    float currentFPS = currentFrameTime > 0.0f ? 1.0f / currentFrameTime : 0.0f;
    ImGui::Text("Performance");
    ImGui::Separator();
    ImGui::Text("Frame Rate: %.1f FPS", currentFPS);
    ImGui::Text("Frame Time: %.2f ms", currentFrameTime * 1000);
    ImGui::Spacing();
    ImGui::Text("Scene Statistics");
    ImGui::Separator();
    if (auto* reg = iApp.getRegistry()) {
      ImGui::Text("Active Entities: %zu", reg->size());
    }
    ImGui::Spacing();
    ImGui::Text("Simulation Time Controls");
    ImGui::Separator();
    float timeScale = Time::getTimeScale();
    if (ImGui::SliderFloat("Time Scale", &timeScale, 0.0f, 3.0f, "%.2fx")) {
      Time::setTimeScale(timeScale);
    }
    if (ImGui::Button("Pause (0x)")) {
      Time::setTimeScale(0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("0.5x")) {
      Time::setTimeScale(0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("1.0x")) {
      Time::setTimeScale(1.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("2.0x")) {
      Time::setTimeScale(2.0f);
    }
  }
  ImGui::End();
}

void MainUI::drawCameraSettings(BasicApp& iApp) {
  ImGui::SetNextWindowSize(ImVec2(360, 340), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Camera Settings", &mShowCameraSettings)) {
    auto* reg = iApp.getRegistry();
    Entity camEntity = iApp.getCameraEntity();

    if (reg && reg->isValid(camEntity) && reg->hasComponent<TransformComponent>(camEntity) &&
        reg->hasComponent<CameraComponent>(camEntity)) {
      auto& transform = reg->getComponent<TransformComponent>(camEntity);
      auto& camera = reg->getComponent<CameraComponent>(camEntity);

      ImGui::Text("Transform (+X Fwd, +Y Left, +Z Up)");
      ImGui::Separator();
      Vec3 pos = transform.getLocal().position;
      if (ImGui::DragFloat3("Position (m)", &pos.x, 0.05f)) {
        transform.setLocalPosition(pos);
      }

      Vec3 eulerDegrees = math::degrees(transform.getLocal().getEulerAngles());
      if (ImGui::DragFloat3("Roll/Pitch/Yaw (deg)", &eulerDegrees.x, 0.5f)) {
        transform.setLocalRotation(Quat::fromEuler(math::radians(eulerDegrees)));
      }

      ImGui::Spacing();
      ImGui::Text("Projection (Reverse-Z Depth)");
      ImGui::Separator();
      ImGui::SliderAngle("FOV", &camera.fov, 10.0f, 120.0f, "%.1f deg");
      ImGui::DragFloat("Near Clip (m)", &camera.nearClip, 0.01f, 0.01f, 10.0f, "%.2f");
      ImGui::DragFloat("Far Clip (m)", &camera.farClip, 1.0f, 1.0f, 1000.0f, "%.1f");

      ImGui::Spacing();
      ImGui::Text("Controller Parameters");
      ImGui::Separator();
      float speed = iApp.getCameraController().getMoveSpeed();
      if (ImGui::SliderFloat("Fly Speed (m/s)", &speed, 0.1f, 20.0f, "%.1f")) {
        iApp.getCameraController().setMoveSpeed(speed);
      }

      float sensitivity = iApp.getCameraController().getLookSensitivity();
      if (ImGui::SliderAngle("Look Sensitivity", &sensitivity, 0.01f, 1.0f, "%.2f deg/px")) {
        iApp.getCameraController().setLookSensitivity(sensitivity);
      }

      ImGui::Spacing();
      if (ImGui::Button("Reset Camera Transform")) {
        resetCamera(iApp);
      }
    }
  }
  ImGui::End();
}

void MainUI::resetCamera(BasicApp& iApp) {
  if (auto* reg = iApp.getRegistry()) {
    Entity camEntity = iApp.getCameraEntity();
    if (reg->isValid(camEntity) && reg->hasComponent<TransformComponent>(camEntity)) {
      auto& transform = reg->getComponent<TransformComponent>(camEntity);
      transform.setLocal(Transform(Vec3(-4.0f, 0.0f, 0.0f)));
    }
  }
}

void MainUI::drawAboutModal() {
  if (mShowAboutModal) {
    ImGui::OpenPopup("About Null Engine");
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(460, 330), ImGuiCond_Appearing);

  if (ImGui::BeginPopupModal("About Null Engine", &mShowAboutModal, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Null Engine - 3D Model Viewer");
    ImGui::TextDisabled("Version 1.0.0 (C++20 & Vulkan 1.4)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Core Technical Architecture:");
    ImGui::BulletText("Vulkan 1.4 Dynamic Rendering & Synchronization2");
    ImGui::BulletText("Programmable Vertex Pulling via BDA & MDI");
    ImGui::BulletText("Slang Shading Language -> SPIR-V 1.4");
    ImGui::BulletText("ROS REP-103 Coordinate System (+X Fwd, +Y Left, +Z Up)");
    ImGui::BulletText("Floating-Point Reverse-Z Depth (0.0 Far Clear)");
    ImGui::Spacing();

    ImGui::Text("Navigation & Controls:");
    ImGui::BulletText("Fly Camera: WASD + QE (Up/Down)");
    ImGui::BulletText("Speed Boost: Hold Shift while flying");
    ImGui::BulletText("Speed Adjust: Scroll Mouse Wheel");
    ImGui::BulletText("Mouse Look: Hold Right-Click + Move Mouse");
    ImGui::BulletText("Toggle UI: Press H to hide/show menus and panels");
    ImGui::Spacing();

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
      mShowAboutModal = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

} // namespace ne
