#include "renderer/mesh.h"
#include "core/assert.h"

namespace ne {

Mesh::Mesh(std::vector<Submesh> iSubmeshes) : mSubmeshes(std::move(iSubmeshes)) {
  NE_ASSERT(!mSubmeshes.empty(), "Mesh must have at least one submesh");
  for (const Submesh& submesh : mSubmeshes) {
    NE_ASSERT(submesh.vertexAddress != 0, "Vertex buffer device address must not be null");
    NE_ASSERT(submesh.indexCount > 0, "Index count must be greater than 0");
  }
}

} // namespace ne
