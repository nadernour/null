#include "renderer/mesh_utils.h"
#include <array>

namespace ne::MeshUtils {

MeshData createBoxMeshData(const Vec3& iSize) {
  SubmeshData meshData;
  meshData.mPositions.reserve(24);
  meshData.mNormals.reserve(24);
  meshData.mTexCoords.reserve(24);
  meshData.mColors.reserve(24);
  meshData.mIndices.reserve(36);

  const float hx = iSize.x * 0.5f;
  const float hy = iSize.y * 0.5f;
  const float hz = iSize.z * 0.5f;

  struct Face {
    Vec3 normal;
    std::array<Vec3, 4> positions;
  };

  const std::array<Face, 6> faces = {{
      // +X Face (Front)
      {Vec3(1.0f, 0.0f, 0.0f), {Vec3(+hx, -hy, -hz), Vec3(+hx, +hy, -hz), Vec3(+hx, +hy, +hz), Vec3(+hx, -hy, +hz)}},
      // -X Face (Back)
      {Vec3(-1.0f, 0.0f, 0.0f), {Vec3(-hx, +hy, -hz), Vec3(-hx, -hy, -hz), Vec3(-hx, -hy, +hz), Vec3(-hx, +hy, +hz)}},
      // +Y Face (Left)
      {Vec3(0.0f, 1.0f, 0.0f), {Vec3(+hx, +hy, -hz), Vec3(-hx, +hy, -hz), Vec3(-hx, +hy, +hz), Vec3(+hx, +hy, +hz)}},
      // -Y Face (Right)
      {Vec3(0.0f, -1.0f, 0.0f), {Vec3(-hx, -hy, -hz), Vec3(+hx, -hy, -hz), Vec3(+hx, -hy, +hz), Vec3(-hx, -hy, +hz)}},
      // +Z Face (Top)
      {Vec3(0.0f, 0.0f, 1.0f), {Vec3(-hx, -hy, +hz), Vec3(+hx, -hy, +hz), Vec3(+hx, +hy, +hz), Vec3(-hx, +hy, +hz)}},
      // -Z Face (Bottom)
      {Vec3(0.0f, 0.0f, -1.0f), {Vec3(-hx, +hy, -hz), Vec3(+hx, +hy, -hz), Vec3(+hx, -hy, -hz), Vec3(-hx, -hy, -hz)}},
  }};

  // Face corners wind counter-clockwise seen from outside, starting bottom-left; UV (0, 0) is the image's top-left
  const std::array<Vec2, 4> uvs = {
      Vec2(0.0f, 1.0f),
      Vec2(1.0f, 1.0f),
      Vec2(1.0f, 0.0f),
      Vec2(0.0f, 0.0f),
  };

  for (size_t f = 0; f < faces.size(); ++f) {
    const uint32_t baseIndex = static_cast<uint32_t>(meshData.mPositions.size());
    for (size_t v = 0; v < 4; ++v) {
      meshData.mPositions.push_back(faces[f].positions[v]);
      meshData.mNormals.push_back(faces[f].normal);
      meshData.mTexCoords.push_back(uvs[v]);
      meshData.mColors.push_back(LinearColor::White);
    }

    meshData.mIndices.push_back(baseIndex + 0);
    meshData.mIndices.push_back(baseIndex + 1);
    meshData.mIndices.push_back(baseIndex + 2);

    meshData.mIndices.push_back(baseIndex + 0);
    meshData.mIndices.push_back(baseIndex + 2);
    meshData.mIndices.push_back(baseIndex + 3);
  }

  return MeshData{.mSubmeshes = {std::move(meshData)}};
}

} // namespace ne::MeshUtils
