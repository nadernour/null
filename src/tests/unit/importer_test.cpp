#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/filesystem.h"
#include "core/mesh_data.h"
#include "importers/gltf_importer.h"

#include <cgltf.h>

// std
#include <array>
#include <filesystem>
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

// Writes a one-triangle glTF and its .bin buffer to the temp directory and returns the .gltf path. Accessor 0 holds the
// positions (VEC3), 1 the indices, and 2 and 3 two UV sets (VEC2, all zeros and all ones). iAttributes is the primitive's
// attribute list, and iTopLevel is inserted after "asset" with its trailing comma.
std::filesystem::path writeTriangleGltf(const std::string& iName, const std::array<uint16_t, 3>& iIndices,
                                        const std::string& iAttributes = R"("POSITION": 0)", const std::string& iTopLevel = "") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path();
  const std::array<float, 9> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  const std::array<uint8_t, 2> padding = {0, 0}; // Keeps the UV views 4-byte aligned
  const std::array<float, 6> uvSet0 = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const std::array<float, 6> uvSet1 = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  std::ofstream bin(dir / (iName + ".bin"), std::ios::binary);
  bin.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
  bin.write(reinterpret_cast<const char*>(iIndices.data()), sizeof(iIndices));
  bin.write(reinterpret_cast<const char*>(padding.data()), sizeof(padding));
  bin.write(reinterpret_cast<const char*>(uvSet0.data()), sizeof(uvSet0));
  bin.write(reinterpret_cast<const char*>(uvSet1.data()), sizeof(uvSet1));
  bin.close();

  std::ofstream(dir / (iName + ".gltf")) << R"({
  "asset": {"version": "2.0"},
  )" + iTopLevel + R"(
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {)" +
                                                iAttributes + R"(}, "indices": 1}]}],
  "buffers": [{"uri": ")" + iName + R"(.bin", "byteLength": 92}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6},
    {"buffer": 0, "byteOffset": 44, "byteLength": 24},
    {"buffer": 0, "byteOffset": 68, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2"}
  ]
})";
  return dir / (iName + ".gltf");
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

NE_TEST_CASE("importer", "glTF With An Out-Of-Range Index Fails Validation") {
  const std::filesystem::path path = writeTriangleGltf("null_importer_test_triangle", {0, 1, 2});
  ModelData valid = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(valid.meshes.size() == 1 && valid.nodes.size() == 1, "A well-formed triangle imports.");

  writeTriangleGltf("null_importer_test_triangle", {0, 1, 3});
  ModelData invalid = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(invalid.meshes.empty() && invalid.nodes.empty(),
                 "Index 3 into 3 vertices fails validation and imports nothing.");

  std::filesystem::remove(path);
  std::filesystem::remove(std::filesystem::path(path).replace_extension(".bin"));
}

NE_TEST_CASE("importer", "glTF Requiring An Unsupported Extension Imports Nothing") {
  const std::filesystem::path path = writeTriangleGltf("null_importer_test_required", {0, 1, 2}, R"("POSITION": 0)",
                                                       R"("extensionsUsed": ["KHR_draco_mesh_compression"],)"
                                                       R"("extensionsRequired": ["KHR_draco_mesh_compression"],)");
  ModelData draco = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(draco.meshes.empty() && draco.nodes.empty(), "A file that requires Draco is rejected whole.");

  // Materials aren't imported, so a required material extension isn't honored either
  writeTriangleGltf("null_importer_test_required", {0, 1, 2}, R"("POSITION": 0)",
                    R"("extensionsUsed": ["KHR_texture_transform"], "extensionsRequired": ["KHR_texture_transform"],)");
  ModelData textureTransform = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(textureTransform.meshes.empty() && textureTransform.nodes.empty(),
                 "A file that requires an unsupported material extension is rejected whole.");

  writeTriangleGltf("null_importer_test_required", {0, 1, 2}, R"("POSITION": 0)",
                    R"("extensionsUsed": ["KHR_mesh_quantization"], "extensionsRequired": ["KHR_mesh_quantization"],)");
  ModelData quantized = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(quantized.meshes.size() == 1, "A file that requires KHR_mesh_quantization imports.");

  std::filesystem::remove(path);
  std::filesystem::remove(std::filesystem::path(path).replace_extension(".bin"));
}

NE_TEST_CASE("importer", "glTF Attributes: Mistyped Ones Are Dropped, Only Set 0 Is Read") {
  // Accessor 0 is VEC3, which glTF doesn't allow for TEXCOORD
  const std::filesystem::path path =
      writeTriangleGltf("null_importer_test_attributes", {0, 1, 2}, R"("POSITION": 0, "TEXCOORD_0": 0)");
  ModelData mistyped = GltfImporter::importModel(path.string());
  NE_TEST_ASSERT(mistyped.meshes.size() == 1 && mistyped.meshes[0].submeshes[0].texCoords.empty(),
                 "A VEC3 TEXCOORD_0 is dropped and the triangle still imports.");

  // TEXCOORD_1 (all ones) comes last, so reading whichever set comes last would replace TEXCOORD_0 (all zeros)
  writeTriangleGltf("null_importer_test_attributes", {0, 1, 2}, R"("POSITION": 0, "TEXCOORD_0": 2, "TEXCOORD_1": 3)");
  ModelData twoSets = GltfImporter::importModel(path.string());
  bool readsSet0 = twoSets.meshes.size() == 1 && twoSets.meshes[0].submeshes[0].texCoords.size() == 3;
  for (size_t i = 0; readsSet0 && i < 3; ++i) {
    readsSet0 = twoSets.meshes[0].submeshes[0].texCoords[i].equals(Vec2(0.0f));
  }
  NE_TEST_ASSERT(readsSet0, "UVs come from TEXCOORD_0, not TEXCOORD_1.");

  std::filesystem::remove(path);
  std::filesystem::remove(std::filesystem::path(path).replace_extension(".bin"));
}

} // namespace ne::test

#endif
