#pragma once

#include "core/mesh_data.h"

namespace ne::MeshUtils {

// 24 vertices so every face has its own normals and [0, 1] UVs; iSize is the full extent
MeshData createBoxMeshData(const Vec3& iSize = Vec3(1.0f));

} // namespace ne::MeshUtils
