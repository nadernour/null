#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/filesystem.h"
#include "core/model_data.h"
#include "importers/gltf_importer.h"

#include <cgltf.h>

// std
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>

namespace ne::test {

namespace {

// World-space positions of the first primitive of the first mesh node, as glTF itself places them (+Y up)
std::vector<Vec3> rawGltfWorldPositions(const std::string& iPath) {
  std::string fullPath = fs::resolveContentPath(iPath);
  cgltf_options options{};
  cgltf_data* data = nullptr;
  std::vector<Vec3> positions;
  if (cgltf_parse_file(&options, fullPath.c_str(), &data) != cgltf_result_success) {
    return positions;
  }
  if (cgltf_load_buffers(&options, data, fullPath.c_str()) == cgltf_result_success) {
    for (cgltf_size i = 0; i < data->nodes_count && positions.empty(); ++i) {
      const cgltf_node& node = data->nodes[i];
      if (!node.mesh) {
        continue;
      }
      Mat4 world;
      cgltf_node_transform_world(&node, world.data());
      for (cgltf_size a = 0; a < node.mesh->primitives[0].attributes_count; ++a) {
        const cgltf_attribute& attribute = node.mesh->primitives[0].attributes[a];
        if (attribute.type != cgltf_attribute_type_position) {
          continue;
        }
        for (cgltf_size v = 0; v < attribute.data->count; ++v) {
          Vec3 p;
          cgltf_accessor_read_float(attribute.data, v, &p.x, 3);
          Vec4 w = world * Vec4(p, 1.0f);
          positions.push_back(Vec3(w.x, w.y, w.z));
        }
      }
    }
  }
  cgltf_free(data);
  return positions;
}

// World matrix of a node by composing local transforms up to the model root
Mat4 nodeWorldMatrix(const ModelData& iModel, int32_t iNodeIndex) {
  Mat4 world = Mat4::Identity;
  for (int32_t i = iNodeIndex; i >= 0; i = iModel.nodes[i].parentIndex) {
    world = iModel.nodes[i].localTransform.toMatrix() * world;
  }
  return world;
}

int32_t firstMeshNode(const ModelData& iModel) {
  for (size_t i = 0; i < iModel.nodes.size(); ++i) {
    if (iModel.nodes[i].meshIndex >= 0) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

// Every imported vertex must land where glTF places it, with axes permuted from +Y up to +Z up: (x, y, z) -> (z, x, y)
bool matchesGltfWorldPositions(const ModelData& iModel, const std::vector<Vec3>& iGltfWorld) {
  int32_t nodeIndex = firstMeshNode(iModel);
  if (nodeIndex < 0 || iGltfWorld.empty()) {
    return false;
  }
  const SubmeshData& submesh = iModel.meshes[iModel.nodes[nodeIndex].meshIndex].submeshes[0];
  if (submesh.positions.size() != iGltfWorld.size()) {
    return false;
  }
  Mat4 world = nodeWorldMatrix(iModel, nodeIndex);
  for (size_t v = 0; v < iGltfWorld.size(); ++v) {
    Vec4 engine = world * Vec4(submesh.positions[v], 1.0f);
    Vec3 expected(iGltfWorld[v].z, iGltfWorld[v].x, iGltfWorld[v].y);
    if (!Vec3(engine.x, engine.y, engine.z).equals(expected, 1e-3f)) {
      return false;
    }
  }
  return true;
}

// Writes a one-triangle glTF and its .bin buffer to iDirectory and returns the .gltf path. Accessor 0 holds the
// positions (VEC3), 1 the indices, 2 and 3 two UV sets (VEC2: equal to the positions' x and y, and all ones), 4 normals
// (VEC3, all (0, 0, 1)), and 5 tangents (VEC4, all (1, 0, 0, -1)). iAttributes is the primitive's attribute list,
// iTopLevel is inserted after "asset" with its trailing comma, and iPrimitive after the primitive's indices with its
// leading comma.
std::filesystem::path writeTriangleGltf(const std::filesystem::path& iDirectory, const std::string& iName,
                                        const std::array<uint16_t, 3>& iIndices,
                                        const std::string& iAttributes = R"("POSITION": 0)", const std::string& iTopLevel = "",
                                        const std::string& iPrimitive = "") {
  const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  const std::array<uint8_t, 2> padding = {0, 0}; // Keeps the UV views 4-byte aligned
  const std::array<float, 6> uvSet0 = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 6> uvSet1 = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  const std::array<float, 9> normals = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 12> tangents = {1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, -1.0f};
  std::ofstream bin(iDirectory / (iName + ".bin"), std::ios::binary);
  bin.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
  bin.write(reinterpret_cast<const char*>(iIndices.data()), sizeof(iIndices));
  bin.write(reinterpret_cast<const char*>(padding.data()), sizeof(padding));
  bin.write(reinterpret_cast<const char*>(uvSet0.data()), sizeof(uvSet0));
  bin.write(reinterpret_cast<const char*>(uvSet1.data()), sizeof(uvSet1));
  bin.write(reinterpret_cast<const char*>(normals.data()), sizeof(normals));
  bin.write(reinterpret_cast<const char*>(tangents.data()), sizeof(tangents));
  bin.close();

  std::ofstream(iDirectory / (iName + ".gltf")) << R"({
  "asset": {"version": "2.0"},
  )" + iTopLevel + R"(
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {)" + iAttributes +
                                                       R"(}, "indices": 1)" + iPrimitive + R"(}]}],
  "buffers": [{"uri": ")" + iName + R"(.bin", "byteLength": 176}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6},
    {"buffer": 0, "byteOffset": 44, "byteLength": 24},
    {"buffer": 0, "byteOffset": 68, "byteLength": 24},
    {"buffer": 0, "byteOffset": 92, "byteLength": 36},
    {"buffer": 0, "byteOffset": 128, "byteLength": 48}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC4"}
  ]
})";
  return iDirectory / (iName + ".gltf");
}

// Writes a glTF whose one primitive has only POSITION (in glTF axes) and no indices, so every three positions make a
// triangle, and returns its path
std::filesystem::path writeUnindexedGltf(const std::filesystem::path& iDirectory, const std::vector<Vec3>& iPositions) {
  const size_t byteLength = iPositions.size() * sizeof(Vec3);
  std::ofstream(iDirectory / "unindexed.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(iPositions.data()), static_cast<std::streamsize>(byteLength));

  const std::string buffer = std::format(R"("buffers": [{{"uri": "unindexed.bin", "byteLength": {}}}],)", byteLength);
  const std::string bufferView = std::format(R"("bufferViews": [{{"buffer": 0, "byteLength": {}}}],)", byteLength);
  const std::string accessor =
      std::format(R"("accessors": [{{"bufferView": 0, "componentType": 5126, "count": {}, "type": "VEC3"}}])", iPositions.size());
  std::ofstream(iDirectory / "unindexed.gltf")
      << R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],)"
      << R"("meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],)" << buffer << bufferView << accessor << "}";
  return iDirectory / "unindexed.gltf";
}

} // namespace

NE_TEST_CASE("importer", "glTF Box: Node Matrix Hierarchy & Axis Conversion") {
  ModelData model = GltfImporter::importModel("models/Box.gltf");

  NE_TEST_ASSERT(model.nodes.size() == 2 && model.meshes.size() == 1, "Box has two nodes and one mesh.");
  NE_TEST_ASSERT(model.meshes[0].submeshes.size() == 1, "Box mesh has one submesh.");
  NE_TEST_ASSERT(model.nodes[0].parentIndex == -1 && model.nodes[0].meshIndex == -1, "Node 0 is the root without geometry.");
  NE_TEST_ASSERT(model.nodes[1].parentIndex == 0 && model.nodes[1].meshIndex == 0, "Node 1 draws the box.");

  // The root's glTF matrix is -90 deg about glTF +X, which is engine +Y (Left)
  Quat expectedRoot = Quat::angleAxis(math::radians(-90.0f), Vec3::Left);
  const Quat& rootRotation = model.nodes[0].localTransform.rotation;
  Quat negatedRoot(-rootRotation.x, -rootRotation.y, -rootRotation.z, -rootRotation.w);
  NE_TEST_ASSERT(rootRotation.equals(expectedRoot, 1e-4f) || negatedRoot.equals(expectedRoot, 1e-4f),
                 "Root matrix must decompose to -90 deg about engine +Y: {}", rootRotation.toString());

  NE_TEST_ASSERT(matchesGltfWorldPositions(model, rawGltfWorldPositions("models/Box.gltf")),
                 "Box world positions must equal glTF world positions with axes permuted.");

  // Counter-clockwise winding must agree with the authored normals (front faces under VK_FRONT_FACE_COUNTER_CLOCKWISE)
  const SubmeshData& box = model.meshes[0].submeshes[0];
  bool windingMatchesNormals = !box.normals.empty();
  for (size_t i = 0; i + 2 < box.indices.size(); i += 3) {
    const Vec3& p0 = box.positions[box.indices[i]];
    Vec3 faceNormal = (box.positions[box.indices[i + 1]] - p0).cross(box.positions[box.indices[i + 2]] - p0);
    windingMatchesNormals &= faceNormal.dot(box.normals[box.indices[i]]) > 0.0f;
  }
  NE_TEST_ASSERT(windingMatchesNormals, "Every triangle's counter-clockwise normal must point along its vertex normal.");
}

NE_TEST_CASE("importer", "glTF DamagedHelmet: Node TRS & Axis Conversion") {
  ModelData model = GltfImporter::importModel("models/DamagedHelmet.glb");

  NE_TEST_ASSERT(model.nodes.size() == 1 && model.meshes.size() == 1, "Helmet has one node and one mesh.");

  // glTF rotation (0.7071, 0, 0, 0.7071) is +90 deg about glTF +X, which is engine +Y (Left)
  Quat expected = Quat::angleAxis(math::radians(90.0f), Vec3::Left);
  NE_TEST_ASSERT(model.nodes[0].localTransform.rotation.equals(expected, 1e-4f),
                 "Node rotation must be converted to engine axes: {}", model.nodes[0].localTransform.rotation.toString());

  NE_TEST_ASSERT(matchesGltfWorldPositions(model, rawGltfWorldPositions("models/DamagedHelmet.glb")),
                 "Helmet world positions must equal glTF world positions with axes permuted.");
}

NE_TEST_CASE("importer", "glTF Box: A Material Of Factors Only") {
  ModelData model = GltfImporter::importModel("models/Box.gltf");

  NE_TEST_ASSERT(model.materials.size() == 1 && model.images.empty(), "Box has one material and no images.");
  const MaterialData& red = model.materials[0];
  NE_TEST_ASSERT(red.name == "Red", "The material keeps its name, got '{}'.", red.name);
  NE_TEST_ASSERT(math::equals(red.baseColor.r, 0.8f) && red.baseColor.g == 0.0f && red.baseColor.b == 0.0f &&
                     red.baseColor.a == 1.0f,
                 "Base color is (0.8, 0, 0, 1).");
  NE_TEST_ASSERT(red.metallic == 0.0f && red.roughness == 1.0f,
                 "Metallic is 0 as authored; roughness keeps glTF's default of 1.");
  NE_TEST_ASSERT(red.baseColorTexture.image < 0 && red.metallicRoughnessTexture.image < 0 && red.normalTexture.image < 0 &&
                     red.occlusionTexture.image < 0 && red.emissiveTexture.image < 0,
                 "The material has no textures.");
  NE_TEST_ASSERT(model.meshes[0].submeshes[0].materialIndex == 0, "The box uses material 0.");
}

NE_TEST_CASE("importer", "glTF DamagedHelmet: Material & Color Space Per Use") {
  ModelData model = GltfImporter::importModel("models/DamagedHelmet.glb");

  NE_TEST_ASSERT(model.materials.size() == 1 && model.images.size() == 5,
                 "Helmet has one material and five images, got {} and {}.", model.materials.size(), model.images.size());
  const MaterialData& material = model.materials[0];
  const auto hasImage = [&](const TextureRef& iTexture, bool iSrgb) {
    return iTexture.image >= 0 && model.images[iTexture.image].srgb == iSrgb;
  };
  NE_TEST_ASSERT(hasImage(material.baseColorTexture, true) && hasImage(material.emissiveTexture, true),
                 "Base color and emissive images hold colors, so they're sRGB.");
  NE_TEST_ASSERT(hasImage(material.metallicRoughnessTexture, false) && hasImage(material.normalTexture, false) &&
                     hasImage(material.occlusionTexture, false),
                 "Metallic-roughness, normal, and occlusion images hold data, so they're linear.");
  NE_TEST_ASSERT(material.metallic == 1.0f && material.roughness == 1.0f, "Unset metallic and roughness factors are glTF's 1.");
  NE_TEST_ASSERT(material.emissive.equals(Vec3(1.0f)), "The emissive factor is (1, 1, 1).");
  const SubmeshData& helmet = model.meshes[0].submeshes[0];
  NE_TEST_ASSERT(helmet.materialIndex == 0, "The helmet uses material 0.");
  NE_TEST_ASSERT(helmet.tangents.size() == helmet.positions.size(),
                 "The helmet has a normal texture but no TANGENT, so it gets MikkTSpace's, one per vertex.");
}

// The base color texture's sampler is nearest, clamp S, and mirror T. The metallic-roughness texture uses the same
// image without a sampler, and the normal texture reads TEXCOORD_1.
NE_TEST_CASE("importer", "glTF Textures: Wrap Per Axis, Color Space Per Use, And Dropped UV Sets") {
  const TempDirectory directory;
  std::filesystem::copy_file(fs::resolveContentPath("textures/uv_checker.png"), directory.getPath() / "texture.png");
  const std::filesystem::path path = writeTriangleGltf(
      directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0, "TEXCOORD_0": 2, "TEXCOORD_1": 3)",
      R"("materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 1}},)"
      R"( "normalTexture": {"index": 0, "texCoord": 1}}],)"
      R"("textures": [{"source": 0, "sampler": 0}, {"source": 0}],)"
      R"("samplers": [{"magFilter": 9728, "wrapS": 33071, "wrapT": 33648}],)"
      R"("images": [{"uri": "texture.png"}],)",
      R"(, "material": 0)");
  ModelData model = GltfImporter::importModel(path.string());

  NE_TEST_ASSERT(model.materials.size() == 1 && model.meshes.size() == 1, "The triangle and its material import.");
  const TextureRef& baseColor = model.materials[0].baseColorTexture;
  const TextureRef& metallicRoughness = model.materials[0].metallicRoughnessTexture;
  NE_TEST_ASSERT(baseColor.filter == TextureFilter::Nearest && baseColor.wrapU == TextureWrap::Clamp &&
                     baseColor.wrapV == TextureWrap::Mirror,
                 "A sampler's filter and per-axis wraps carry over.");
  NE_TEST_ASSERT(metallicRoughness.filter == TextureFilter::Linear && metallicRoughness.wrapU == TextureWrap::Repeat &&
                     metallicRoughness.wrapV == TextureWrap::Repeat,
                 "A texture without a sampler gets glTF's defaults: linear and repeat.");
  NE_TEST_ASSERT(model.images.size() == 2 && baseColor.image >= 0 && metallicRoughness.image >= 0 &&
                     model.images[baseColor.image].srgb && !model.images[metallicRoughness.image].srgb,
                 "An image used as a color and as data is imported once in each color space.");
  NE_TEST_ASSERT(model.materials[0].normalTexture.image < 0, "A texture on TEXCOORD_1 is dropped.");
  NE_TEST_ASSERT(model.meshes[0].submeshes[0].materialIndex == 0, "The triangle uses its material.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2});
  ModelData untextured = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(untextured.materials.size() == 1 && untextured.materials[0].metallic == 1.0f &&
                     untextured.materials[0].roughness == 1.0f && untextured.meshes[0].submeshes[0].materialIndex == 0,
                 "A primitive without a material gets glTF's default one, fully metallic and fully rough.");
}

// glTF's triangle (0, 0, 0), (1, 0, 0), (0, 1, 0) winds counter-clockwise around glTF +Z, which is engine +X
NE_TEST_CASE("importer", "glTF Normals & Tangents: Flat Normals When Missing, Tangents In Engine Axes") {
  const TempDirectory directory;
  const std::filesystem::path path = writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2});
  ModelData flat = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(flat.meshes.size() == 1, "The triangle imports.");
  const std::vector<Vec3>& flatNormals = flat.meshes[0].submeshes[0].normals;
  NE_TEST_ASSERT(flatNormals.size() == 3 && std::all_of(flatNormals.begin(), flatNormals.end(),
                                                        [](const Vec3& iNormal) { return iNormal.equals(Vec3::Forward); }),
                 "A triangle without NORMAL gets its face normal, engine +X, at each of its vertices.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0, "NORMAL": 4, "TANGENT": 5)");
  ModelData withNormals = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(withNormals.meshes.size() == 1, "The triangle imports.");
  const std::vector<Vec4>& tangents = withNormals.meshes[0].submeshes[0].tangents;
  NE_TEST_ASSERT(tangents.size() == 3 &&
                     std::all_of(tangents.begin(), tangents.end(),
                                 [](const Vec4& iTangent) { return iTangent.equals(Vec4(0.0f, 1.0f, 0.0f, -1.0f)); }),
                 "glTF's tangent (1, 0, 0) becomes engine +Y, and its w keeps its sign.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0, "TANGENT": 5)");
  ModelData withoutNormals = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(withoutNormals.meshes.size() == 1, "The triangle imports.");
  NE_TEST_ASSERT(withoutNormals.meshes[0].submeshes[0].tangents.empty() &&
                     withoutNormals.meshes[0].submeshes[0].normals.size() == 3,
                 "Without NORMAL, the TANGENT is ignored and flat normals take its place, as glTF requires.");
}

// The triangle's UVs equal its glTF x and y, so u grows along glTF +X (engine +Y) and v along glTF +Y (engine +Z)
NE_TEST_CASE("importer", "glTF Tangents: Generated Only For A Material With A Normal Texture") {
  const TempDirectory directory;
  std::filesystem::copy_file(fs::resolveContentPath("textures/uv_checker.png"), directory.getPath() / "normal.png");
  const std::string attributes = R"("POSITION": 0, "NORMAL": 4, "TEXCOORD_0": 2)";
  const std::filesystem::path path = writeTriangleGltf(
      directory.getPath(), "triangle", {0, 1, 2}, attributes,
      R"("materials": [{"normalTexture": {"index": 0}}], "textures": [{"source": 0}], "images": [{"uri": "normal.png"}],)",
      R"(, "material": 0)");
  ModelData normalMapped = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(normalMapped.meshes.size() == 1, "The triangle imports.");
  const std::vector<Vec4>& tangents = normalMapped.meshes[0].submeshes[0].tangents;
  NE_TEST_ASSERT(tangents.size() == 3 &&
                     std::all_of(tangents.begin(), tangents.end(),
                                 [](const Vec4& iTangent) { return iTangent.equals(Vec4(0.0f, 1.0f, 0.0f, -1.0f)); }),
                 "MikkTSpace's tangent points toward +u, engine +Y, and its w puts the bitangent toward -v, the same "
                 "tangent the file states in accessor 5.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, attributes, R"("materials": [{}],)", R"(, "material": 0)");
  ModelData plain = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(plain.meshes.size() == 1 && plain.meshes[0].submeshes[0].tangents.empty(),
                 "A material without a normal texture needs no tangents, so none are generated.");
}

// Two triangles that make the unit square in glTF's z = 0 plane; the corners (1, 0, 0) and (0, 1, 0) are listed twice
NE_TEST_CASE("importer", "glTF Flat Normals: Corners On One Flat Face Weld Back Together") {
  const TempDirectory directory;
  const std::filesystem::path path =
      writeUnindexedGltf(directory.getPath(), {Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
                                               Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f)});
  ModelData square = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(square.meshes.size() == 1, "The square imports.");
  const SubmeshData& submesh = square.meshes[0].submeshes[0];
  NE_TEST_ASSERT(submesh.positions.size() == 4 && submesh.indices.size() == 6,
                 "Both triangles get the same face normal, so their shared corners weld: 4 vertices, got {}.",
                 submesh.positions.size());
  NE_TEST_ASSERT(std::all_of(submesh.normals.begin(), submesh.normals.end(),
                             [](const Vec3& iNormal) { return iNormal.equals(Vec3::Forward); }),
                 "Every vertex keeps the face normal, engine +X.");
}

NE_TEST_CASE("importer", "glTF With An Out-Of-Range Index Fails Validation") {
  const TempDirectory directory;
  const std::filesystem::path path = writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2});
  ModelData valid = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(valid.meshes.size() == 1 && valid.nodes.size() == 1, "A well-formed triangle imports.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 3});
  ModelData invalid = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(invalid.meshes.empty() && invalid.nodes.empty(),
                 "Index 3 into 3 vertices fails validation and imports nothing.");
}

NE_TEST_CASE("importer", "glTF Primitives That Aren't Whole Triangles Are Skipped") {
  const TempDirectory directory;
  const std::filesystem::path path = writeUnindexedGltf(
      directory.getPath(), {Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f)});
  ModelData partial = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(partial.meshes.empty() && partial.nodes.size() == 1,
                 "Four vertices aren't whole triangles, which glTF requires, so the primitive is skipped and its node "
                 "kept.");
}

NE_TEST_CASE("importer", "glTF Requiring An Unsupported Extension Imports Nothing") {
  const TempDirectory directory;
  const std::filesystem::path path = writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0)",
                                                       R"("extensionsUsed": ["KHR_draco_mesh_compression"],)"
                                                       R"("extensionsRequired": ["KHR_draco_mesh_compression"],)");
  ModelData draco = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(draco.meshes.empty() && draco.nodes.empty(), "A file that requires Draco is rejected whole.");

  // Materials are imported, but texture transforms aren't honored
  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0)",
                    R"("extensionsUsed": ["KHR_texture_transform"], "extensionsRequired": ["KHR_texture_transform"],)");
  ModelData textureTransform = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(textureTransform.meshes.empty() && textureTransform.nodes.empty(),
                 "A file that requires an unsupported material extension is rejected whole.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0)",
                    R"("extensionsUsed": ["KHR_mesh_quantization"], "extensionsRequired": ["KHR_mesh_quantization"],)");
  ModelData quantized = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(quantized.meshes.size() == 1, "A file that requires KHR_mesh_quantization imports.");

  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0)",
                    R"("extensionsUsed": ["KHR_materials_emissive_strength"],)"
                    R"("extensionsRequired": ["KHR_materials_emissive_strength"],)");
  ModelData emissiveStrength = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(emissiveStrength.meshes.size() == 1, "A file that requires KHR_materials_emissive_strength imports.");
}

NE_TEST_CASE("importer", "glTF Attributes: Mistyped Ones Are Dropped, Only Set 0 Is Read") {
  // Accessor 0 is VEC3, which glTF doesn't allow for TEXCOORD
  const TempDirectory directory;
  const std::filesystem::path path =
      writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0, "TEXCOORD_0": 0)");
  ModelData mistyped = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(mistyped.meshes.size() == 1 && mistyped.meshes[0].submeshes[0].texCoords.empty(),
                 "A VEC3 TEXCOORD_0 is dropped and the triangle still imports.");

  // TEXCOORD_1 (all ones) comes last, so reading whichever set comes last would replace TEXCOORD_0
  writeTriangleGltf(directory.getPath(), "triangle", {0, 1, 2}, R"("POSITION": 0, "TEXCOORD_0": 2, "TEXCOORD_1": 3)");
  ModelData twoSets = GltfImporter::importModel(path.string());
  const std::array uvSet0{Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f)};
  bool readsSet0 = twoSets.meshes.size() == 1 && twoSets.meshes[0].submeshes[0].texCoords.size() == 3;
  for (size_t i = 0; readsSet0 && i < 3; ++i) {
    readsSet0 = twoSets.meshes[0].submeshes[0].texCoords[i].equals(uvSet0[i]);
  }
  NE_TEST_ASSERT(readsSet0, "UVs come from TEXCOORD_0, not TEXCOORD_1.");
}

} // namespace ne::test

#endif
