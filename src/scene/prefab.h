#pragma once

#include "components/mesh_component.h"
#include "core/ecs.h"
#include "core/math/transform.h"
#include "core/model_data.h"

// std
#include <string>
#include <vector>

namespace ne {

class ResourceManager;

/**
 * @brief Reusable entity tree with its GPU meshes. Every instance shares the same meshes and materials,
 * so identical instances render as one instanced draw.
 */
class Prefab {
public:
  // Uploads the textures, materials, and meshes; the ModelData is not referenced afterwards
  Prefab(ResourceManager& ioResourceManager, const ModelData& iModelData);

  // Returns the new root entity, which is named after the prefab
  Entity instantiate(Registry& ioRegistry, const Transform& iRootTransform = Transform{}) const;

private:
  std::string mName;
  std::vector<MeshComponent> mMeshComponents; // Indexed by ModelNode::meshIndex; the mesh is null if it failed validation
  std::vector<ModelNode> mNodes;              // Parents always precede their children
};

} // namespace ne
