#pragma once

#include "core/math/math.h"
#include <memory>
#include <vector>

namespace ne {

class Mesh;
class Material;

// Submesh i is drawn with mMaterials[i]; submeshes without a material are skipped
struct MeshComponent {
  std::shared_ptr<Mesh> mMesh;
  std::vector<std::shared_ptr<Material>> mMaterials;
  LinearColor mColorTint = LinearColor::White;
};

} // namespace ne
