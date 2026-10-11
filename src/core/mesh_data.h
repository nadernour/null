#pragma once

#include "core/math/math.h"
#include "core/math/transform.h"
#include <cstdint>
#include <vector>
#include <string>

namespace ne {

// A glTF primitive
struct SubmeshData {
  std::vector<Vec3> mPositions;
  std::vector<Vec3> mNormals;
  std::vector<Vec2> mTexCoords;
  std::vector<LinearColor> mColors;
  std::vector<uint32_t> mIndices;
};

// A glTF mesh; each submesh is drawn with its own material
struct MeshData {
  std::vector<SubmeshData> mSubmeshes;
};

struct ModelNode {
  std::string mName;
  Transform mLocalTransform; // Relative to the parent node, or to the model root
  int32_t mParentIndex = -1; // Index into ModelData::mNodes; -1 for a root node
  int32_t mMeshIndex = -1;   // Index into ModelData::mMeshes; -1 when the node draws nothing
};

struct ModelData {
  std::vector<MeshData> mMeshes;
  std::vector<ModelNode> mNodes; // Parents always precede their children
  std::string mName;
};

} // namespace ne
