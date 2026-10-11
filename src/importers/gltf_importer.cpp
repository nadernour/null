#include "importers/gltf_importer.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/logger.h"

#include <cgltf.h>

// std
#include <cstring>
#include <optional>

namespace ne {

namespace {

// glTF (+Y up, +Z forward) to engine (+Z up, +X forward) is the axis permutation (x, y, z) -> (z, x, y). It is a
// rotation, so winding and tangent handedness are preserved and vectors, quaternion axes, and scales convert alike.
Vec3 toEngineAxes(const Vec3& iGltf) { return Vec3(iGltf.z, iGltf.x, iGltf.y); }
Quat toEngineAxes(const Quat& iGltf) { return Quat(iGltf.z, iGltf.x, iGltf.y, iGltf.w); }

std::optional<SubmeshData> importPrimitive(const cgltf_primitive& iPrimitive) {
  if (iPrimitive.type != cgltf_primitive_type_triangles) {
    return std::nullopt;
  }

  cgltf_accessor* pos_accessor = nullptr;
  cgltf_accessor* normal_accessor = nullptr;
  cgltf_accessor* texcoord_accessor = nullptr;
  cgltf_accessor* color_accessor = nullptr;

  for (cgltf_size k = 0; k < iPrimitive.attributes_count; ++k) {
    const cgltf_attribute& attribute = iPrimitive.attributes[k];
    if (attribute.type == cgltf_attribute_type_position) {
      pos_accessor = attribute.data;
    } else if (attribute.type == cgltf_attribute_type_normal) {
      normal_accessor = attribute.data;
    } else if (attribute.type == cgltf_attribute_type_texcoord) {
      texcoord_accessor = attribute.data;
    } else if (attribute.type == cgltf_attribute_type_color) {
      color_accessor = attribute.data;
    }
  }

  if (!pos_accessor) {
    return std::nullopt;
  }

  SubmeshData submesh;
  submesh.mPositions.resize(pos_accessor->count);
  if (normal_accessor) {
    submesh.mNormals.resize(pos_accessor->count);
  }
  if (texcoord_accessor) {
    submesh.mTexCoords.resize(pos_accessor->count);
  }
  if (color_accessor) {
    submesh.mColors.resize(pos_accessor->count);
  }

  for (cgltf_size v = 0; v < pos_accessor->count; ++v) {
    cgltf_bool pos_success = cgltf_accessor_read_float(pos_accessor, v, &submesh.mPositions[v].x, 3);
    NE_ASSERT(pos_success, "Failed to read vertex position");
    submesh.mPositions[v] = toEngineAxes(submesh.mPositions[v]);

    if (normal_accessor) {
      cgltf_bool normal_success = cgltf_accessor_read_float(normal_accessor, v, &submesh.mNormals[v].x, 3);
      NE_ASSERT(normal_success, "Failed to read vertex normal");
      submesh.mNormals[v] = toEngineAxes(submesh.mNormals[v]);
    }

    if (texcoord_accessor) {
      cgltf_bool texcoord_success = cgltf_accessor_read_float(texcoord_accessor, v, &submesh.mTexCoords[v].x, 2);
      NE_ASSERT(texcoord_success, "Failed to read vertex texture coordinates");
    }

    if (color_accessor) {
      // RGB accessors leave alpha at 1
      cgltf_bool color_success = cgltf_accessor_read_float(color_accessor, v, &submesh.mColors[v].r, 4);
      NE_ASSERT(color_success, "Failed to read vertex color");
    }
  }

  if (iPrimitive.indices) {
    submesh.mIndices.resize(iPrimitive.indices->count);
    for (cgltf_size idx = 0; idx < iPrimitive.indices->count; ++idx) {
      submesh.mIndices[idx] = static_cast<uint32_t>(cgltf_accessor_read_index(iPrimitive.indices, idx));
    }
  } else {
    submesh.mIndices.resize(pos_accessor->count);
    for (uint32_t idx = 0; idx < pos_accessor->count; ++idx) {
      submesh.mIndices[idx] = idx;
    }
  }

  if (submesh.mPositions.size() < 3 || submesh.mIndices.size() < 3) {
    return std::nullopt;
  }
  return submesh;
}

Transform importNodeTransform(const cgltf_node& iNode) {
  Transform gltfTransform;
  if (iNode.has_matrix) {
    Mat4 matrix;
    std::memcpy(matrix.data(), iNode.matrix, sizeof(iNode.matrix)); // Both column-major
    gltfTransform = Transform::fromMatrix(matrix);
  } else {
    gltfTransform.position = Vec3(iNode.translation[0], iNode.translation[1], iNode.translation[2]);
    gltfTransform.rotation = Quat(iNode.rotation[0], iNode.rotation[1], iNode.rotation[2], iNode.rotation[3]);
    gltfTransform.scale = Vec3(iNode.scale[0], iNode.scale[1], iNode.scale[2]);
  }

  if (gltfTransform.scale.x * gltfTransform.scale.y * gltfTransform.scale.z < 0.0f) {
    NE_WARN("GltfImporter: Node '{}' is mirrored (negative scale determinant); its winding will render inverted",
               iNode.name ? iNode.name : "");
  }

  return Transform(toEngineAxes(gltfTransform.position), toEngineAxes(gltfTransform.rotation),
                   toEngineAxes(gltfTransform.scale));
}

// Appends iNode and its subtree depth-first, so parents always precede their children
void importNode(const cgltf_data& iData, const cgltf_node& iNode, int32_t iParentIndex,
                const std::vector<int32_t>& iMeshIndices, ModelData& ioModel) {
  ModelNode node;
  node.mName = iNode.name ? iNode.name : "";
  node.mLocalTransform = importNodeTransform(iNode);
  node.mParentIndex = iParentIndex;
  if (iNode.mesh) {
    node.mMeshIndex = iMeshIndices[static_cast<size_t>(iNode.mesh - iData.meshes)];
  }

  const int32_t nodeIndex = static_cast<int32_t>(ioModel.mNodes.size());
  ioModel.mNodes.push_back(std::move(node));

  for (cgltf_size i = 0; i < iNode.children_count; ++i) {
    importNode(iData, *iNode.children[i], nodeIndex, iMeshIndices, ioModel);
  }
}

} // namespace

namespace GltfImporter {

ModelData importModel(const std::string& iPath) {
  std::string fullPath = ne::fs::resolveContentPath(iPath);
  NE_LOG("GltfImporter: Importing model from {}", fullPath);

  cgltf_options options{};
  cgltf_data* data = nullptr;
  cgltf_result result = cgltf_parse_file(&options, fullPath.c_str(), &data);
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: Failed to parse glTF file {} with error {}", fullPath, static_cast<int>(result));
    return {};
  }

  result = cgltf_load_buffers(&options, data, fullPath.c_str());
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: Failed to load buffers for glTF file {} with error {}", fullPath, static_cast<int>(result));
    cgltf_free(data);
    return {};
  }

  ModelData model;
  model.mName = iPath;

  // A glTF mesh used by several nodes is imported once; meshes without triangle primitives get index -1
  std::vector<int32_t> meshIndices(data->meshes_count, -1);
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    MeshData mesh;
    for (cgltf_size j = 0; j < data->meshes[i].primitives_count; ++j) {
      if (std::optional<SubmeshData> submesh = importPrimitive(data->meshes[i].primitives[j])) {
        mesh.mSubmeshes.push_back(std::move(*submesh));
      }
    }
    if (!mesh.mSubmeshes.empty()) {
      meshIndices[i] = static_cast<int32_t>(model.mMeshes.size());
      model.mMeshes.push_back(std::move(mesh));
    }
  }

  // The default scene, or every root node when the file defines no scene
  const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
  if (scene) {
    for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
      importNode(*data, *scene->nodes[i], -1, meshIndices, model);
    }
  } else {
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
      if (!data->nodes[i].parent) {
        importNode(*data, data->nodes[i], -1, meshIndices, model);
      }
    }
  }

  cgltf_free(data);
  NE_LOG("GltfImporter: Imported {} with {} nodes and {} meshes", fullPath, model.mNodes.size(), model.mMeshes.size());
  return model;
}

} // namespace GltfImporter

} // namespace ne
