#pragma once

#include "core/math/math.h"
#include <cstdint>
#include <vector>

namespace ne {

// A glTF primitive
struct SubmeshData {
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> texCoords;
  std::vector<LinearColor> colors;
  std::vector<Vec4> tangents; // xyz toward +u; w = ±1, so that cross(normal, xyz) * w points up the image (toward -v)
  std::vector<uint32_t> indices;
  int32_t materialIndex = -1; // Index into ModelData::materials; -1 for the default material
};

// A glTF mesh; each submesh is drawn with its own material
struct MeshData {
  std::vector<SubmeshData> submeshes;

  // False, with the reason logged, when the mesh is unsafe to draw: it needs a submesh, and every submesh needs vertices
  // and whole triangles of indices that stay within them
  bool validate() const;
};

} // namespace ne
