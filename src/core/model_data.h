#pragma once

#include "core/image_data.h"
#include "core/material_data.h"
#include "core/math/transform.h"
#include "core/mesh_data.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ne {

struct ModelNode {
  std::string name;
  Transform localTransform; // Relative to the parent node, or to the model root
  int32_t parentIndex = -1; // Index into ModelData::nodes; -1 for a root node
  int32_t meshIndex = -1;   // Index into ModelData::meshes; -1 when the node draws nothing
};

struct ModelData {
  std::vector<MeshData> meshes;
  std::vector<ModelNode> nodes; // Parents always precede their children
  std::vector<MaterialData> materials;
  std::vector<ImageData> images; // The materials' textures, one per color space an image is used in
  std::string name;
};

} // namespace ne
