#include "core/mesh_utils.h"
#include "core/logger.h"

#include <mikktspace.h>

// std
#include <algorithm>
#include <array>
#include <bit>
#include <unordered_map>

namespace ne::MeshUtils {

namespace {

// An attribute's values in index order, one per index; an attribute without a value per vertex comes back empty
template <typename T>
std::vector<T> unweldAttribute(const std::vector<T>& iValues, const std::vector<uint32_t>& iIndices, size_t iVertexCount) {
  std::vector<T> unwelded;
  if (iValues.size() != iVertexCount) {
    return unwelded;
  }
  unwelded.reserve(iIndices.size());
  for (uint32_t index : iIndices) {
    unwelded.push_back(iValues[index]);
  }
  return unwelded;
}

// A vertex bit for bit: position, normal, UV, color, and tangent, with zeros for the attributes its submesh lacks
using VertexBits = std::array<uint32_t, 16>;

struct VertexBitsHash {
  size_t operator()(const VertexBits& iBits) const {
    uint64_t hash = 14695981039346656037ull; // FNV-1a, a word at a time
    for (uint32_t word : iBits) {
      hash = (hash ^ word) * 1099511628211ull;
    }
    return static_cast<size_t>(hash);
  }
};

// -0 and +0 are equal but differ in their sign bit, and cross products of axis-aligned edges give both. Adding +0 turns
// -0 into +0 and leaves every other value as it is, so the two zeros weld.
void copyBits(const float* iValues, size_t iCount, uint32_t* oBits) {
  for (size_t i = 0; i < iCount; ++i) {
    oBits[i] = std::bit_cast<uint32_t>(iValues[i] + 0.0f);
  }
}

// MikkTSpace's callbacks reach the submesh through the context's user data. It is unwelded, so a face's corner is
// the vertex at face * 3 + corner.
SubmeshData& submeshOf(const SMikkTSpaceContext* iContext) { return *static_cast<SubmeshData*>(iContext->m_pUserData); }
size_t cornerOf(int iFace, int iCorner) { return static_cast<size_t>(iFace) * 3 + static_cast<size_t>(iCorner); }

int getNumFaces(const SMikkTSpaceContext* iContext) { return static_cast<int>(submeshOf(iContext).indices.size() / 3); }

int getNumVerticesOfFace(const SMikkTSpaceContext*, int) { return 3; }

void getPosition(const SMikkTSpaceContext* iContext, float oPosition[], int iFace, int iCorner) {
  std::copy_n(submeshOf(iContext).positions[cornerOf(iFace, iCorner)].data(), 3, oPosition);
}

void getNormal(const SMikkTSpaceContext* iContext, float oNormal[], int iFace, int iCorner) {
  std::copy_n(submeshOf(iContext).normals[cornerOf(iFace, iCorner)].data(), 3, oNormal);
}

void getTexCoord(const SMikkTSpaceContext* iContext, float oTexCoord[], int iFace, int iCorner) {
  std::copy_n(submeshOf(iContext).texCoords[cornerOf(iFace, iCorner)].data(), 2, oTexCoord);
}

// MikkTSpace's v points up the image and glTF's down, so the bitangent's sign flips
void setTangent(const SMikkTSpaceContext* iContext, const float iTangent[], float iSign, int iFace, int iCorner) {
  submeshOf(iContext).tangents[cornerOf(iFace, iCorner)] = Vec4(iTangent[0], iTangent[1], iTangent[2], -iSign);
}

} // namespace

MeshData createBoxMeshData(const Vec3& iSize) {
  SubmeshData meshData;
  meshData.positions.reserve(24);
  meshData.normals.reserve(24);
  meshData.texCoords.reserve(24);
  meshData.colors.reserve(24);
  meshData.indices.reserve(36);

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
    const uint32_t baseIndex = static_cast<uint32_t>(meshData.positions.size());
    for (size_t v = 0; v < 4; ++v) {
      meshData.positions.push_back(faces[f].positions[v]);
      meshData.normals.push_back(faces[f].normal);
      meshData.texCoords.push_back(uvs[v]);
      meshData.colors.push_back(LinearColor::White);
    }

    meshData.indices.push_back(baseIndex + 0);
    meshData.indices.push_back(baseIndex + 1);
    meshData.indices.push_back(baseIndex + 2);

    meshData.indices.push_back(baseIndex + 0);
    meshData.indices.push_back(baseIndex + 2);
    meshData.indices.push_back(baseIndex + 3);
  }

  return MeshData{.submeshes = {std::move(meshData)}};
}

void unweldVertices(SubmeshData& ioSubmesh) {
  const size_t vertexCount = ioSubmesh.positions.size();
  ioSubmesh.positions = unweldAttribute(ioSubmesh.positions, ioSubmesh.indices, vertexCount);
  ioSubmesh.normals = unweldAttribute(ioSubmesh.normals, ioSubmesh.indices, vertexCount);
  ioSubmesh.texCoords = unweldAttribute(ioSubmesh.texCoords, ioSubmesh.indices, vertexCount);
  ioSubmesh.colors = unweldAttribute(ioSubmesh.colors, ioSubmesh.indices, vertexCount);
  ioSubmesh.tangents = unweldAttribute(ioSubmesh.tangents, ioSubmesh.indices, vertexCount);
  for (size_t i = 0; i < ioSubmesh.indices.size(); ++i) {
    ioSubmesh.indices[i] = static_cast<uint32_t>(i);
  }
}

void weldVertices(SubmeshData& ioSubmesh) {
  const size_t vertexCount = ioSubmesh.positions.size();
  const bool hasNormals = ioSubmesh.normals.size() == vertexCount;
  const bool hasTexCoords = ioSubmesh.texCoords.size() == vertexCount;
  const bool hasColors = ioSubmesh.colors.size() == vertexCount;
  const bool hasTangents = ioSubmesh.tangents.size() == vertexCount;

  // The first vertex with each set of bits stays, and the indices of its copies move to it
  SubmeshData welded;
  std::unordered_map<VertexBits, uint32_t, VertexBitsHash> weldedIndices;
  weldedIndices.reserve(vertexCount);
  std::vector<uint32_t> weldedIndexOf(vertexCount);
  for (size_t v = 0; v < vertexCount; ++v) {
    VertexBits bits{};
    copyBits(ioSubmesh.positions[v].data(), 3, &bits[0]);
    if (hasNormals) {
      copyBits(ioSubmesh.normals[v].data(), 3, &bits[3]);
    }
    if (hasTexCoords) {
      copyBits(ioSubmesh.texCoords[v].data(), 2, &bits[6]);
    }
    if (hasColors) {
      copyBits(&ioSubmesh.colors[v].r, 4, &bits[8]);
    }
    if (hasTangents) {
      copyBits(ioSubmesh.tangents[v].data(), 4, &bits[12]);
    }

    const auto [entry, isFirst] = weldedIndices.try_emplace(bits, static_cast<uint32_t>(welded.positions.size()));
    if (isFirst) {
      welded.positions.push_back(ioSubmesh.positions[v]);
      if (hasNormals) {
        welded.normals.push_back(ioSubmesh.normals[v]);
      }
      if (hasTexCoords) {
        welded.texCoords.push_back(ioSubmesh.texCoords[v]);
      }
      if (hasColors) {
        welded.colors.push_back(ioSubmesh.colors[v]);
      }
      if (hasTangents) {
        welded.tangents.push_back(ioSubmesh.tangents[v]);
      }
    }
    weldedIndexOf[v] = entry->second;
  }

  for (uint32_t& index : ioSubmesh.indices) {
    index = weldedIndexOf[index];
  }
  ioSubmesh.positions = std::move(welded.positions);
  ioSubmesh.normals = std::move(welded.normals);
  ioSubmesh.texCoords = std::move(welded.texCoords);
  ioSubmesh.colors = std::move(welded.colors);
  ioSubmesh.tangents = std::move(welded.tangents);
}

bool generateTangents(SubmeshData& ioSubmesh) {
  const size_t vertexCount = ioSubmesh.positions.size();
  if (ioSubmesh.normals.size() != vertexCount || ioSubmesh.texCoords.size() != vertexCount) {
    NE_WARN("MeshUtils: Can't generate tangents for a submesh without a normal and a UV per vertex");
    return false;
  }

  // MikkTSpace gives every triangle corner its own tangent, so each corner gets its own vertex; welding afterwards
  // merges the corners whose tangents agree
  SubmeshData corners = ioSubmesh;
  unweldVertices(corners);
  corners.tangents.assign(corners.positions.size(), Vec4(1.0f, 0.0f, 0.0f, 1.0f));

  SMikkTSpaceInterface callbacks{
      .m_getNumFaces = getNumFaces,
      .m_getNumVerticesOfFace = getNumVerticesOfFace,
      .m_getPosition = getPosition,
      .m_getNormal = getNormal,
      .m_getTexCoord = getTexCoord,
      .m_setTSpaceBasic = setTangent,
      .m_setTSpace = nullptr,
  };
  SMikkTSpaceContext context{.m_pInterface = &callbacks, .m_pUserData = &corners};
  if (!genTangSpaceDefault(&context)) {
    NE_WARN("MeshUtils: MikkTSpace failed to generate tangents for a submesh of {} triangles", corners.indices.size() / 3);
    return false;
  }

  weldVertices(corners);
  ioSubmesh = std::move(corners);
  return true;
}

} // namespace ne::MeshUtils
