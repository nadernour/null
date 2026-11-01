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
  float4 tangent; // xyz toward +u; cross(normal, xyz) * w points up the image (toward -v)
};

// Every type takes the same path: a directional light is a point infinitely far away (position.w = 0), a light without
// a range has invRangeSq = 0, and a light without a cone has spotScale = 0 and spotOffset = 1
struct Light {
  float4 position;  // Point and spot: (world position, 1). Directional: (direction toward the light, 0)
  float3 intensity; // Linear color x intensity: lux for directional lights, candela for point and spot lights
  float invRangeSq;
  float3 direction; // Where the light travels
  float spotScale;
  float spotOffset;
};

struct GlobalUniforms {
  float4x4 viewProj;
  Light* lights;
  float4 eye; // Perspective camera: (position, 1). Orthographic: (backward direction, 0)
  float3 skyRadiance;
  float exposure;
  float3 groundRadiance;
  uint lightCount;
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

// Mirrors gpu::kMaterialHasNormalTexture
static const uint kMaterialHasNormalTexture = 1u << 0;

// Mirrors kUnitlessEmissiveNits (core/material_data.h)
static const float kUnitlessEmissiveNits = 1000.0;

static const float kPi = 3.14159265;

struct ScenePushConstants {
  DrawInfo* drawInfos;
  GlobalUniforms* globalUniforms;
  InstanceData* instances;
  Material* materials;
};

// Mirrors gpu::DebugView
enum DebugView : uint {
  Lit,
  BaseColor,
  Normal,
  GeometryNormal,
  Tangent,
  Bitangent,
  Metallic,
  Roughness,
  Occlusion,
  Emissive,
  Uv,
  Count
};

[vk::push_constant]
ScenePushConstants pc;

struct VSInput {
  uint vertexId : SV_VertexID;
  uint drawIndex : SV_DrawIndex;
  uint instanceId : SV_InstanceID;
};

struct VSOutput {
  float4 pos : SV_Position;
  float3 worldPos;
  float3 normal;
  float4 tangent; // xyz in world space; w is the bitangent's sign
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

  float4 worldPos = mul(inst.modelMatrix, float4(v.pos, 1.0));
  output.pos = mul(pc.globalUniforms->viewProj, worldPos);
  output.worldPos = worldPos.xyz;
  output.normal = normalize(mul((float3x3)inst.normalMatrix, v.normal));
  // The model matrix, not the normal matrix: a tangent lies in the surface, and the normal matrix keeps the normal
  // perpendicular to it under non-uniform scale
  output.tangent = float4(normalize(mul((float3x3)inst.modelMatrix, v.tangent.xyz)), v.tangent.w);
  output.uv = v.uv;
  output.color = v.color * inst.color;
  output.materialIndex = inst.materialIndex;
  return output;
}

// Factor x texture x vertex color x instance tint; mask and blend materials read its alpha
float4 baseColor(Material material, VSOutput vertIn) {
  return material.baseColor * sampleTexture(material.baseColorTexture, vertIn.uv) * vertIn.color;
}

// Karis, "Physically Based Shading on Mobile" (2014): a fit of how much of an even environment a surface reflects, as
// a scale and an offset of F0, so no lookup texture is needed
float3 envBrdfApprox(float3 F0, float roughness, float NdotV) {
  const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
  const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
  float4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
  float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
  return F0 * AB.x + AB.y;
}

// The sky's radiance above the horizon and the ground's below, world +Z up. A diffuse surface facing dir sees (dir.z +
// 1) / 2 of sky, weighted by angle the way diffuse light is, so this is exactly the diffuse light of a two-color sky.
float3 hemisphere(float3 dir) {
  return lerp(pc.globalUniforms->groundRadiance, pc.globalUniforms->skyRadiance, saturate(dir.z * 0.5 + 0.5));
}

// The glTF metal-roughness BRDF (GGX distribution, height-correlated Smith visibility, Schlick Fresnel) under every
// light, then the hemisphere ambient, which occlusion darkens, and the emission: what leaves the surface toward the
// camera, in nits
float3 litRadiance(float3 base, float metallic, float perceptualRoughness, float ao, float3 emissive, float3 N,
                   float3 worldPos) {
  GlobalUniforms* globals = pc.globalUniforms;
  // Toward the camera; an orthographic camera's eye has w = 0, which gives every pixel its backward direction
  float3 V = normalize(globals->eye.xyz - worldPos * globals->eye.w);
  float NdotV = max(dot(N, V), 1e-4); // A normal-mapped normal can face away from the camera
  float roughness = max(perceptualRoughness, 0.045); // Keeps the highlight of a mirror finite
  float3 F0 = lerp(float3(0.04), base, metallic);
  float3 diffuseColor = base * (1.0 - metallic);
  float a = roughness * roughness;
  float a2 = a * a;
  float3 radiance = float3(0.0);

  for (uint i = 0; i < globals->lightCount; ++i) {
    Light light = globals->lights[i];
    // Toward the light. For a directional light (w = 0) it's the same at every pixel and has length 1, so 1 / d2 is 1.
    float3 toLight = light.position.xyz - worldPos * light.position.w;
    // At least (1 cm)^2: a light touching the surface counts as 1 cm away, which keeps 1 / d2 finite
    float d2 = max(dot(toLight, toLight), 1e-4);
    float3 L = toLight * rsqrt(d2);
    float NdotL = dot(N, L);
    // Behind the surface. Skipping it also keeps normalize(V + L) from dividing by zero when L = -V, a light shining
    // straight into the camera: a NaN times a zero NdotL is still NaN.
    if (NdotL <= 0.0) {
      continue;
    }
    // The illuminance (lux) on a surface facing the light, after KHR_lights_punctual's range fade and cone
    float r = d2 * light.invRangeSq; // (distance / range)^2
    float cone = saturate(dot(light.direction, -L) * light.spotScale + light.spotOffset);
    float3 E = light.intensity * (saturate(1.0 - r * r) * cone * cone / d2);

    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (kPi * d * d);
    float Vis = 0.5 / (NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2) + NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2));
    float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    radiance += ((1.0 - F) * diffuseColor / kPi + F * D * Vis) * E * NdotL;
  }

  // Ambient Occlusion
  float3 R = normalize(lerp(reflect(-V, N), N, a));
  radiance += (diffuseColor * hemisphere(N) + envBrdfApprox(F0, roughness, NdotV) * hemisphere(R)) * ao;
  return radiance + emissive;
}

float4 shade(Material material, float4 base, VSOutput vertIn, bool isFrontFace) {
  float4 metallicRoughness = sampleTexture(material.metallicRoughnessTexture, vertIn.uv);
  float metallic = material.metallic * metallicRoughness.b;
  float roughness = material.roughness * metallicRoughness.g;
  float ao = 1.0 - material.occlusionStrength * (1.0 - sampleTexture(material.occlusionTexture, vertIn.uv).r);
  float3 emissive = material.emissive * sampleTexture(material.emissiveTexture, vertIn.uv).rgb;

  // The tangent frame as mikktspace.h prescribes: the interpolated normal and tangent as they arrive, the bitangent
  // from them, and only the final normal normalized. That is the exact inverse of what MikkTSpace bakers wrote.
  float3 N = vertIn.normal;
  float3 T = vertIn.tangent.xyz;
  float3 B = vertIn.tangent.w * cross(N, T);
  // Only double-sided materials draw back faces: the surface seen from behind, so every axis reverses. B reverses on
  // its own, since cross(-N, -T) is cross(N, T).
  if (!isFrontFace) {
    N = -N;
    T = -T;
    B = -B;
  }
  float3 geometryNormal = normalize(N);
  if ((material.flags & kMaterialHasNormalTexture) != 0) {
    float3 n = sampleTexture(material.normalTexture, vertIn.uv).xyz * 2.0 - 1.0;
    n.xy *= material.normalScale;
    N = n.x * T + n.y * B + n.z * N;
  }
  N = normalize(N);

  switch ((DebugView)pc.globalUniforms->debugView) {
    case DebugView.BaseColor:
      return float4(base.rgb, 1.0);
    case DebugView.Normal:
      return float4(srgbToLinear(N * 0.5 + 0.5), 1.0);
    case DebugView.GeometryNormal:
      return float4(srgbToLinear(geometryNormal * 0.5 + 0.5), 1.0);
    case DebugView.Tangent:
      return float4(srgbToLinear(normalize(T) * 0.5 + 0.5), 1.0);
    case DebugView.Bitangent:
      return float4(srgbToLinear(normalize(B) * 0.5 + 0.5), 1.0);
    case DebugView.Metallic:
      return float4(srgbToLinear(float3(metallic)), 1.0);
    case DebugView.Roughness:
      return float4(srgbToLinear(float3(roughness)), 1.0);
    case DebugView.Occlusion:
      return float4(srgbToLinear(float3(ao)), 1.0);
    case DebugView.Emissive:
      return float4(emissive / kUnitlessEmissiveNits, 1.0); // The emissive as authored, as the Sample Viewer shows it
    case DebugView.Uv:
      return float4(srgbToLinear(float3(vertIn.uv, 0.0)), 1.0);
    default:
      break;
  }

  // Exposed here, so the HDR target's fp16 holds it; the clamp catches the rest, such as a mirror under a bright light
  float3 radiance = litRadiance(base.rgb, metallic, roughness, ao, emissive, N, vertIn.worldPos);
  return float4(min(radiance * pc.globalUniforms->exposure, float3(64000.0)), base.a);
}

// Opaque and blend materials; the alpha it returns matters only to blending
[shader("fragment")]
float4 fragMain(VSOutput vertIn, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  Material material = pc.materials[vertIn.materialIndex];
  return shade(material, baseColor(material, vertIn), vertIn, isFrontFace);
}

// Mask materials only: depth can't be written before a shader that may discard has run, so opaque materials draw
// through fragMain and keep early depth testing
[shader("fragment")]
float4 fragMainMasked(VSOutput vertIn, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  Material material = pc.materials[vertIn.materialIndex];
  float4 base = baseColor(material, vertIn);
  if (base.a < material.alphaCutoff) {
    discard;
  }
  return shade(material, base, vertIn, isFrontFace);
}
