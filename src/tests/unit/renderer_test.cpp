#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "apps/application.h"
#include "components/mesh_component.h"
#include "components/transform_component.h"
#include "core/ecs.h"
#include "platform/window.h"
#include "renderer/bindless_manager.h"
#include "renderer/gpu_types.h"
#include "renderer/mesh_utils.h"
#include "renderer/resource_manager.h"
#include "renderer/utils.h"

#include <GLFW/glfw3.h>

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
  std::shared_ptr<Material> material = resources.createMaterial();

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

} // namespace ne::test

#endif
