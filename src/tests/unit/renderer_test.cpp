#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "apps/application.h"
#include "components/environment_component.h"
#include "components/light_component.h"
#include "components/mesh_component.h"
#include "components/transform_component.h"
#include "core/ecs.h"
#include "core/material_data.h"
#include "core/mesh_utils.h"
#include "platform/window.h"
#include "renderer/bindless_manager.h"
#include "renderer/gpu_types.h"
#include "renderer/renderer.h"
#include "renderer/resource_manager.h"
#include "renderer/utils.h"

#include <GLFW/glfw3.h>

#include <array>
#include <utility>

namespace ne::test {

// In CI this runs the whole frame on lavapipe under synchronization validation; Debug builds abort at exit on any error
NE_TEST_CASE("renderer", "Frames Render Through Scene, Tonemap, And UI") {
  ioContext.app->runForFrames(3);
  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so all three frames ran.");
}

// A tiling window manager may ignore the resize; under CI's Xvfb it happens
NE_TEST_CASE("renderer", "A Resize Keeps The HDR Target's Bindless Slot") {
  const BindlessManager& bindless = *ioContext.app->getResourceManager()->getBindlessManager();
  const uint32_t registeredBefore = bindless.getRegisteredSampledImageCount();
  Window& window = *ioContext.app->getWindow();
  int32_t width = 0, height = 0;
  window.getWindowSize(&width, &height);

  glfwSetWindowSize(window.getGLFWwindow(), 640, 480);
  ioContext.app->runForFrames(3);
  glfwSetWindowSize(window.getGLFWwindow(), width, height);
  ioContext.app->runForFrames(3);

  NE_TEST_ASSERT(bindless.getRegisteredSampledImageCount() == registeredBefore,
                 "Resizing reuses the HDR target's bindless slot instead of taking a new one.");
}

// More instance data than one upload block holds: it gets a block of its own, and the indirect commands after it start
// another. Debug builds abort on an upload overflow and, at exit, on validation errors.
NE_TEST_CASE("renderer", "A Frame's Uploads Can Outgrow One Block") {
  ResourceManager& resources = *ioContext.app->getResourceManager();
  Registry& registry = *ioContext.app->getRegistry();
  std::shared_ptr<Mesh> cube = resources.createMesh(MeshUtils::createBoxMeshData());
  std::shared_ptr<Material> material = resources.getDefaultMaterial();

  // Behind BasicApp's camera (at x = -4, looking along +X), so the cubes are clipped and lavapipe stays fast
  std::vector<Entity> entities(vk_utils::kUploadBlockSize / sizeof(gpu::InstanceData) + 1);
  for (Entity& entity : entities) {
    entity = registry.createEntity();
    registry.addComponent<TransformComponent>(entity, Transform(Vec3(-100.0f, 0.0f, 0.0f)));
    registry.addComponent<MeshComponent>(entity, cube, std::vector{material});
  }

  // Both frames in flight add blocks, then the third frame reuses the first frame's
  ioContext.app->runForFrames(3);
  for (Entity entity : entities) {
    registry.destroyEntity(entity);
  }

  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so all three frames ran.");
}

// One half-transparent cube per alpha mode and sidedness, in front of BasicApp's camera, so every pipeline draws with
// both cull modes: the masked one may discard, the blended one tests against the opaque depth, and double-sided cubes
// don't cull. BasicApp's sun casts shadows, so the opaque and masked cubes also draw into the cascades through both
// shadow pipelines, which skip the blended ones. Debug builds abort at exit on validation errors, such as a draw before
// its dynamic cull mode is set.
NE_TEST_CASE("renderer", "Frames Render Every Alpha Mode, Single- And Double-Sided") {
  ResourceManager& resources = *ioContext.app->getResourceManager();
  Registry& registry = *ioContext.app->getRegistry();
  std::shared_ptr<Mesh> cube = resources.createMesh(MeshUtils::createBoxMeshData(Vec3(0.15f)));

  std::vector<Entity> entities;
  for (AlphaMode alphaMode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend}) {
    for (bool doubleSided : {false, true}) {
      std::shared_ptr<Material> material = resources.createMaterial(MaterialData{.name = "Variant",
                                                                                 .baseColor = LinearColor{1.0f, 1.0f, 1.0f, 0.5f},
                                                                                 .alphaMode = alphaMode,
                                                                                 .doubleSided = doubleSided});

      Entity entity = registry.createEntity();
      const float y = -0.5f + 0.2f * static_cast<float>(entities.size());
      registry.addComponent<TransformComponent>(entity, Transform(Vec3(-2.0f, y, 0.0f)));
      registry.addComponent<MeshComponent>(entity, cube, std::vector{material});
      entities.push_back(entity);
    }
  }

  ioContext.app->runForFrames(3);
  for (Entity entity : entities) {
    registry.destroyEntity(entity);
  }

  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so all three frames ran.");
}

// A directional light, a point light with and one without a range, and a spot light, between the camera and the cubes
// and aimed at them. In CI this runs the scene shader's light loop on lavapipe under validation.
NE_TEST_CASE("renderer", "Frames Render Every Light Type") {
  Registry& registry = *ioContext.app->getRegistry();
  const std::array lights{
      LightComponent{.type = LightComponent::Type::Directional, .intensity = 500.0f},
      LightComponent{.type = LightComponent::Type::Point, .intensity = 50.0f},
      LightComponent{.type = LightComponent::Type::Point, .intensity = 50.0f, .range = 3.0f},
      LightComponent{.type = LightComponent::Type::Spot, .intensity = 500.0f, .innerConeAngle = 0.2f, .outerConeAngle = 0.4f},
  };

  std::vector<Entity> entities;
  for (const LightComponent& light : lights) {
    Entity entity = registry.createEntity();
    registry.addComponent<TransformComponent>(entity, Transform(Vec3(-2.0f, 0.0f, 0.5f)));
    registry.addComponent<LightComponent>(entity, light);
    entities.push_back(entity);
  }

  ioContext.app->runForFrames(3);
  for (Entity entity : entities) {
    registry.destroyEntity(entity);
  }

  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so all three frames ran.");
}

// BasicApp always has a sun and a sky, so only this test renders frames without a light buffer: its address stays 0, and
// the shader's light loop never reads it
NE_TEST_CASE("renderer", "Frames Render Without Lights Or An Environment") {
  Registry& registry = *ioContext.app->getRegistry();
  std::vector<std::pair<Entity, LightComponent>> lights;
  registry.view<LightComponent>().each([&](Entity iEntity, const LightComponent& iLight) { lights.emplace_back(iEntity, iLight); });
  std::vector<std::pair<Entity, EnvironmentComponent>> environments;
  registry.view<EnvironmentComponent>().each(
      [&](Entity iEntity, const EnvironmentComponent& iEnvironment) { environments.emplace_back(iEntity, iEnvironment); });
  NE_TEST_ASSERT(!lights.empty() && !environments.empty(), "BasicApp has a sun and an environment to remove.");

  for (const auto& [entity, light] : lights) {
    registry.removeComponent<LightComponent>(entity);
  }
  for (const auto& [entity, environment] : environments) {
    registry.removeComponent<EnvironmentComponent>(entity);
  }
  ioContext.app->runForFrames(3);
  for (const auto& [entity, light] : lights) {
    registry.addComponent<LightComponent>(entity, light);
  }
  for (const auto& [entity, environment] : environments) {
    registry.addComponent<EnvironmentComponent>(entity, environment);
  }

  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so all three frames ran.");
}

// One frame in each debug view, so every branch of the scene shader's view switch and both tonemap paths run under
// validation; the Cascades view reads the shadows of BasicApp's sun
NE_TEST_CASE("renderer", "Frames Render Every Debug View") {
  Renderer& renderer = *ioContext.app->getRenderer();
  const gpu::DebugView before = renderer.getDebugView();
  for (uint32_t view = 0; view < static_cast<uint32_t>(gpu::DebugView::Count); ++view) {
    renderer.setDebugView(static_cast<gpu::DebugView>(view));
    ioContext.app->runForFrames(1);
  }
  renderer.setDebugView(before);

  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so every view rendered.");
}

} // namespace ne::test

#endif
