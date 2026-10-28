#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/mesh_data.h"
#include "core/mesh_utils.h"

namespace ne::test {

// MikkTSpace's v points up the image and glTF's down, so generateTangents negates each w. Every triangle's UV gradients
// show whether it did: the tangent must point toward +u, and the bitangent, cross(N, T) * w, toward -v.
NE_TEST_CASE("mesh", "Box Tangents Point Toward +u, Their Bitangents Up The Image") {
  SubmeshData box = MeshUtils::createBoxMeshData().submeshes[0];
  NE_TEST_ASSERT(MeshUtils::generateTangents(box), "A box has normals and UVs, so it gets tangents.");
  NE_TEST_ASSERT(box.positions.size() == 24 && box.tangents.size() == 24 && box.indices.size() == 36,
                 "Welding merges the 36 corners back into the box's 24 vertices, got {}.", box.positions.size());

  bool isFrame = true;
  for (size_t v = 0; v < box.positions.size(); ++v) {
    const Vec3 tangent(box.tangents[v].x, box.tangents[v].y, box.tangents[v].z);
    isFrame &= math::equals(tangent.length(), 1.0f) && math::equals(tangent.dot(box.normals[v]), 0.0f) &&
               math::equals(math::abs(box.tangents[v].w), 1.0f);
  }
  NE_TEST_ASSERT(isFrame, "Every tangent is unit length and perpendicular to its normal, and every w is +1 or -1.");

  bool followsUvs = true;
  for (size_t i = 0; i + 2 < box.indices.size(); i += 3) {
    const uint32_t i0 = box.indices[i];
    const uint32_t i1 = box.indices[i + 1];
    const uint32_t i2 = box.indices[i + 2];
    // Each edge is dP/du * du + dP/dv * dv; solving for the two gradients
    const Vec3 e1 = box.positions[i1] - box.positions[i0];
    const Vec3 e2 = box.positions[i2] - box.positions[i0];
    const Vec2 d1 = box.texCoords[i1] - box.texCoords[i0];
    const Vec2 d2 = box.texCoords[i2] - box.texCoords[i0];
    const float det = d1.x * d2.y - d2.x * d1.y;
    const Vec3 dPdu = (e1 * d2.y - e2 * d1.y) / det;
    const Vec3 dPdv = (e2 * d1.x - e1 * d2.x) / det;
    for (uint32_t corner : {i0, i1, i2}) {
      const Vec4& t = box.tangents[corner];
      const Vec3 tangent(t.x, t.y, t.z);
      const Vec3 bitangent = box.normals[corner].cross(tangent) * t.w;
      followsUvs &= tangent.dot(dPdu) > 0.0f && bitangent.dot(dPdv) < 0.0f;
    }
  }
  NE_TEST_ASSERT(followsUvs, "Every tangent points toward +u and every bitangent toward -v, the image's up, as glTF "
                             "normal maps expect.");
}

} // namespace ne::test

#endif
