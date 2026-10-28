#include "core/mesh_data.h"
#include "core/logger.h"

namespace ne {

bool MeshData::validate() const {
  if (submeshes.empty()) {
    NE_ERROR("MeshData: Invalid mesh: it has no submeshes");
    return false;
  }
  for (size_t i = 0; i < submeshes.size(); ++i) {
    const SubmeshData& submesh = submeshes[i];
    if (submesh.positions.empty()) {
      NE_ERROR("MeshData: Invalid mesh: submesh {} has no vertices", i);
      return false;
    }
    if (submesh.indices.empty()) {
      NE_ERROR("MeshData: Invalid mesh: submesh {} has no indices", i);
      return false;
    }
    if (submesh.indices.size() % 3 != 0) {
      NE_ERROR("MeshData: Invalid mesh: submesh {} has {} indices, which aren't whole triangles", i, submesh.indices.size());
      return false;
    }
    for (uint32_t index : submesh.indices) {
      if (index >= submesh.positions.size()) {
        NE_ERROR("MeshData: Invalid mesh: submesh {} indexes vertex {} but has only {} vertices", i, index,
                 submesh.positions.size());
        return false;
      }
    }
  }
  return true;
}

} // namespace ne
