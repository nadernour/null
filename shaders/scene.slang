#language slang latest

[vk::binding(0, 0)]
SamplerState gSamplers[];

[vk::binding(1, 0)]
Texture2D gTextures[];

[vk::binding(2, 0)]
SamplerComparisonState gShadowSampler;

// The types and constants named as in src/renderer/gpu_types.h mirror it, and it documents them

struct Vertex {
  float3 pos;
  float3 normal;
  float2 uv;
  float4 color;
  float4 tangent;
};

struct Light {
  float4 position;
  float3 intensity;
  float invRangeSq;
  float3 direction;
  float spotScale;
  float spotOffset;
};

struct ShadowData {
  float4x4 cascades[4];
  float4 depthPlane;
  float4 splitDepths;
  float4 normalOffsets;
  uint atlasTexture;
  float texelSize;
};

struct GlobalUniforms {
  float4x4 viewProj;
  Light* lights;
  ShadowData* shadows;
  float4 eye;
  float3 skyRadiance;
  float exposure;
  float3 groundRadiance;
  uint lightCount;
  uint debugView;
  uint shadowLight;
  uint envBrdfTexture;
};

// Marks the shadows pointer unused: comparing a pointer with null needs the shaderInt64 feature
static const uint kNoShadowLight = ~0u;

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

enum DebugView : uint {
  Lit,
  Cascades,
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

// vertIn.color is the vertex color times the instance tint
float4 baseColor(Material material, VSOutput vertIn) {
  return material.baseColor * sampleTexture(material.baseColorTexture, vertIn.uv) * vertIn.color;
}

// The cascade that covers a world point: the number of cascades that end before its view depth, so 4 is past them all
uint cascadeIndex(ShadowData* shadows, float3 worldPos) {
  float depth = dot(shadows->depthPlane.xyz, worldPos) + shadows->depthPlane.w;
  return uint(dot(select(depth > shadows->splitDepths, float4(1.0), float4(0.0)), float4(1.0)));
}

// 1 in light, 0 in shadow, from 3x3 PCF. The lookup moves off the surface along N, the geometry normal.
float shadowFactor(ShadowData* shadows, float3 worldPos, float3 N) {
  uint cascade = cascadeIndex(shadows, worldPos);
  if (cascade >= 4) {
    return 1.0; // Past the shadow distance
  }
  float3 position = worldPos + N * shadows->normalOffsets[cascade];
  float3 coord = mul(shadows->cascades[cascade], float4(position, 1.0)).xyz;
  // Reaches two texels out, which stays inside the tile's border (ShadowCascades::kBorderTexels)
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float2 uv = coord.xy + float2(x, y) * shadows->texelSize;
      lit += gTextures[shadows->atlasTexture].SampleCmpLevelZero(gShadowSampler, uv, coord.z);
    }
  }
  return lit / 9.0;
}

// The Cascades debug view's tints: red, green, blue, and yellow for cascades 0 to 3, then none past them
static const float3 kCascadeDebugTints[5] = {
  float3(1.0, 0.3, 0.3), float3(0.3, 1.0, 0.3), float3(0.4, 0.4, 1.0), float3(1.0, 1.0, 0.3), float3(1.0, 1.0, 1.0)
};

// The share of an even sky that the specular lobe reflects toward the camera: the first bounce, from
// renderer/env_brdf.h's table, and the light that bounces between microfacets after it
struct EnvBrdf {
  float3 FssEss;
  float3 FmsEms;
};

// Fdez-Aguera 2019, "A Multiple-Scattering Microfacet Model for Real-Time Image-based Lighting", Listing 2. F0 stands
// where the listing has a roughness-dependent Fresnel: the table's B already holds Fresnel's rise at grazing angles.
EnvBrdf envBrdf(float3 F0, float perceptualRoughness, float NdotV) {
  float2 f_ab = sampleTexture(pc.globalUniforms->envBrdfTexture, float2(NdotV, perceptualRoughness)).rg;
  EnvBrdf result;
  result.FssEss = F0 * f_ab.x + f_ab.y;
  float Ess = f_ab.x + f_ab.y;
  float Ems = 1.0 - Ess;
  float3 Favg = F0 + (1.0 - F0) / 21.0;
  float3 Fms = result.FssEss * Favg / (1.0 - (1.0 - Ess) * Favg);
  result.FmsEms = Fms * Ems;
  return result;
}

// Sky above the horizon, ground below (+Z up): exactly the diffuse light a two-color sky gives a surface facing dir
float3 hemisphere(float3 dir) {
  return lerp(pc.globalUniforms->groundRadiance, pc.globalUniforms->skyRadiance, saturate(dir.z * 0.5 + 0.5));
}

// Radiance toward the camera, in nits: the glTF BRDF (GGX, height-correlated Smith, Schlick) under every light, plus the
// occluded hemisphere ambient and the emission. N is the shading normal; shadow lookups move along geometryNormal.
float3 litRadiance(float3 base, float metallic, float perceptualRoughness, float ao, float3 emissive, float3 N,
                   float3 geometryNormal, float3 worldPos) {
  GlobalUniforms* globals = pc.globalUniforms;
  // Toward the camera; with an orthographic eye (w = 0), the same at every pixel
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
    // Toward the light; for a directional light (w = 0), the same at every pixel and of length 1
    float3 toLight = light.position.xyz - worldPos * light.position.w;
    // At least (1 cm)^2: a light touching the surface counts as 1 cm away, which keeps 1 / d2 finite
    float d2 = max(dot(toLight, toLight), 1e-4);
    float3 L = toLight * rsqrt(d2);
    float NdotL = dot(N, L);
    // Behind the surface. Skipping it also keeps H finite when L = -V: a NaN times a zero NdotL is still NaN.
    if (NdotL <= 0.0) {
      continue;
    }
    // The illuminance (lux) on a surface facing the light, after KHR_lights_punctual's range fade and cone
    float r = d2 * light.invRangeSq; // (distance / range)^2
    float cone = saturate(dot(light.direction, -L) * light.spotScale + light.spotOffset);
    float3 E = light.intensity * (saturate(1.0 - r * r) * cone * cone / d2);
    if (i == globals->shadowLight) {
      E *= shadowFactor(globals->shadows, worldPos, geometryNormal);
    }

    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (kPi * d * d);
    float Vis = 0.5 / (NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2) + NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2));
    float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    radiance += ((1.0 - F) * diffuseColor / kPi + F * D * Vis) * E * NdotL;
  }

  // Ambient: specular along R, which bends toward N as roughness grows
  float3 R = normalize(lerp(reflect(-V, N), N, a));
  // The bounces past the first leave in every direction, as the diffuse does, so both take the light around N; the
  // diffuse gets only the light the specular doesn't reflect
  EnvBrdf specular = envBrdf(F0, roughness, NdotV);
  float3 diffuse = diffuseColor * (1.0 - (specular.FssEss + specular.FmsEms));
  radiance += (specular.FssEss * hemisphere(R) + (specular.FmsEms + diffuse) * hemisphere(N)) * ao;
  return radiance + emissive;
}

float4 shade(Material material, float4 base, VSOutput vertIn, bool isFrontFace) {
  float4 metallicRoughness = sampleTexture(material.metallicRoughnessTexture, vertIn.uv);
  float metallic = material.metallic * metallicRoughness.b;
  float roughness = material.roughness * metallicRoughness.g;
  float ao = 1.0 - material.occlusionStrength * (1.0 - sampleTexture(material.occlusionTexture, vertIn.uv).r);
  float3 emissive = material.emissive * sampleTexture(material.emissiveTexture, vertIn.uv).rgb;

  // As mikktspace.h prescribes: B from N and T as interpolated, and only the final normal normalized
  float3 N = vertIn.normal;
  float3 T = vertIn.tangent.xyz;
  float3 B = vertIn.tangent.w * cross(N, T);
  // Back faces (double-sided only) reverse every axis. B is computed first, since cross(-N, -T) is cross(N, T).
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

  float3 radiance = litRadiance(base.rgb, metallic, roughness, ao, emissive, N, geometryNormal, vertIn.worldPos);
  GlobalUniforms* globals = pc.globalUniforms;
  if ((DebugView)globals->debugView == DebugView.Cascades && globals->shadowLight != kNoShadowLight) {
    radiance *= kCascadeDebugTints[cascadeIndex(globals->shadows, vertIn.worldPos)];
  }
  // Exposed here, so the HDR target's fp16 holds it; the clamp catches the rest, such as a mirror under a bright light
  return float4(min(radiance * globals->exposure, float3(64000.0)), base.a);
}

// Opaque and blend materials
[shader("fragment")]
float4 fragMain(VSOutput vertIn, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  Material material = pc.materials[vertIn.materialIndex];
  return shade(material, baseColor(material, vertIn), vertIn, isFrontFace);
}

// Mask materials only, so the others keep early depth testing
[shader("fragment")]
float4 fragMainMasked(VSOutput vertIn, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  Material material = pc.materials[vertIn.materialIndex];
  float4 base = baseColor(material, vertIn);
  if (base.a < material.alphaCutoff) {
    discard;
  }
  return shade(material, base, vertIn, isFrontFace);
}

// Masked casters in the shadow pass: only the alpha test
[shader("fragment")]
void fragShadowMasked(VSOutput vertIn) {
  Material material = pc.materials[vertIn.materialIndex];
  if (baseColor(material, vertIn).a < material.alphaCutoff) {
    discard;
  }
}
