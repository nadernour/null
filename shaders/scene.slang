#language slang latest

[vk::binding(0, 0)]
SamplerState gSamplers[];

[vk::binding(1, 0)]
Texture2D gTextures[];

struct Vertex {
  float3 pos;
  float3 normal;
  float2 uv;
  float4 color;
};

struct GlobalUniforms {
  float4x4 viewProj;
  uint debugView;
};

struct DrawInfo {
  Vertex* vertices;
  uint instanceBaseOffset;
};

struct InstanceData {
  float4x4 modelMatrix;
  float4x4 normalMatrix;
  float4 color;
  uint materialIndex;
};

// Each texture packs its bindless index (low 16 bits) and its sampler index (high 16 bits)
struct Material {
  float4 baseColor;
  float3 emissive;
  float metallic;
  float roughness;
  float normalScale;
  float occlusionStrength;
  float alphaCutoff;
  uint baseColorTexture;
  uint metallicRoughnessTexture;
  uint normalTexture;
  uint occlusionTexture;
  uint emissiveTexture;
  uint flags;
};

struct ScenePushConstants {
  DrawInfo* drawInfos;
  GlobalUniforms* globalUniforms;
  InstanceData* instances;
  Material* materials;
};

// Mirrors gpu::DebugView
enum DebugView : uint { Lit, BaseColor, Normal, Metallic, Roughness, Occlusion, Emissive, Uv, Count };

[vk::push_constant]
ScenePushConstants pc;

struct VSInput {
  uint vertexId : SV_VertexID;
  uint drawIndex : SV_DrawIndex;
  uint instanceId : SV_InstanceID;
};

struct VSOutput {
  float4 pos : SV_Position;
  float3 normal;
  float2 uv;
  float4 color;
  nointerpolation uint materialIndex;
};

// One instanced draw mixes materials, so the indices differ between its invocations
float4 sampleTexture(uint packed, float2 uv) {
  return gTextures[NonUniformResourceIndex(packed & 0xFFFF)].Sample(gSamplers[NonUniformResourceIndex(packed >> 16)], uv);
}

// A debug view writes data through this, so the display's sRGB encode puts the stored value on screen
float3 srgbToLinear(float3 value) {
  return select(value <= 0.04045, value / 12.92, pow((value + 0.055) / 1.055, 2.4));
}

[shader("vertex")]
VSOutput vertMain(VSInput input) {
  VSOutput output;
  DrawInfo draw = pc.drawInfos[input.drawIndex];
  Vertex v = draw.vertices[input.vertexId];

  uint instanceIndex = draw.instanceBaseOffset + input.instanceId;
  InstanceData inst = pc.instances[instanceIndex];

  output.pos = mul(pc.globalUniforms->viewProj, mul(inst.modelMatrix, float4(v.pos, 1.0)));
  output.normal = normalize(mul((float3x3)inst.normalMatrix, v.normal));
  output.uv = v.uv;
  output.color = v.color * inst.color;
  output.materialIndex = inst.materialIndex;
  return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
  Material material = pc.materials[vertIn.materialIndex];
  float4 base = material.baseColor * sampleTexture(material.baseColorTexture, vertIn.uv) * vertIn.color;
  float4 metallicRoughness = sampleTexture(material.metallicRoughnessTexture, vertIn.uv);
  float metallic = material.metallic * metallicRoughness.b;
  float roughness = material.roughness * metallicRoughness.g;
  float ao = 1.0 + material.occlusionStrength * (sampleTexture(material.occlusionTexture, vertIn.uv).r - 1.0);
  float3 emissive = material.emissive * sampleTexture(material.emissiveTexture, vertIn.uv).rgb;
  float3 N = normalize(vertIn.normal);

  switch ((DebugView)pc.globalUniforms->debugView) {
    case DebugView.BaseColor:
      return float4(base.rgb, 1.0);
    case DebugView.Normal:
      return float4(srgbToLinear(N * 0.5 + 0.5), 1.0);
    case DebugView.Metallic:
      return float4(srgbToLinear(float3(metallic)), 1.0);
    case DebugView.Roughness:
      return float4(srgbToLinear(float3(roughness)), 1.0);
    case DebugView.Occlusion:
      return float4(srgbToLinear(float3(ao)), 1.0);
    case DebugView.Emissive:
      return float4(emissive, 1.0);
    case DebugView.Uv:
      return float4(srgbToLinear(float3(vertIn.uv, 0.0)), 1.0);
    default:
      break;
  }

  // A fixed light; occlusion darkens only the ambient term, and emission adds to the light the surface reflects
  float3 lightDir = normalize(float3(1.0, 0.5, 1.0));
  float NdotL = max(dot(N, lightDir), 0.0);
  float3 lighting = float3(0.18, 0.18, 0.18) * ao + float3(0.85, 0.85, 0.85) * NdotL;
  return float4(base.rgb * lighting + emissive, base.a);
}
