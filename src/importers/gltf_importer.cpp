#include "importers/gltf_importer.h"
#include "core/filesystem.h"
#include "core/logger.h"
#include "core/mesh_utils.h"
#include "importers/image_importer.h"

#include <cgltf.h>

// std
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>

namespace ne {

namespace {

// glTF (+Y up, +Z forward) to engine (+Z up, +X forward) is the axis permutation (x, y, z) -> (z, x, y). It is a
// rotation, so winding and tangent handedness are preserved and vectors, quaternion axes, and scales convert alike.
Vec3 toEngineAxes(const Vec3& iGltf) { return Vec3(iGltf.z, iGltf.x, iGltf.y); }
Quat toEngineAxes(const Quat& iGltf) { return Quat(iGltf.z, iGltf.x, iGltf.y, iGltf.w); }
// A tangent's w, the bitangent's sign, stays as it is
Vec4 toEngineAxes(const Vec4& iGltf) { return Vec4(iGltf.z, iGltf.x, iGltf.y, iGltf.w); }

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
// KHR_mesh_quantization only adds accessor component types, which cgltf_accessor_read_float converts, and
// KHR_materials_emissive_strength scales the emissive factor.
constexpr const char* kSupportedRequiredExtensions[] = {"KHR_mesh_quantization", "KHR_materials_emissive_strength"};

bool isSupportedRequiredExtension(const char* iName) {
  for (const char* supported : kSupportedRequiredExtensions) {
    if (std::strcmp(iName, supported) == 0) {
      return true;
    }
  }
  return false;
}

// glTF allows POSITION and NORMAL only as VEC3, TANGENT as VEC4, TEXCOORD as VEC2, and COLOR as VEC3 or VEC4
bool hasAllowedType(const cgltf_attribute& iAttribute) {
  switch (iAttribute.type) {
    case cgltf_attribute_type_position:
    case cgltf_attribute_type_normal:
      return iAttribute.data->type == cgltf_type_vec3;
    case cgltf_attribute_type_tangent:
      return iAttribute.data->type == cgltf_type_vec4;
    case cgltf_attribute_type_texcoord:
      return iAttribute.data->type == cgltf_type_vec2;
    case cgltf_attribute_type_color:
      return iAttribute.data->type == cgltf_type_vec3 || iAttribute.data->type == cgltf_type_vec4;
    default:
      return true;
  }
}

// glTF requires flat normals for a primitive without NORMAL: each triangle gets three vertices of its own, which take
// its counter-clockwise face normal. Welding then merges the corners that triangles on one flat face still share.
void generateFlatNormals(SubmeshData& ioSubmesh) {
  MeshUtils::unweldVertices(ioSubmesh);
  ioSubmesh.normals.resize(ioSubmesh.positions.size());
  for (size_t i = 0; i < ioSubmesh.positions.size(); i += 3) {
    const Vec3& p0 = ioSubmesh.positions[i];
    // Any length but zero normalizes, so tiny triangles keep theirs; a zero-area one covers no pixels, so Up does
    const Vec3 faceNormal =
        (ioSubmesh.positions[i + 1] - p0).cross(ioSubmesh.positions[i + 2] - p0).getSafeNormal(0.0f, Vec3::Up);
    ioSubmesh.normals[i] = faceNormal;
    ioSubmesh.normals[i + 1] = faceNormal;
    ioSubmesh.normals[i + 2] = faceNormal;
  }
  MeshUtils::weldVertices(ioSubmesh);
}

// Null, with the reason logged, when the primitive can't be drawn. An optional attribute glTF doesn't allow is dropped
// instead, so the primitive still draws. iHasNormalTexture says whether its material kept a normal texture, which needs
// tangents.
std::optional<SubmeshData> importPrimitive(const cgltf_primitive& iPrimitive, const char* iMeshName, cgltf_size iPrimitiveIndex,
                                           bool iHasNormalTexture) {
  if (iPrimitive.type != cgltf_primitive_type_triangles) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': only triangles are supported", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }

  const cgltf_accessor* positionAccessor = nullptr;
  const cgltf_accessor* normalAccessor = nullptr;
  const cgltf_accessor* tangentAccessor = nullptr;
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
    } else if (attribute.type == cgltf_attribute_type_tangent) {
      tangentAccessor = attribute.data;
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
  // glTF ignores the tangents of a primitive without normals, which gets flat normals instead
  if (!normalAccessor) {
    tangentAccessor = nullptr;
  }

  const cgltf_size vertexCount = positionAccessor->count;
  SubmeshData submesh;
  submesh.positions.resize(vertexCount);
  if (normalAccessor) {
    submesh.normals.resize(vertexCount);
  }
  if (tangentAccessor) {
    submesh.tangents.resize(vertexCount);
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

    if (tangentAccessor) {
      isRead = isRead && cgltf_accessor_read_float(tangentAccessor, v, &submesh.tangents[v].x, 4);
      submesh.tangents[v] = toEngineAxes(submesh.tangents[v]);
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

  // glTF allows only whole triangles, which the flat normals and MikkTSpace below count on
  if (submesh.positions.size() < 3 || submesh.indices.size() < 3 || submesh.indices.size() % 3 != 0) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': triangles need 3 or more vertices and a multiple of 3 indices, "
            "it has {} and {}",
            iPrimitiveIndex, iMeshName, submesh.positions.size(), submesh.indices.size());
    return std::nullopt;
  }

  if (!normalAccessor) {
    generateFlatNormals(submesh);
  }
  // glTF calculates missing tangents only for a material with a normal texture, from that texture's UVs. The importer
  // keeps a normal texture only on TEXCOORD_0, the set texCoords holds.
  if (iHasNormalTexture && submesh.tangents.empty()) {
    MeshUtils::generateTangents(submesh);
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
void importNode(const cgltf_data& iData, const cgltf_node& iNode, int32_t iParentIndex, const std::vector<int32_t>& iMeshIndices,
                ModelData& ioModel) {
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

// The ModelData images made from one glTF image: one per color space its textures use, decoded once
struct ImageUses {
  std::array<int32_t, 2> entries{-1, -1}; // ModelData::images index, linear then sRGB; -1 until a texture uses it so
  bool isUnreadable = false;              // Decoding failed, so every texture that uses it is dropped
};

// The model's images, made from the file's images as its materials use them
struct ModelImages {
  const cgltf_data& data;
  const std::string& path;        // The glTF file's, as importModel got it; image URIs are relative to it
  std::vector<ImageUses> uses;    // One per glTF image
  std::vector<ImageData>& images; // ModelData::images
};

// Null, with the reason logged, when the image can't be decoded
std::optional<ImageData> decodeImage(const cgltf_image& iImage, cgltf_size iImageIndex, const std::string& iModelPath) {
  if (iImage.buffer_view) {
    const uint8_t* bytes = cgltf_buffer_view_data(iImage.buffer_view);
    if (!bytes) {
      NE_WARN("GltfImporter: Image {} lies in a buffer that isn't loaded", iImageIndex);
      return std::nullopt;
    }
    return ImageImporter::importFromMemory({bytes, iImage.buffer_view->size});
  }
  if (!iImage.uri) {
    NE_WARN("GltfImporter: Image {} has neither a buffer view nor a URI", iImageIndex);
    return std::nullopt;
  }
  if (std::strncmp(iImage.uri, "data:", 5) == 0) {
    NE_WARN("GltfImporter: Image {} is embedded as a data: URI, which the importer doesn't read", iImageIndex);
    return std::nullopt;
  }
  if (std::strstr(iImage.uri, "://")) {
    NE_WARN("GltfImporter: Image {} is at {}; only local files are read", iImageIndex, iImage.uri);
    return std::nullopt;
  }

  // Relative to the glTF file, so importFromFile resolves it as importModel resolved the file: from the content
  // directory, unless it's absolute
  std::string uri = iImage.uri;
  uri.resize(cgltf_decode_uri(uri.data()));
  return ImageImporter::importFromFile((std::filesystem::path(iModelPath).parent_path() / uri).string());
}

// The ModelData::images index of iImage in the color space, or -1 when it can't be decoded
int32_t useImage(ModelImages& ioImages, const cgltf_image& iImage, bool iSrgb) {
  const cgltf_size imageIndex = cgltf_image_index(&ioImages.data, &iImage);
  ImageUses& uses = ioImages.uses[imageIndex];
  int32_t& entry = uses.entries[iSrgb ? 1 : 0];
  if (entry >= 0 || uses.isUnreadable) {
    return entry;
  }

  // An entry in the other color space already holds the pixels
  const int32_t other = uses.entries[iSrgb ? 0 : 1];
  std::optional<ImageData> image =
      other >= 0 ? std::optional(ioImages.images[other]) : decodeImage(iImage, imageIndex, ioImages.path);
  if (!image) {
    uses.isUnreadable = true;
    return -1;
  }
  image->srgb = iSrgb;
  entry = static_cast<int32_t>(ioImages.images.size());
  ioImages.images.push_back(std::move(*image));
  return entry;
}

TextureWrap toTextureWrap(cgltf_wrap_mode iMode) {
  switch (iMode) {
    case cgltf_wrap_mode_clamp_to_edge:
      return TextureWrap::Clamp;
    case cgltf_wrap_mode_mirrored_repeat:
      return TextureWrap::Mirror;
    case cgltf_wrap_mode_repeat:
      break;
  }
  return TextureWrap::Repeat; // Also for values glTF doesn't define
}

// The texture of one material slot, without an image when the importer can't honor it (the reason is logged)
TextureRef importTexture(ModelImages& ioImages, const cgltf_texture_view& iView, bool iSrgb, const char* iSlot,
                         const char* iMaterialName) {
  TextureRef texture;
  if (!iView.texture) {
    return texture;
  }
  // KHR_texture_basisu and EXT_texture_webp keep their images apart, so a texture with only those has no image here
  if (!iView.texture->image) {
    NE_WARN("GltfImporter: Dropped the {} texture of material '{}': it has no PNG or JPEG image", iSlot, iMaterialName);
    return texture;
  }
  if (iView.texcoord != 0) {
    NE_WARN("GltfImporter: Dropped the {} texture of material '{}': it uses TEXCOORD_{}, and only TEXCOORD_0 is imported", iSlot,
            iMaterialName, iView.texcoord);
    return texture;
  }
  if (iView.has_transform) {
    NE_WARN("GltfImporter: Ignored the KHR_texture_transform of the {} texture of material '{}'", iSlot, iMaterialName);
  }

  // A texture without a sampler keeps glTF's defaults: repeat and linear
  if (const cgltf_sampler* sampler = iView.texture->sampler) {
    texture.wrapU = toTextureWrap(sampler->wrap_s);
    texture.wrapV = toTextureWrap(sampler->wrap_t);
    texture.filter = sampler->mag_filter == cgltf_filter_type_nearest ? TextureFilter::Nearest : TextureFilter::Linear;
  }
  texture.image = useImage(ioImages, *iView.texture->image, iSrgb);
  if (texture.image < 0) {
    NE_WARN("GltfImporter: Dropped the {} texture of material '{}': its image can't be decoded", iSlot, iMaterialName);
  }
  return texture;
}

// Base color and emissive textures hold colors, so their images are sRGB; the other textures hold data, so theirs are
// linear. An image used both ways is imported twice.
MaterialData importMaterial(ModelImages& ioImages, const cgltf_material& iMaterial) {
  const cgltf_pbr_metallic_roughness& pbr = iMaterial.pbr_metallic_roughness;
  MaterialData material;
  material.name = iMaterial.name ? iMaterial.name : "";
  const char* name = material.name.c_str();

  // cgltf starts from glTF's defaults, also when pbrMetallicRoughness is missing
  material.baseColor =
      LinearColor{pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]};
  material.metallic = pbr.metallic_factor;
  material.roughness = pbr.roughness_factor;
  const float emissiveStrength = iMaterial.has_emissive_strength ? iMaterial.emissive_strength.emissive_strength : 1.0f;
  material.emissive =
      Vec3(iMaterial.emissive_factor[0], iMaterial.emissive_factor[1], iMaterial.emissive_factor[2]) * emissiveStrength;
  material.alphaCutoff = iMaterial.alpha_cutoff;
  if (iMaterial.alpha_mode == cgltf_alpha_mode_mask) {
    material.alphaMode = AlphaMode::Mask;
  } else if (iMaterial.alpha_mode == cgltf_alpha_mode_blend) {
    material.alphaMode = AlphaMode::Blend;
  }
  material.doubleSided = iMaterial.double_sided;
  // cgltf sets a texture's scale only along with the texture
  if (iMaterial.normal_texture.texture) {
    material.normalScale = iMaterial.normal_texture.scale;
  }
  if (iMaterial.occlusion_texture.texture) {
    material.occlusionStrength = iMaterial.occlusion_texture.scale;
  }

  material.baseColorTexture = importTexture(ioImages, pbr.base_color_texture, true, "base color", name);
  material.metallicRoughnessTexture = importTexture(ioImages, pbr.metallic_roughness_texture, false, "metallic-roughness", name);
  material.normalTexture = importTexture(ioImages, iMaterial.normal_texture, false, "normal", name);
  material.occlusionTexture = importTexture(ioImages, iMaterial.occlusion_texture, false, "occlusion", name);
  material.emissiveTexture = importTexture(ioImages, iMaterial.emissive_texture, true, "emissive", name);
  return material;
}

// glTF's default material, for primitives that name none: white, fully metallic, and fully rough
MaterialData makeGltfDefaultMaterial() { return MaterialData{.name = "glTF_Default", .metallic = 1.0f, .roughness = 1.0f}; }

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

  // In file order, so ModelData::materials[i] is glTF material i; each image is imported when a material first uses it
  ModelImages modelImages{
      .data = *data, .path = iPath, .uses = std::vector<ImageUses>(data->images_count), .images = model.images};
  model.materials.reserve(data->materials_count);
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    model.materials.push_back(importMaterial(modelImages, data->materials[i]));
  }

  // A glTF mesh used by several nodes is imported once; meshes without triangle primitives get index -1. A primitive
  // that names no material gets glTF's default one, which follows the file's own.
  int32_t defaultMaterialIndex = -1;
  std::vector<int32_t> meshIndices(data->meshes_count, -1);
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    const char* meshName = data->meshes[i].name ? data->meshes[i].name : "";
    MeshData mesh;
    for (cgltf_size j = 0; j < data->meshes[i].primitives_count; ++j) {
      const cgltf_primitive& primitive = data->meshes[i].primitives[j];
      // The materials are already imported, so a normal texture the importer dropped asks for no tangents
      const bool hasNormalTexture =
          primitive.material && model.materials[cgltf_material_index(data, primitive.material)].normalTexture.image >= 0;
      std::optional<SubmeshData> submesh = importPrimitive(primitive, meshName, j, hasNormalTexture);
      if (!submesh) {
        continue;
      }
      if (primitive.material) {
        submesh->materialIndex = static_cast<int32_t>(cgltf_material_index(data, primitive.material));
      } else {
        if (defaultMaterialIndex < 0) {
          defaultMaterialIndex = static_cast<int32_t>(model.materials.size());
          model.materials.push_back(makeGltfDefaultMaterial());
        }
        submesh->materialIndex = defaultMaterialIndex;
      }
      mesh.submeshes.push_back(std::move(*submesh));
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
  NE_LOG("GltfImporter: Imported {}: {} nodes, {} meshes, {} materials, {} images", fullPath, model.nodes.size(),
         model.meshes.size(), model.materials.size(), model.images.size());
  return model;
}

} // namespace GltfImporter

} // namespace ne
