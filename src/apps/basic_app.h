#pragma once

#include "apps/application.h"
#include "apps/main_ui.h"
#include "core/ecs.h"
#include "core/event.h"
#include "scene/camera_controller.h"
#include <memory>
#include <vector>

namespace ne {

class Mesh;
class Material;

class BasicApp : public Application {
public:
  BasicApp();
  ~BasicApp();

  BasicApp(const BasicApp&) = delete;
  BasicApp& operator=(const BasicApp&) = delete;

  virtual void update(float iDeltaTime) override;
  virtual void renderUI() override;

  Entity getCameraEntity() const { return mCameraEntity; }
  Entity getSunEntity() const { return mSunEntity; }
  Entity getEnvironmentEntity() const { return mEnvironmentEntity; }
  CameraController& getCameraController() { return mCameraController; }
  MainUI& getMainUI() { return mMainUI; }

private:
  Entity mCameraEntity;
  Entity mSunEntity;
  Entity mEnvironmentEntity;
  Entity mGroundEntity;
  Entity mCubeEntity1;
  Entity mCubeEntity2;
  Entity mHelmetEntity;

  CameraController mCameraController;

  std::vector<std::shared_ptr<Mesh>> mLoadedMeshes;
  std::shared_ptr<Material> mCube1Material;
  std::shared_ptr<Material> mCube2Material;
  std::shared_ptr<Material> mGroundMaterial;
  float mCurrentRotationAngle{0.0f};

  MainUI mMainUI;
};
} // namespace ne
