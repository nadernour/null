#include "importers/gltf_importer.h"
#include "core/filesystem.h"
#include "core/logger.h"
#include "core/mesh_utils.h"
#include "importers/image_importer.h"

#include <cgltf.h>

// std
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>

namespace ne {

namespace {

// glTF (+Y up, +Z forward) to engine (+Z up, +X forward) is the axis permutation (x, y, z) -> (z, x, y). It is a
// rotation, so winding and tangent handedness are preserved and vectors, quaternion axes, and scales convert alike.
Vec3 toEngineAxes(const Vec3& iGltf) { return Vec3(iGltf.z, iGltf.x, iGltf.y); }
Quat toEngineAxes(const Quat& iGltf) { return Quat(iGltf.z, iGltf.x, iGltf.y, iGltf.w); }
// A tangent's w, the bitangent's sign, stays as it is
Vec4 toEngineAxes(const Vec4& iGltf) { return Vec4(iGltf.z, iGltf.x, iGltf.y, iGltf.w); }

template <typename T>
void convertToEngineAxes(std::vector<T>& ioValues) {
  for (T& value : ioValues) {
    value = toEngineAxes(value);
  }
}

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
constexpr const char* kSupportedExtensions[] = {"KHR_mesh_quantization", "KHR_materials_emissive_strength"};

bool isSupportedExtension(const char* iName) {
  for (const char* supported : kSupportedExtensions) {
    if (std::strcmp(iName, supported) == 0) {
      return true;
    }
  }
  return false;
}

struct GltfDataDeleter {
  void operator()(cgltf_data* iData) const { cgltf_free(iData); }
};
using GltfDataPtr = std::unique_ptr<cgltf_data, GltfDataDeleter>;

// Parses the file and loads its buffers. Null, with the reason logged, when either fails, when the file requires an
// extension the importer doesn't support, or when it fails validation.
GltfDataPtr loadGltf(const std::string& iPath) {
  cgltf_options options{};
  cgltf_data* parsed = nullptr;
  cgltf_result result = cgltf_parse_file(&options, iPath.c_str(), &parsed);
  GltfDataPtr data(parsed);
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: Failed to parse glTF file {}: {}", iPath, resultToString(result));
    return nullptr;
  }

  for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
    if (!isSupportedExtension(data->extensions_required[i])) {
      NE_ERROR("GltfImporter: glTF file {} requires {}, which the importer doesn't support", iPath, data->extensions_required[i]);
      return nullptr;
    }
  }

  result = cgltf_load_buffers(&options, data.get(), iPath.c_str());
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: Failed to load buffers for glTF file {}: {}", iPath, resultToString(result));
    return nullptr;
  }

  // Checks accessors against their buffers and indices against their primitive's vertex count, which needs the buffers loaded
  result = cgltf_validate(data.get());
  if (result != cgltf_result_success) {
    NE_ERROR("GltfImporter: glTF file {} failed validation: {}", iPath, resultToString(result));
    return nullptr;
  }
  return data;
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

// Reads all of iAccessor, sizeof(T) / sizeof(float) floats per element, so an RGB COLOR leaves alpha at 1. With the type
// checked, a read fails only when the data isn't in memory, such as a compressed buffer view.
template <typename T>
bool readAttribute(const cgltf_accessor& iAccessor, std::vector<T>& oValues) {
  oValues.resize(iAccessor.count);
  for (cgltf_size i = 0; i < iAccessor.count; ++i) {
    if (!cgltf_accessor_read_float(&iAccessor, i, reinterpret_cast<float*>(&oValues[i]), sizeof(T) / sizeof(float))) {
      return false;
    }
  }
  return true;
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
// instead, so the primitive still draws.
std::optional<SubmeshData> importPrimitive(const cgltf_primitive& iPrimitive, const char* iMeshName, cgltf_size iPrimitiveIndex) {
  if (iPrimitive.type != cgltf_primitive_type_triangles) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': only triangles are supported", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }

  SubmeshData submesh;
  bool isRead = true;
  for (cgltf_size k = 0; k < iPrimitive.attributes_count && isRead; ++k) {
    const cgltf_attribute& attribute = iPrimitive.attributes[k];
    if (attribute.index != 0) { // Only the first TEXCOORD and COLOR sets are used
      continue;
    }
    if (!hasAllowedType(attribute)) {
      NE_WARN("GltfImporter: Ignored {} of primitive {} of mesh '{}': glTF doesn't allow its type", attribute.name,
              iPrimitiveIndex, iMeshName);
      continue;
    }
    switch (attribute.type) {
      case cgltf_attribute_type_position:
        isRead = readAttribute(*attribute.data, submesh.positions);
        break;
      case cgltf_attribute_type_normal:
        isRead = readAttribute(*attribute.data, submesh.normals);
        break;
      case cgltf_attribute_type_tangent:
        isRead = readAttribute(*attribute.data, submesh.tangents);
        break;
      case cgltf_attribute_type_texcoord:
        isRead = readAttribute(*attribute.data, submesh.texCoords);
        break;
      case cgltf_attribute_type_color:
        isRead = readAttribute(*attribute.data, submesh.colors);
        break;
      default:
        break;
    }
  }
  if (!isRead) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': its vertex data can't be read", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }
  if (submesh.positions.empty()) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': it has no usable POSITION", iPrimitiveIndex, iMeshName);
    return std::nullopt;
  }
  // glTF ignores the tangents of a primitive without normals, which gets flat normals instead
  if (submesh.normals.empty()) {
    submesh.tangents.clear();
  }
  convertToEngineAxes(submesh.positions);
  convertToEngineAxes(submesh.normals);
  convertToEngineAxes(submesh.tangents);

  if (iPrimitive.indices) {
    const cgltf_size indexCount = iPrimitive.indices->count;
    submesh.indices.resize(indexCount);
    if (cgltf_accessor_unpack_indices(iPrimitive.indices, submesh.indices.data(), sizeof(uint32_t), indexCount) != indexCount) {
      NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': its indices can't be read", iPrimitiveIndex, iMeshName);
      return std::nullopt;
    }
  } else {
    submesh.indices.resize(submesh.positions.size());
    for (uint32_t idx = 0; idx < submesh.indices.size(); ++idx) {
      submesh.indices[idx] = idx;
    }
  }

  // glTF allows only whole triangles, which flat normals and MikkTSpace count on
  if (submesh.positions.size() < 3 || submesh.indices.size() < 3 || submesh.indices.size() % 3 != 0) {
    NE_WARN("GltfImporter: Skipped primitive {} of mesh '{}': triangles need 3 or more vertices and a multiple of 3 indices, "
            "it has {} and {}",
            iPrimitiveIndex, iMeshName, submesh.positions.size(), submesh.indices.size());
    return std::nullopt;
  }

  if (submesh.normals.empty()) {
    generateFlatNormals(submesh);
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

// Null, with the reason logged, when the image can't be decoded
std::optional<ImageData> decodeImage(const cgltf_image& iImage, cgltf_size iImageIndex, const std::filesystem::path& iDirectory) {
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

  // Relative to the glTF file's directory, which is absolute, so importFromFile doesn't resolve it again from the content
  // directory
  std::string uri = iImage.uri;
  uri.resize(cgltf_decode_uri(uri.data()));
  return ImageImporter::importFromFile((iDirectory / uri).string());
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

// The ModelData images made from one glTF image: one per color space its textures use, decoded once
struct ImageUses {
  int32_t linear = -1; // ModelData::images indices; -1 until a texture uses the image in that color space
  int32_t srgb = -1;
  bool isUnreadable = false; // Decoding failed, so every texture that uses it is dropped
};

// Builds the ModelData of one glTF file in three passes: the materials, then the meshes, whose tangents depend on their
// materials, then the nodes, which refer to the meshes
class ModelBuilder {
public:
  // iDirectory is the glTF file's, which image URIs are relative to; it must be absolute
  ModelBuilder(const cgltf_data& iData, std::filesystem::path iDirectory);

  // Call once: it moves the model out
  ModelData build();

private:
  MaterialData importMaterial(const cgltf_material& iMaterial);
  TextureRef importTexture(const cgltf_texture_view& iView, bool iSrgb, const char* iSlot, const char* iMaterialName);
  int32_t useImage(const cgltf_image& iImage, bool iSrgb);
  MeshData importMesh(const cgltf_mesh& iMesh);
  int32_t getDefaultMaterialIndex();
  void importNode(const cgltf_node& iNode, int32_t iParentIndex);

  const cgltf_data& mData;
  std::filesystem::path mDirectory;
  ModelData mModel;
  std::vector<ImageUses> mImageUses;  // One per glTF image
  std::vector<int32_t> mMeshIndices;  // ModelData::meshes index per glTF mesh; -1 for one without triangle primitives
  int32_t mDefaultMaterialIndex = -1; // -1 until a primitive names no material
};

ModelBuilder::ModelBuilder(const cgltf_data& iData, std::filesystem::path iDirectory)
    : mData(iData), mDirectory(std::move(iDirectory)), mImageUses(iData.images_count), mMeshIndices(iData.meshes_count, -1) {}

ModelData ModelBuilder::build() {
  // In file order, so ModelData::materials[i] is glTF material i; each image is imported when a material first uses it
  mModel.materials.reserve(mData.materials_count);
  for (cgltf_size i = 0; i < mData.materials_count; ++i) {
    mModel.materials.push_back(importMaterial(mData.materials[i]));
  }

  // A glTF mesh used by several nodes is imported once
  for (cgltf_size i = 0; i < mData.meshes_count; ++i) {
    MeshData mesh = importMesh(mData.meshes[i]);
    if (!mesh.submeshes.empty()) {
      mMeshIndices[i] = static_cast<int32_t>(mModel.meshes.size());
      mModel.meshes.push_back(std::move(mesh));
    }
  }

  // The default scene, else the first one, or every root node when the file defines no scene
  const cgltf_scene* scene = mData.scene ? mData.scene : (mData.scenes_count > 0 ? &mData.scenes[0] : nullptr);
  if (scene) {
    for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
      importNode(*scene->nodes[i], -1);
    }
  } else {
    for (cgltf_size i = 0; i < mData.nodes_count; ++i) {
      if (!mData.nodes[i].parent) {
        importNode(mData.nodes[i], -1);
      }
    }
  }
  return std::move(mModel);
}

// Base color and emissive textures hold colors, so their images are sRGB; the other textures hold data, so theirs are
// linear. An image used both ways is imported twice.
MaterialData ModelBuilder::importMaterial(const cgltf_material& iMaterial) {
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

  material.baseColorTexture = importTexture(pbr.base_color_texture, true, "base color", name);
  material.metallicRoughnessTexture = importTexture(pbr.metallic_roughness_texture, false, "metallic-roughness", name);
  material.normalTexture = importTexture(iMaterial.normal_texture, false, "normal", name);
  material.occlusionTexture = importTexture(iMaterial.occlusion_texture, false, "occlusion", name);
  material.emissiveTexture = importTexture(iMaterial.emissive_texture, true, "emissive", name);
  return material;
}

// The texture of one material slot, without an image when the importer can't honor it (the reason is logged)
TextureRef ModelBuilder::importTexture(const cgltf_texture_view& iView, bool iSrgb, const char* iSlot,
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
  texture.image = useImage(*iView.texture->image, iSrgb);
  if (texture.image < 0) {
    NE_WARN("GltfImporter: Dropped the {} texture of material '{}': its image can't be decoded", iSlot, iMaterialName);
  }
  return texture;
}

// The ModelData::images index of iImage in the color space, or -1 when it can't be decoded
int32_t ModelBuilder::useImage(const cgltf_image& iImage, bool iSrgb) {
  const cgltf_size imageIndex = cgltf_image_index(&mData, &iImage);
  ImageUses& uses = mImageUses[imageIndex];
  int32_t& entry = iSrgb ? uses.srgb : uses.linear;
  if (entry >= 0 || uses.isUnreadable) {
    return entry;
  }

  // An entry in the other color space already holds the pixels
  const int32_t other = iSrgb ? uses.linear : uses.srgb;
  std::optional<ImageData> image = other >= 0 ? std::optional(mModel.images[other]) : decodeImage(iImage, imageIndex, mDirectory);
  if (!image) {
    uses.isUnreadable = true;
    return -1;
  }
  image->srgb = iSrgb;
  entry = static_cast<int32_t>(mModel.images.size());
  mModel.images.push_back(std::move(*image));
  return entry;
}

// The mesh's triangle primitives, each with its material and the tangents that material needs
MeshData ModelBuilder::importMesh(const cgltf_mesh& iMesh) {
  const char* meshName = iMesh.name ? iMesh.name : "";
  MeshData mesh;
  for (cgltf_size i = 0; i < iMesh.primitives_count; ++i) {
    const cgltf_primitive& primitive = iMesh.primitives[i];
    std::optional<SubmeshData> submesh = importPrimitive(primitive, meshName, i);
    if (!submesh) {
      continue;
    }
    submesh->materialIndex =
        primitive.material ? static_cast<int32_t>(cgltf_material_index(&mData, primitive.material)) : getDefaultMaterialIndex();
    // glTF calculates missing tangents only for a material with a normal texture, from that texture's UVs. The importer
    // keeps a normal texture only on TEXCOORD_0, the set texCoords holds, so one it dropped asks for no tangents.
    if (submesh->tangents.empty() && mModel.materials[submesh->materialIndex].normalTexture.image >= 0) {
      MeshUtils::generateTangents(*submesh);
    }
    mesh.submeshes.push_back(std::move(*submesh));
  }
  return mesh;
}

// glTF's default material, for primitives that name none: white, fully metallic, and fully rough. It is added on first
// use, so it follows the file's own materials.
int32_t ModelBuilder::getDefaultMaterialIndex() {
  if (mDefaultMaterialIndex < 0) {
    mDefaultMaterialIndex = static_cast<int32_t>(mModel.materials.size());
    mModel.materials.push_back(MaterialData{.name = "glTF_Default", .metallic = 1.0f, .roughness = 1.0f});
  }
  return mDefaultMaterialIndex;
}

// Appends iNode and its subtree depth-first, so parents always precede their children
void ModelBuilder::importNode(const cgltf_node& iNode, int32_t iParentIndex) {
  ModelNode node;
  node.name = iNode.name ? iNode.name : "";
  node.localTransform = importNodeTransform(iNode);
  node.parentIndex = iParentIndex;
  if (iNode.mesh) {
    node.meshIndex = mMeshIndices[cgltf_mesh_index(&mData, iNode.mesh)];
  }

  const int32_t nodeIndex = static_cast<int32_t>(mModel.nodes.size());
  mModel.nodes.push_back(std::move(node));

  for (cgltf_size i = 0; i < iNode.children_count; ++i) {
    importNode(*iNode.children[i], nodeIndex);
  }
}

} // namespace

namespace GltfImporter {

ModelData importModel(const std::string& iPath) {
  const std::string fullPath = ne::fs::resolveContentPath(iPath);
  const GltfDataPtr data = loadGltf(fullPath);
  if (!data) {
    return {};
  }

  ModelData model = ModelBuilder(*data, std::filesystem::path(fullPath).parent_path()).build();
  model.name = iPath;
  NE_LOG("GltfImporter: Imported {}: {} nodes, {} meshes, {} materials, {} images", fullPath, model.nodes.size(),
         model.meshes.size(), model.materials.size(), model.images.size());
  return model;
}

} // namespace GltfImporter

} // namespace ne
