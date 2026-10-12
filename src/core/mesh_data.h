#pragma once

#include "core/math/math.h"
#include "core/math/transform.h"
#include <cstdint>
#include <vector>
#include <string>

namespace ne {

// A glTF primitive
struct SubmeshData {
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> texCoords;
  std::vector<LinearColor> colors;
  std::vector<uint32_t> indices;
};

// A glTF mesh; each submesh is drawn with its own material
struct MeshData {
  std::vector<SubmeshData> submeshes;
};

struct ModelNode {
  std::string name;
  Transform localTransform; // Relative to the parent node, or to the model root
  int32_t parentIndex = -1; // Index into ModelData::nodes; -1 for a root node
  int32_t meshIndex = -1;   // Index into ModelData::meshes; -1 when the node draws nothing
};

struct ModelData {
  std::vector<MeshData> meshes;
  std::vector<ModelNode> nodes; // Parents always precede their children
  std::string name;
};

} // namespace ne
