#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/ecs.h"
#include "components/camera_component.h"
#include "components/transform_component.h"
#include "components/mesh_component.h"

namespace ne::test {

struct PositionComponent {
  float x{0.0f}, y{0.0f}, z{0.0f};
};

struct VelocityComponent {
  float vx{0.0f}, vy{0.0f}, vz{0.0f};
};

struct TagComponent {
  int tag{0};
};

struct HookedComponent {
  static inline int sRemoveCount = 0;

  static void onRemove(Registry& ioRegistry, Entity iEntity) {
    if (ioRegistry.hasComponent<HookedComponent>(iEntity)) {
      sRemoveCount++;
    }
  }
};

NE_TEST_CASE("ecs", "Entity Handle Allocation & Recycling") {
  Registry registry;

  Entity e1 = registry.createEntity();
  Entity e2 = registry.createEntity();

  NE_TEST_ASSERT(registry.isValid(e1), "e1 handle must be valid.");
  NE_TEST_ASSERT(registry.isValid(e2), "e2 handle must be valid.");
  NE_TEST_ASSERT(e1 != e2, "Entity handles must be unique.");

  registry.destroyEntity(e1);
  NE_TEST_ASSERT(!registry.isValid(e1), "e1 handle must be invalid after destruction.");

  Entity e3 = registry.createEntity();
  NE_TEST_ASSERT(e3.id == e1.id, "e3 should recycle e1's ID index.");
  NE_TEST_ASSERT(e3.version == e1.version + 1, "e3 must have incremented version.");
  NE_TEST_ASSERT(!registry.isValid(e1), "e1 handle must remain invalid.");
}

NE_TEST_CASE("ecs", "Component Emplace & Sparse Access") {
  Registry registry;

  Entity e2 = registry.createEntity();
  registry.addComponent<PositionComponent>(e2, 10.0f, 20.0f, 30.0f);
  NE_TEST_ASSERT(registry.hasComponent<PositionComponent>(e2), "e2 has PositionComponent.");

  auto& pos = registry.getComponent<PositionComponent>(e2);
  NE_TEST_ASSERT(math::equals(pos.x, 10.0f) && math::equals(pos.y, 20.0f) && math::equals(pos.z, 30.0f), "PositionComponent values match.");
}

NE_TEST_CASE("ecs", "Swap-and-Pop Array Integrity") {
  Registry registry;

  Entity e2 = registry.createEntity();
  Entity e3 = registry.createEntity();
  Entity e4 = registry.createEntity();

  registry.addComponent<PositionComponent>(e2, 10.0f, 20.0f, 30.0f);
  registry.addComponent<PositionComponent>(e4, 1.0f, 1.0f, 1.0f);
  registry.addComponent<PositionComponent>(e3, 5.0f, 5.0f, 5.0f);

  NE_TEST_ASSERT(registry.getPool<PositionComponent>().size() == 3, "Pool size should be 3.");
  registry.removeComponent<PositionComponent>(e2);
  NE_TEST_ASSERT(!registry.hasComponent<PositionComponent>(e2), "e2 component removed.");
  NE_TEST_ASSERT(registry.getPool<PositionComponent>().size() == 2, "Pool size should be 2.");
}

NE_TEST_CASE("ecs", "Cascading Entity Destruction & Multi-Pool Cleanup") {
  Registry registry;

  Entity e1 = registry.createEntity();
  registry.addComponent<PositionComponent>(e1, 1.0f, 2.0f, 3.0f);
  registry.addComponent<VelocityComponent>(e1, 0.1f, 0.2f, 0.3f);
  registry.addComponent<TagComponent>(e1, 100);

  Entity e2 = registry.createEntity();
  registry.addComponent<PositionComponent>(e2, 4.0f, 5.0f, 6.0f);

  NE_TEST_ASSERT(registry.getPool<PositionComponent>().size() == 2, "Position pool size is 2.");
  NE_TEST_ASSERT(registry.getPool<VelocityComponent>().size() == 1, "Velocity pool size is 1.");
  NE_TEST_ASSERT(registry.getPool<TagComponent>().size() == 1, "Tag pool size is 1.");

  registry.destroyEntity(e1);

  NE_TEST_ASSERT(!registry.isValid(e1), "e1 is invalidated.");
  NE_TEST_ASSERT(!registry.hasComponent<PositionComponent>(e1), "e1 position removed.");
  NE_TEST_ASSERT(!registry.hasComponent<VelocityComponent>(e1), "e1 velocity removed.");
  NE_TEST_ASSERT(!registry.hasComponent<TagComponent>(e1), "e1 tag removed.");

  NE_TEST_ASSERT(registry.getPool<PositionComponent>().size() == 1, "Position pool decremented to 1.");
  NE_TEST_ASSERT(registry.getPool<VelocityComponent>().size() == 0, "Velocity pool decremented to 0.");
  NE_TEST_ASSERT(registry.getPool<TagComponent>().size() == 0, "Tag pool decremented to 0.");
  NE_TEST_ASSERT(registry.hasComponent<PositionComponent>(e2), "e2 still has PositionComponent.");
}

NE_TEST_CASE("ecs", "getOrAdd Behavior") {
  Registry registry;
  auto& pool = registry.getPool<PositionComponent>();

  Entity e = registry.createEntity();
  NE_TEST_ASSERT(!pool.has(e), "e starts without component.");

  // First call adds component
  auto& comp1 = pool.getOrAdd(e);
  comp1.x = 42.0f;
  NE_TEST_ASSERT(pool.has(e), "Component was added by getOrAdd.");

  // Second call retrieves existing component
  auto& comp2 = pool.getOrAdd(e);
  NE_TEST_ASSERT(math::equals(comp2.x, 42.0f), "getOrAdd returns existing component reference.");
  NE_TEST_ASSERT(&comp1 == &comp2, "getOrAdd returns same memory address.");
}

NE_TEST_CASE("ecs", "Multi-Component View Iteration") {
  Registry registry;

  Entity e3 = registry.createEntity();
  Entity e4 = registry.createEntity();

  registry.addComponent<PositionComponent>(e3, 5.0f, 5.0f, 5.0f);
  registry.addComponent<PositionComponent>(e4, 1.0f, 1.0f, 1.0f);

  registry.addComponent<VelocityComponent>(e3, 2.0f, 0.0f, 0.0f);
  registry.addComponent<VelocityComponent>(e4, 0.0f, 3.0f, 0.0f);

  int matchingCount = 0;
  registry.view<PositionComponent, VelocityComponent>().each(
      [&](Entity, PositionComponent& ioPosition, const VelocityComponent& iVelocity) {
        matchingCount++;
        ioPosition.x += iVelocity.vx;
        ioPosition.y += iVelocity.vy;
      });

  NE_TEST_ASSERT(matchingCount == 2, "View must match exactly 2 entities.");
  NE_TEST_ASSERT(math::equals(registry.getComponent<PositionComponent>(e3).x, 7.0f), "e3 updated via view.");
  NE_TEST_ASSERT(math::equals(registry.getComponent<PositionComponent>(e4).y, 4.0f), "e4 updated via view.");
}

NE_TEST_CASE("ecs", "Disjoint & Empty View Queries") {
  Registry registry;

  Entity e1 = registry.createEntity();
  registry.addComponent<PositionComponent>(e1, 1.0f, 2.0f, 3.0f);

  Entity e2 = registry.createEntity();
  registry.addComponent<VelocityComponent>(e2, 4.0f, 5.0f, 6.0f);

  int count = 0;
  // Neither e1 nor e2 has BOTH Position and Velocity
  registry.view<PositionComponent, VelocityComponent>().each([&](Entity, PositionComponent&, VelocityComponent&) {
    count++;
  });

  NE_TEST_ASSERT(count == 0, "Disjoint multi-component view should match 0 entities.");
}

NE_TEST_CASE("ecs", "Dense Component Pool Iteration") {
  Registry registry;
  Entity e1 = registry.createEntity();
  Entity e2 = registry.createEntity();

  registry.addComponent<TagComponent>(e1, 10);
  registry.addComponent<TagComponent>(e2, 20);

  auto& pool = registry.getPool<TagComponent>();
  int sum = 0;
  for (const auto& tagComp : pool.components()) {
    sum += tagComp.tag;
  }
  NE_TEST_ASSERT(sum == 30, "Range-based for loop over pool.components() produces correct sum.");
  NE_TEST_ASSERT(pool.data() != nullptr, "pool.data() returns valid pointer.");
}

NE_TEST_CASE("ecs", "Registry Reset") {
  Registry registry;
  Entity e1 = registry.createEntity();
  Entity e2 = registry.createEntity();
  registry.addComponent<PositionComponent>(e1, 1.0f, 1.0f, 1.0f);
  registry.addComponent<PositionComponent>(e2, 2.0f, 2.0f, 2.0f);

  NE_TEST_ASSERT(registry.size() == 2, "Registry size before reset is 2.");
  registry.reset();

  NE_TEST_ASSERT(registry.size() == 0, "Registry size after reset is 0.");
  NE_TEST_ASSERT(!registry.isValid(e1), "e1 invalid after reset.");
  NE_TEST_ASSERT(!registry.isValid(e2), "e2 invalid after reset.");
  NE_TEST_ASSERT(registry.getPool<PositionComponent>().size() == 0, "Position pool cleared after reset.");

  registry.createEntity();
  registry.createEntity();
  NE_TEST_ASSERT(!registry.isValid(e1) && !registry.isValid(e2), "Handles from before the reset stay invalid once their ids are reused.");
}

NE_TEST_CASE("ecs", "Stale Handles Do Not Alias Recycled Entities") {
  Registry registry;
  Entity stale = registry.createEntity();
  registry.destroyEntity(stale);

  Entity recycled = registry.createEntity();
  registry.addComponent<TagComponent>(recycled, 7);
  NE_TEST_ASSERT(recycled.id == stale.id, "The id is recycled.");
  NE_TEST_ASSERT(registry.hasComponent<TagComponent>(recycled), "The new entity has its component.");
  NE_TEST_ASSERT(!registry.hasComponent<TagComponent>(stale), "A stale handle must not see the recycled entity's components.");
}

NE_TEST_CASE("ecs", "Remove Hooks Run Before Removal") {
  HookedComponent::sRemoveCount = 0;
  Registry registry;

  Entity e1 = registry.createEntity();
  registry.addComponent<HookedComponent>(e1);
  registry.removeComponent<HookedComponent>(e1);
  NE_TEST_ASSERT(HookedComponent::sRemoveCount == 1, "removeComponent() runs the hook while the component still exists.");

  Entity e2 = registry.createEntity();
  registry.addComponent<TagComponent>(e2, 1);
  registry.addComponent<HookedComponent>(e2);
  registry.destroyEntity(e2);
  NE_TEST_ASSERT(HookedComponent::sRemoveCount == 2, "destroyEntity() runs the hook while the component still exists.");

  registry.destroyEntity(registry.createEntity());
  NE_TEST_ASSERT(HookedComponent::sRemoveCount == 2, "Entities without the component do not run its hook.");
}

NE_TEST_CASE("ecs", "Camera and Mesh Component View Queries") {
  Registry registry;

  Entity cam = registry.createEntity();
  registry.addComponent<TransformComponent>(cam, Transform(Vec3(-5.0f, 0.0f, 1.0f)));
  auto& camComp = registry.addComponent<CameraComponent>(cam);
  camComp.primary = true;

  Entity meshObj = registry.createEntity();
  registry.addComponent<TransformComponent>(meshObj, Transform(Vec3(0.0f, 2.0f, 0.0f)));
  registry.addComponent<MeshComponent>(meshObj);

  Mat4 resolvedViewProj{1.0f};
  int camCount = 0;
  registry.view<TransformComponent, CameraComponent>().each(
      [&](Entity, const TransformComponent& iTransform, const CameraComponent& iCamera) {
        if (iCamera.primary) {
          resolvedViewProj = iCamera.getViewProjectionMatrix(iTransform.getLocal().toMatrix(), 16.0f / 9.0f);
          camCount++;
        }
      });

  NE_TEST_ASSERT(camCount == 1, "Exactly one primary camera entity resolved.");
  NE_TEST_ASSERT(!resolvedViewProj.equals(Mat4::Identity), "Resolved ViewProjection matrix must not be Identity.");

  int meshCount = 0;
  registry.view<TransformComponent, MeshComponent>().each(
      [&](Entity, const TransformComponent& iTransform, const MeshComponent&) {
        NE_TEST_ASSERT(iTransform.getLocal().position.equals(Vec3(0.0f, 2.0f, 0.0f)), "Mesh entity transform position matches.");
        meshCount++;
      });

  NE_TEST_ASSERT(meshCount == 1, "Exactly one mesh entity matched in view.");
}

NE_TEST_CASE("ecs", "In-Place Component Construction") {
  Registry registry;

  // Class component through its constructor
  Entity e1 = registry.createEntity();
  Transform local(Vec3(1.0f, 2.0f, 3.0f), Quat::angleAxis(math::radians(45.0f), Vec3::Up), Vec3(2.0f));
  auto& transform = registry.addComponent<TransformComponent>(e1, local);
  NE_TEST_ASSERT(transform.getLocal().equals(local), "Local transform forwarded correctly.");

  // Aggregate component through designated initializers
  Entity e2 = registry.createEntity();
  auto& camera = registry.addComponent<CameraComponent>(e2, CameraComponent{.fov = math::radians(60.0f), .nearClip = 0.5f});
  NE_TEST_ASSERT(math::equals(camera.fov, math::radians(60.0f)) && math::equals(camera.nearClip, 0.5f), "Initialized fields forwarded correctly.");
  NE_TEST_ASSERT(math::equals(camera.farClip, 1000.0f), "Omitted fields keep their defaults.");
}

} // namespace ne::test

#endif
