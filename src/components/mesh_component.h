#pragma once

#include "core/math/math.h"
#include <memory>
#include <vector>

namespace ne {

class Mesh;
class Material;

// Submesh i is drawn with materials[i]; submeshes without a material are skipped
struct MeshComponent {
  std::shared_ptr<Mesh> mesh;
  std::vector<std::shared_ptr<Material>> materials;
  LinearColor colorTint = LinearColor::White;
};

} // namespace ne
