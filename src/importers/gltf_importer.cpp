#include "importers/gltf_importer.h"
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

const char* resultToString(cgltf_result iResult) {
  switch (iResult) {
    case cgltf_result_success:
      return "success";
    case cgltf_result_data_too_short:
      return "data too short";
    case cgltf_result_unknown_format:
      return "unknown format";
    case cgltf_result_invalid_json:
      return "invalid JSON";
    case cgltf_result_invalid_gltf:
      return "invalid glTF";
    case cgltf_result_invalid_options:
      return "invalid options";
    case cgltf_result_file_not_found:
      return "file not found";
    case cgltf_result_io_error:
      return "I/O error";
    case cgltf_result_out_of_memory:
      return "out of memory";
    case cgltf_result_legacy_gltf:
      return "legacy glTF 1.0";
    case cgltf_result_max_enum:
      break;
  }
  return "unknown error";
}

// Required extensions the importer honors; a file that requires any other can't be loaded correctly, so it is rejected.
// KHR_mesh_quantization only adds accessor component types, which cgltf_accessor_read_float converts.
constexpr const char* kSupportedRequiredExtensions[] = {"KHR_mesh_quantization"};

bool isSupportedRequiredExtension(const char* iName) {
  for (const char* supported : kSupportedRequiredExtensions) {
    if (std::strcmp(iName, supported) == 0) {
      return true;
    }
  }
  return false;
}

// glTF allows POSITION and NORMAL only as VEC3, TEXCOORD as VEC2, and COLOR as VEC3 or VEC4
bool hasAllowedType(const cgltf_attribute& iAttribute) {
  switch (iAttribute.type) {
    case cgltf_attribute_type_position:
    case cgltf_attribute_type_normal:
      return iAttribute.data->type == cgltf_type_vec3;
    case cgltf_attribute_type_texcoord:
      return iAttribute.data->type == cgltf_type_vec2;
    case cgltf_attribute_type_color:
      return iAttribute.data->type == cgltf_type_vec3 || iAttribute.data->type == cgltf_type_vec4;
    default:
      return true;
  }
}

// Null, with the reason logged, when the primitive can't be drawn. An optional attribute glTF doesn't allow is dropped
// instead, so the primitive still draws.
std::optional<SubmeshData> importPrimitive(const cgltf_primitive& iPrimitive, const char* iMeshName, cgltf_size iPrimitiveIndex) {
  if (iPrimitive.type != cgltf_primitive_type_triangles) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': only triangles are supported", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }

  const cgltf_accessor* positionAccessor = nullptr;
  const cgltf_accessor* normalAccessor = nullptr;
  const cgltf_accessor* texCoordAccessor = nullptr;
  const cgltf_accessor* colorAccessor = nullptr;
  for (cgltf_size k = 0; k < iPrimitive.attributes_count; ++k) {
    const cgltf_attribute& attribute = iPrimitive.attributes[k];
    if (attribute.index != 0) { // Only the first TEXCOORD and COLOR sets are used
      continue;
    }
    if (!hasAllowedType(attribute)) {
      NE_WARN("GltfImporter: Ignored {} of primitive {} of mesh '{}': glTF doesn't allow its type", attribute.name,
              iPrimitiveIndex, iMeshName);
      continue;
    }
    if (attribute.type == cgltf_attribute_type_position) {
      positionAccessor = attribute.data;
    } else if (attribute.type == cgltf_attribute_type_normal) {
      normalAccessor = attribute.data;
    } else if (attribute.type == cgltf_attribute_type_texcoord) {
      texCoordAccessor = attribute.data;
    } else if (attribute.type == cgltf_attribute_type_color) {
      colorAccessor = attribute.data;
    }
  }

  if (!positionAccessor) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': it has no usable POSITION", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }

  const cgltf_size vertexCount = positionAccessor->count;
  SubmeshData submesh;
  submesh.positions.resize(vertexCount);
  if (normalAccessor) {
    submesh.normals.resize(vertexCount);
  }
  if (texCoordAccessor) {
    submesh.texCoords.resize(vertexCount);
  }
  if (colorAccessor) {
    submesh.colors.resize(vertexCount);
  }

  // With the types checked, a read fails only when the data isn't in memory, such as a compressed buffer view
  bool isRead = true;
  for (cgltf_size v = 0; v < vertexCount && isRead; ++v) {
    isRead = cgltf_accessor_read_float(positionAccessor, v, &submesh.positions[v].x, 3);
    submesh.positions[v] = toEngineAxes(submesh.positions[v]);

    if (normalAccessor) {
      isRead = isRead && cgltf_accessor_read_float(normalAccessor, v, &submesh.normals[v].x, 3);
      submesh.normals[v] = toEngineAxes(submesh.normals[v]);
    }

    if (texCoordAccessor) {
      isRead = isRead && cgltf_accessor_read_float(texCoordAccessor, v, &submesh.texCoords[v].x, 2);
    }

    if (colorAccessor) {
      // RGB accessors leave alpha at 1
      isRead = isRead && cgltf_accessor_read_float(colorAccessor, v, &submesh.colors[v].r, 4);
    }
  }
  if (!isRead) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': its vertex data can't be read", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }

  if (iPrimitive.indices) {
    const cgltf_size indexCount = iPrimitive.indices->count;
    submesh.indices.resize(indexCount);
    if (cgltf_accessor_unpack_indices(iPrimitive.indices, submesh.indices.data(), sizeof(uint32_t), indexCount) != indexCount) {
      NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': its indices can't be read", iPrimitiveIndex, iMeshName);
      return std::nullopt;
    }
  } else {
    submesh.indices.resize(vertexCount);
    for (uint32_t idx = 0; idx < vertexCount; ++idx) {
      submesh.indices[idx] = idx;
    }
  }

  if (submesh.positions.size() < 3 || submesh.indices.size() < 3) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': a triangle needs 3 vertices and 3 indices, it has {} and {}",
            iPrimitiveIndex, iMeshName, submesh.positions.size(), submesh.indices.size());
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

  return Transform(toEngineAxes(gltfTransform.position), toEngineAxes(gltfTransform.rotation), toEngineAxes(gltfTransform.scale));
}

// Appends iNode and its subtree depth-first, so parents always precede their children
void importNode(const cgltf_data& iData, const cgltf_node& iNode, int32_t iParentIndex,
                const std::vector<int32_t>& iMeshIndices, ModelData& ioModel) {
  ModelNode node;
  node.name = iNode.name ? iNode.name : "";
  node.localTransform = importNodeTransform(iNode);
  node.parentIndex = iParentIndex;
  if (iNode.mesh) {
    node.meshIndex = iMeshIndices[static_cast<size_t>(iNode.mesh - iData.meshes)];
  }

  const int32_t nodeIndex = static_cast<int32_t>(ioModel.nodes.size());
  ioModel.nodes.push_back(std::move(node));

  for (cgltf_size i = 0; i < iNode.children_count; ++i) {
    importNode(iData, *iNode.children[i], nodeIndex, iMeshIndices, ioModel);
  }
}

} // namespace

namespace GltfImporter {

ModelData importModel(const std::string& iPath) {
  std::string fullPath = ne::fs::resolveContentPath(iPath);

  cgltf_options options{};
  cgltf_data* data = nullptr;
  cgltf_result result = cgltf_parse_file(&options, fullPath.c_str(), &data);
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: Failed to parse glTF file {}: {}", fullPath, resultToString(result));
    return {};
  }

  for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
    if (!isSupportedRequiredExtension(data->extensions_required[i])) {
      NE_ERROR("GltfImporter: glTF file {} requires {}, which the importer doesn't support", fullPath,
               data->extensions_required[i]);
      cgltf_free(data);
      return {};
    }
  }

  result = cgltf_load_buffers(&options, data, fullPath.c_str());
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: Failed to load buffers for glTF file {}: {}", fullPath, resultToString(result));
    cgltf_free(data);
    return {};
  }

  // Checks accessors against their buffers and indices against their primitive's vertex count, which needs the buffers loaded
  result = cgltf_validate(data);
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: glTF file {} failed validation: {}", fullPath, resultToString(result));
    cgltf_free(data);
    return {};
  }

  ModelData model;
  model.name = iPath;

  // A glTF mesh used by several nodes is imported once; meshes without triangle primitives get index -1
  std::vector<int32_t> meshIndices(data->meshes_count, -1);
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    const char* meshName = data->meshes[i].name ? data->meshes[i].name : "";
    MeshData mesh;
    for (cgltf_size j = 0; j < data->meshes[i].primitives_count; ++j) {
      if (std::optional<SubmeshData> submesh = importPrimitive(data->meshes[i].primitives[j], meshName, j)) {
        mesh.submeshes.push_back(std::move(*submesh));
      }
    }
    if (!mesh.submeshes.empty()) {
      meshIndices[i] = static_cast<int32_t>(model.meshes.size());
      model.meshes.push_back(std::move(mesh));
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
  NE_LOG("GltfImporter: Imported {}: {} nodes, {} meshes", fullPath, model.nodes.size(), model.meshes.size());
  return model;
}

} // namespace GltfImporter

} // namespace ne
