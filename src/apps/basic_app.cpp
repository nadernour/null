#include "apps/basic_app.h"
#include "components/camera_component.h"
#include "components/environment_component.h"
#include "components/light_component.h"
#include "components/mesh_component.h"
#include "components/name_component.h"
#include "components/transform_component.h"
#include "core/image_data.h"
#include "core/logger.h"
#include "core/material_data.h"
#include "core/math/math.h"
#include "core/mesh_utils.h"
#include "core/time.h"
#include "importers/gltf_importer.h"
#include "importers/image_importer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/resource_manager.h"
#include "scene/prefab.h"
#include <array>
#include <format>

namespace ne {

BasicApp::BasicApp() : Application("Basic App (MDI Showcase)", 1200, 1000) {
  MeshData cubeMeshData = MeshUtils::createBoxMeshData(Vec3(1.0f));
  std::shared_ptr<Mesh> cubeMesh = getResourceManager()->createMesh(cubeMeshData);
  mLoadedMeshes.push_back(cubeMesh);

  Prefab helmetPrefab(*getResourceManager(), GltfImporter::importModel("models/DamagedHelmet.glb"));

  std::optional<ImageData> checkerImageData = ImageImporter::importFromFile("textures/uv_checker.png");
  uint32_t checkerTextureId = 0;
  if (checkerImageData) {
    checkerTextureId = getResourceManager()->createTexture(*checkerImageData, "UV_Checker_Texture");
  }

  // Both cubes use the checker as their base color, the second without filtering
  const std::array checkerTextureIds{checkerTextureId};
  mCube1Material = getResourceManager()->createMaterial(
      MaterialData{.name = "Checker_Linear", .baseColorTexture = {.image = 0}}, checkerTextureIds);
  mCube2Material = getResourceManager()->createMaterial(
      MaterialData{.name = "Checker_Nearest", .baseColorTexture = {.image = 0, .filter = TextureFilter::Nearest}},
      checkerTextureIds);

  // An indoor exposure (EV100 7.9), for the sun's indoor 1000 lux
  mCameraEntity = getRegistry()->createEntity();
  getRegistry()->addComponent<TransformComponent>(mCameraEntity, Transform(Vec3(-4.0f, 0.0f, 0.0f)));
  getRegistry()->addComponent<CameraComponent>(mCameraEntity, CameraComponent{.fov = math::radians(45.0f),
                                                                               .nearClip = 0.1f,
                                                                               .farClip = 100.0f,
                                                                               .aperture = 4.0f,
                                                                               .shutterSpeed = 1.0f / 60.0f,
                                                                               .iso = 400.0f});
  getRegistry()->addComponent<NameComponent>(mCameraEntity, "Camera");

  // Pitched down 50 degrees and turned 30 degrees right, the sun shines along (0.56, -0.32, -0.77): it comes from above
  // the camera's left shoulder
  mSunEntity = getRegistry()->createEntity();
  getRegistry()->addComponent<TransformComponent>(mSunEntity,
                                                  Transform(Vec3::Zero, Quat::fromEuler(math::radians(Vec3(0.0f, 50.0f, -30.0f)))));
  getRegistry()->addComponent<LightComponent>(mSunEntity,
                                              LightComponent{.type = LightComponent::Type::Directional, .intensity = 1000.0f});
  getRegistry()->addComponent<NameComponent>(mSunEntity, "Sun");

  mEnvironmentEntity = getRegistry()->createEntity();
  getRegistry()->addComponent<EnvironmentComponent>(mEnvironmentEntity,
                                                    EnvironmentComponent{.skyColor = {0.65f, 0.8f, 1.0f, 1.0f},
                                                                         .skyIntensity = 150.0f,
                                                                         .groundColor = {0.5f, 0.45f, 0.4f, 1.0f},
                                                                         .groundIntensity = 50.0f});
  getRegistry()->addComponent<NameComponent>(mEnvironmentEntity, "Environment");

  // Left (+Y)
  if (!mLoadedMeshes.empty()) {
    mCubeEntity1 = getRegistry()->createEntity();
    getRegistry()->addComponent<TransformComponent>(mCubeEntity1, Transform(Vec3(0.0f, 1.5f, -0.5f)));
    getRegistry()->addComponent<MeshComponent>(mCubeEntity1, mLoadedMeshes[0], std::vector{mCube1Material});
    getRegistry()->addComponent<NameComponent>(mCubeEntity1, "Cube_LinearRepeat");
  }

  // Center, raised (+Z)
  if (!mLoadedMeshes.empty()) {
    mCubeEntity2 = getRegistry()->createEntity();
    getRegistry()->addComponent<TransformComponent>(mCubeEntity2, Transform(Vec3(0.0f, 0.0f, 0.5f)));
    getRegistry()->addComponent<MeshComponent>(mCubeEntity2, mLoadedMeshes[0], std::vector{mCube2Material});
    getRegistry()->addComponent<NameComponent>(mCubeEntity2, "Cube_NearestRepeat");
  }

  // Right (-Y)
  mHelmetEntity = helmetPrefab.instantiate(*getRegistry(), Transform(Vec3(0.0f, -1.5f, -0.5f)));

  // Under everything: its top is at z = -1.5, just below the helmet's lowest point (about -1.4)
  std::shared_ptr<Mesh> groundMesh = getResourceManager()->createMesh(MeshUtils::createBoxMeshData(Vec3(20.0f, 20.0f, 0.1f)));
  mLoadedMeshes.push_back(groundMesh);
  mGroundMaterial = getResourceManager()->createMaterial(
      MaterialData{.name = "Ground", .baseColor = LinearColor{0.5f, 0.5f, 0.5f, 1.0f}, .roughness = 0.8f});
  mGroundEntity = getRegistry()->createEntity();
  getRegistry()->addComponent<TransformComponent>(mGroundEntity, Transform(Vec3(0.0f, 0.0f, -1.55f)));
  getRegistry()->addComponent<MeshComponent>(mGroundEntity, groundMesh, std::vector{mGroundMaterial});
  getRegistry()->addComponent<NameComponent>(mGroundEntity, "Ground");
}

BasicApp::~BasicApp() = default;

void BasicApp::renderUI() { mMainUI.draw(*this); }

void BasicApp::update(float iDeltaTime) {
  if (getRegistry()->isValid(mCameraEntity)) {
    mCameraController.update(Time::getUnscaledDeltaTime(), getRegistry()->getComponent<TransformComponent>(mCameraEntity));
  }

  mCurrentRotationAngle += math::radians(30.0f) * iDeltaTime;
  Quat rotZ = Quat::angleAxis(mCurrentRotationAngle, Vec3(0.0f, 0.0f, 1.0f));

  if (getRegistry()->isValid(mCubeEntity1)) {
    getRegistry()->getComponent<TransformComponent>(mCubeEntity1).setLocalRotation(rotZ);
  }

  if (getRegistry()->isValid(mCubeEntity2)) {
    getRegistry()->getComponent<TransformComponent>(mCubeEntity2).setLocalRotation(rotZ);
  }

  if (getRegistry()->isValid(mHelmetEntity)) {
    getRegistry()->getComponent<TransformComponent>(mHelmetEntity).setLocalRotation(rotZ);
  }
}

} // namespace ne
