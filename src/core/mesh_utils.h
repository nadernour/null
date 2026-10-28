#pragma once

#include "core/mesh_data.h"

namespace ne::MeshUtils {

// 24 vertices so every face has its own normals and [0, 1] UVs; iSize is the full extent
MeshData createBoxMeshData(const Vec3& iSize = Vec3(1.0f));

// Gives each index a vertex of its own, in index order, and sets the indices to 0..n-1, so every triangle corner can
// take values of its own. weldVertices() undoes it.
void unweldVertices(SubmeshData& ioSubmesh);

// Merges vertices whose position, normal, UV, color, and tangent are bitwise equal (with -0 matching +0), keeping the
// order they first appear in, and rebuilds the indices
void weldVertices(SubmeshData& ioSubmesh);

// MikkTSpace tangents, which glTF names for meshes without their own. Each w makes cross(normal, tangent) * w point up
// the image (toward -v), as glTF normal maps expect. Needs normals and UVs; false, with the reason logged, without them
// or when MikkTSpace fails.
bool generateTangents(SubmeshData& ioSubmesh);

} // namespace ne::MeshUtils
