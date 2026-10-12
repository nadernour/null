#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "apps/application.h"
#include "components/camera_component.h"
#include "components/mesh_component.h"
#include "components/name_component.h"
#include "components/transform_component.h"
#include "core/ecs.h"
#include "renderer/mesh.h"
#include "scene/prefab.h"
#include "scene/transform_system.h"

#include <algorithm>

namespace ne::test {

namespace {

Entity makeNode(Registry& ioRegistry, const Transform& iLocal = Transform{}, Entity iParent = Entity::Null) {
  Entity entity = ioRegistry.createEntity();
  ioRegistry.addComponent<TransformComponent>(entity, iLocal);
  if (iParent.isValid()) {
    TransformSystem::setParent(ioRegistry, entity, iParent);
  }
  return entity;
}

TransformComponent& transformOf(Registry& ioRegistry, Entity iEntity) { return ioRegistry.getComponent<TransformComponent>(iEntity); }

Vec3 worldPosition(Registry& ioRegistry, Entity iEntity) {
  const Mat4& world = transformOf(ioRegistry, iEntity).getWorldMatrix();
  return Vec3(world[3].x, world[3].y, world[3].z);
}

bool hasChild(Registry& ioRegistry, Entity iParent, Entity iChild) {
  const auto& children = transformOf(ioRegistry, iParent).getChildren();
  return std::find(children.begin(), children.end(), iChild) != children.end();
}

} // namespace

NE_TEST_CASE("scene", "TransformSystem World Matrix Propagation") {
  Registry registry;
  Entity root = makeNode(registry, Transform(Vec3(1.0f, 0.0f, 0.0f), Quat::angleAxis(math::radians(90.0f), Vec3::Up)));
  Entity child = makeNode(registry, Transform(Vec3(1.0f, 0.0f, 0.0f), Quat::Identity, Vec3(2.0f)), root);
  Entity grandchild = makeNode(registry, Transform(Vec3(0.0f, 0.0f, 1.0f)), child);

  TransformSystem::update(registry);

  const Mat4 rootLocal = transformOf(registry, root).getLocal().toMatrix();
  const Mat4 childLocal = transformOf(registry, child).getLocal().toMatrix();
  const Mat4 grandLocal = transformOf(registry, grandchild).getLocal().toMatrix();
  NE_TEST_ASSERT(transformOf(registry, root).getWorldMatrix().equals(rootLocal), "Root world matrix must equal its local matrix.");
  NE_TEST_ASSERT(transformOf(registry, child).getWorldMatrix().equals(rootLocal * childLocal), "Child world = parent world * local.");
  NE_TEST_ASSERT(transformOf(registry, grandchild).getWorldMatrix().equals(rootLocal * childLocal * grandLocal),
                 "Grandchild world = parent world * local.");

  // Root yawed 90 deg (forward -> +Y): child sits one unit along the root's forward; parent scale 2 doubles the grandchild offset
  NE_TEST_ASSERT(worldPosition(registry, child).equals(Vec3(1.0f, 1.0f, 0.0f), 1e-4f), "Child world position follows root rotation.");
  NE_TEST_ASSERT(worldPosition(registry, grandchild).equals(Vec3(1.0f, 1.0f, 2.0f), 1e-4f), "Grandchild world position follows parent scale.");
}

NE_TEST_CASE("scene", "TransformSystem Recomputes Only What Changed") {
  Registry registry;
  Entity root = makeNode(registry, Transform(Vec3(1.0f, 0.0f, 0.0f)));
  Entity child = makeNode(registry, Transform(Vec3(0.0f, 1.0f, 0.0f)), root);
  Entity grandchild = makeNode(registry, Transform(Vec3(0.0f, 0.0f, 1.0f)), child);
  TransformSystem::update(registry);

  // A clean child of a moved root must still follow it
  transformOf(registry, root).setLocalPosition(Vec3(5.0f, 0.0f, 0.0f));
  TransformSystem::update(registry);
  NE_TEST_ASSERT(worldPosition(registry, grandchild).equals(Vec3(5.0f, 1.0f, 1.0f)), "Descendants of a changed root are recomputed.");

  // Changing a mid node updates its subtree and leaves its parent alone
  transformOf(registry, child).setLocalPosition(Vec3(0.0f, 2.0f, 0.0f));
  TransformSystem::update(registry);
  NE_TEST_ASSERT(worldPosition(registry, root).equals(Vec3(5.0f, 0.0f, 0.0f)), "Ancestors of a changed node keep their world.");
  NE_TEST_ASSERT(worldPosition(registry, grandchild).equals(Vec3(5.0f, 2.0f, 1.0f)), "Descendants of a changed node are recomputed.");

  // Re-parenting alone (no local change) must also refresh the world matrix
  Entity other = makeNode(registry, Transform(Vec3(0.0f, 0.0f, 10.0f)));
  TransformSystem::update(registry);
  TransformSystem::setParent(registry, grandchild, other);
  TransformSystem::update(registry);
  NE_TEST_ASSERT(worldPosition(registry, grandchild).equals(Vec3(0.0f, 0.0f, 11.0f)), "A re-parented node is recomputed.");
}

NE_TEST_CASE("scene", "TransformSystem Re-parenting & Detaching") {
  Registry registry;
  Entity parentA = makeNode(registry, Transform(Vec3(10.0f, 0.0f, 0.0f)));
  Entity parentB = makeNode(registry, Transform(Vec3(0.0f, 20.0f, 0.0f), Quat::angleAxis(math::radians(90.0f), Vec3::Up)));
  Entity child = makeNode(registry, Transform(Vec3(0.0f, 0.0f, 1.0f)), parentA);
  NE_TEST_ASSERT(transformOf(registry, child).getParent() == parentA && hasChild(registry, parentA, child), "Child is linked under A.");

  // KeepLocal: the local transform is kept and becomes relative to B
  TransformSystem::setParent(registry, child, parentB);
  NE_TEST_ASSERT(transformOf(registry, child).getParent() == parentB, "Child parent must be B.");
  NE_TEST_ASSERT(!hasChild(registry, parentA, child) && hasChild(registry, parentB, child), "Only B must list the child.");
  TransformSystem::update(registry);
  NE_TEST_ASSERT(worldPosition(registry, child).equals(Vec3(0.0f, 20.0f, 1.0f), 1e-4f), "KeepLocal places the child relative to B.");

  // KeepWorld: the child stays where it is in the world
  const Mat4 worldBefore = transformOf(registry, child).getWorldMatrix();
  TransformSystem::setParent(registry, child, parentA, TransformSystem::AttachRule::KeepWorld);
  TransformSystem::update(registry);
  NE_TEST_ASSERT(transformOf(registry, child).getWorldMatrix().equals(worldBefore, 1e-4f), "KeepWorld preserves the world matrix.");
  NE_TEST_ASSERT(transformOf(registry, child).getLocal().position.equals(Vec3(-10.0f, 20.0f, 1.0f), 1e-4f),
                 "KeepWorld recomputes the local transform relative to A.");

  // Detaching with KeepWorld makes the local transform the world transform
  TransformSystem::setParent(registry, child, Entity::Null, TransformSystem::AttachRule::KeepWorld);
  NE_TEST_ASSERT(!transformOf(registry, child).getParent().isValid(), "Detached child has no parent.");
  NE_TEST_ASSERT(transformOf(registry, parentA).getChildren().empty(), "A must have no children after detach.");
  TransformSystem::update(registry);
  NE_TEST_ASSERT(transformOf(registry, child).getWorldMatrix().equals(worldBefore, 1e-4f), "Detached child keeps its world matrix.");
}

NE_TEST_CASE("scene", "Destroying A Node Destroys Its Subtree") {
  Registry registry;
  Entity root = makeNode(registry);
  Entity mid = makeNode(registry, Transform{}, root);
  Entity leaf = makeNode(registry, Transform{}, mid);
  Entity sibling = makeNode(registry, Transform{}, root);

  registry.destroyEntity(mid);
  NE_TEST_ASSERT(!registry.isValid(mid) && !registry.isValid(leaf), "Destroyed node and its descendants must be invalid.");
  NE_TEST_ASSERT(registry.isValid(root) && registry.isValid(sibling), "Ancestors and siblings must survive.");
  NE_TEST_ASSERT(transformOf(registry, root).getChildren() == std::vector<Entity>{sibling}, "Parent must only list the surviving sibling.");
  TransformSystem::update(registry); // Must not hit stale links

  registry.destroyEntity(root);
  NE_TEST_ASSERT(registry.size() == 0, "Destroying the root must destroy the whole tree.");
}

NE_TEST_CASE("scene", "Removing A Node's Transform Detaches It And Destroys Its Children") {
  Registry registry;
  Entity root = makeNode(registry);
  Entity mid = makeNode(registry, Transform{}, root);
  Entity leaf = makeNode(registry, Transform{}, mid);

  registry.removeComponent<TransformComponent>(mid);
  NE_TEST_ASSERT(registry.isValid(mid) && !registry.hasComponent<TransformComponent>(mid), "The entity survives without its transform.");
  NE_TEST_ASSERT(!registry.isValid(leaf), "Its children are destroyed.");
  NE_TEST_ASSERT(transformOf(registry, root).getChildren().empty(), "Its parent no longer lists it.");
  TransformSystem::update(registry); // Must not hit stale links
}

NE_TEST_CASE("scene", "Parented Camera Uses World Pose") {
  Registry registry;
  Entity base = makeNode(registry, Transform(Vec3(5.0f, 0.0f, 0.0f), Quat::angleAxis(math::radians(90.0f), Vec3::Up)));
  Entity cameraEntity = makeNode(registry, Transform(Vec3(0.0f, 0.0f, 1.0f)), base);
  const auto& camera = registry.addComponent<CameraComponent>(cameraEntity);
  TransformSystem::update(registry);

  // Base yawed 90 deg: the camera at (5, 0, 1) looks along world +Y
  Mat4 view = camera.getViewMatrix(transformOf(registry, cameraEntity).getWorldMatrix());
  NE_TEST_ASSERT((view * Vec4(5.0f, 3.0f, 1.0f, 1.0f)).equals(Vec4(0.0f, 0.0f, 3.0f, 1.0f), 1e-4f),
                 "Point 3 units along world +Y must be 3 units ahead of the camera.");
}

NE_TEST_CASE("scene", "Prefab Instantiates Node Tree") {
  SubmeshData triangle;
  triangle.mPositions = {Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
  triangle.mIndices = {0, 1, 2};
  ModelData data;
  data.mName = "Robot";
  data.mMeshes = {MeshData{.mSubmeshes = {triangle}}, MeshData{.mSubmeshes = {triangle, triangle}}};
  data.mNodes = {
      ModelNode{.mName = "base", .mLocalTransform = Transform(Vec3(1.0f, 0.0f, 0.0f)), .mParentIndex = -1, .mMeshIndex = 0},
      ModelNode{.mName = "arm", .mLocalTransform = Transform(Vec3(0.0f, 0.0f, 1.0f)), .mParentIndex = 0, .mMeshIndex = 1},
      ModelNode{.mName = "tool", .mLocalTransform = Transform(Vec3(0.0f, 1.0f, 0.0f)), .mParentIndex = 1, .mMeshIndex = -1},
  };

  Registry registry;
  Prefab prefab(*ioContext.mApp->getResourceManager(), data);

  Entity first = prefab.instantiate(registry, Transform(Vec3(10.0f, 0.0f, 0.0f)));
  Entity second = prefab.instantiate(registry);
  TransformSystem::update(registry);

  NE_TEST_ASSERT(registry.size() == 8, "Each instance creates a root plus one entity per node.");
  NE_TEST_ASSERT(registry.getComponent<NameComponent>(first).mName == "Robot", "Root is named after the prefab.");

  const auto& rootChildren = transformOf(registry, first).getChildren();
  NE_TEST_ASSERT(rootChildren.size() == 1, "Root has the single root node as child.");
  Entity base = rootChildren[0];
  Entity arm = transformOf(registry, base).getChildren()[0];
  Entity tool = transformOf(registry, arm).getChildren()[0];

  NE_TEST_ASSERT(registry.getComponent<NameComponent>(base).mName == "base", "Node names are carried over.");
  NE_TEST_ASSERT(registry.getComponent<NameComponent>(tool).mName == "tool", "Node names are carried over.");

  const MeshComponent& armMesh = registry.getComponent<MeshComponent>(arm);
  NE_TEST_ASSERT(armMesh.mMesh->getSubmeshes().size() == 2, "Arm draws one mesh with both submeshes.");
  NE_TEST_ASSERT(armMesh.mMaterials.size() == 2, "Arm has one material per submesh.");
  NE_TEST_ASSERT(!registry.hasComponent<MeshComponent>(tool), "A node without geometry has no MeshComponent.");
  NE_TEST_ASSERT(worldPosition(registry, tool).equals(Vec3(11.0f, 1.0f, 1.0f)), "Node world position composes the whole chain.");

  Entity secondArm = transformOf(registry, transformOf(registry, second).getChildren()[0]).getChildren()[0];
  NE_TEST_ASSERT(registry.getComponent<MeshComponent>(secondArm).mMesh == armMesh.mMesh, "Instances share the same GPU mesh.");
}

} // namespace ne::test

#endif
