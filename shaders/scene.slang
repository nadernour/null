#language slang latest

[vk::binding(0, 0)]
SamplerState gSamplers[];

// The same samplers as gSamplers, for the one that compares depths: in SPIR-V both are plain samplers, and Vulkan allows
// two declarations of one binding. Slang warns about the overlap (39001), so the warning is off for this declaration
// only; any other overlap still warns.
#pragma warning(push)
#pragma warning(disable : 39001)
[vk::binding(0, 0)]
SamplerComparisonState gShadowSamplers[];
#pragma warning(pop)

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

// The cascades of the directional light that casts shadows; cascade i is drawn into tile i of the atlas
struct ShadowData {
  float4x4 cascades[4]; // World position -> (atlas u, atlas v, depth)
  float4 tileBounds[4]; // Each cascade's tile as (min u, min v, max u, max v), half a texel in from its edges
  float4 depthPlane;    // The camera's view depth of a world point p: dot(depthPlane.xyz, p) + depthPlane.w
  float4 splitDepths;   // The view depth where each cascade ends
  float4 normalOffsets; // How far each cascade's lookups move along the normal, in meters
  uint atlasTexture;    // Bindless index (low 16 bits) and sampler index (high 16 bits)
  float texelSize;      // One atlas texel, in atlas UV
};

struct GlobalUniforms {
  float4x4 viewProj;
  Light* lights;
  ShadowData* shadows; // Read only when shadowLight names a light
  float4 eye; // Perspective camera: (position, 1). Orthographic: (backward direction, 0)
  float3 skyRadiance;
  float exposure;
  float3 groundRadiance;
  uint lightCount;
  uint debugView;
  uint shadowLight; // The light whose shadows were drawn, or kNoShadowLight
};

// Mirrors gpu::kNoShadowLight. The shader never compares the shadows pointer with null: that turns the address into a
// 64-bit integer, which needs the shaderInt64 feature.
static const uint kNoShadowLight = 0xFFFFFFFF;

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

// Factor x texture x vertex color x instance tint; mask and blend materials read its alpha
float4 baseColor(Material material, VSOutput vertIn) {
  return material.baseColor * sampleTexture(material.baseColorTexture, vertIn.uv) * vertIn.color;
}

// The cascade that covers a world point: the number of cascades that end before its view depth, so 4 is past them all
uint cascadeIndex(ShadowData* shadows, float3 worldPos) {
  float depth = dot(shadows->depthPlane.xyz, worldPos) + shadows->depthPlane.w;
  return uint(dot(select(depth > shadows->splitDepths, float4(1.0), float4(0.0)), float4(1.0)));
}

// How much of the shadowed light reaches a surface point: 1 in light, 0 in shadow, from nine depth comparisons around
// it. N is the geometry normal: the lookup moves along it, off the surface, so the surface's own depth in the shadow map
// can't shadow it.
float shadowFactor(ShadowData* shadows, float3 worldPos, float3 N) {
  uint cascade = cascadeIndex(shadows, worldPos);
  if (cascade >= 4) {
    return 1.0; // Past the shadow distance
  }
  float3 position = worldPos + N * shadows->normalOffsets[cascade];
  float3 coord = mul(shadows->cascades[cascade], float4(position, 1.0)).xyz;
  float4 bounds = shadows->tileBounds[cascade];
  uint textureIndex = shadows->atlasTexture & 0xFFFF;
  uint samplerIndex = shadows->atlasTexture >> 16;
  // Each tap compares bilinearly, against the four texels around it; clamped to the tile, none reads another cascade
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float2 uv = clamp(coord.xy + float2(x, y) * shadows->texelSize, bounds.xy, bounds.zw);
      lit += gTextures[textureIndex].SampleCmpLevelZero(gShadowSamplers[samplerIndex], uv, coord.z);
    }
  }
  return lit / 9.0;
}

// The Cascades view's tints: red, green, blue, and yellow for cascades 0 to 3, then none past them
static const float3 kCascadeTints[5] = {
  float3(1.0, 0.3, 0.3), float3(0.3, 1.0, 0.3), float3(0.4, 0.4, 1.0), float3(1.0, 1.0, 0.3), float3(1.0, 1.0, 1.0)
};

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
// camera, in nits. N is the shading normal and geometryNormal the vertex normal, which shadow lookups move along.
float3 litRadiance(float3 base, float metallic, float perceptualRoughness, float ao, float3 emissive, float3 N,
                   float3 geometryNormal, float3 worldPos) {
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
    // Only the light that casts shadows has a shadow map; the loop knows it by its index
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

  float3 radiance = litRadiance(base.rgb, metallic, roughness, ao, emissive, N, geometryNormal, vertIn.worldPos);
  GlobalUniforms* globals = pc.globalUniforms;
  if ((DebugView)globals->debugView == DebugView.Cascades && globals->shadowLight != kNoShadowLight) {
    radiance *= kCascadeTints[cascadeIndex(globals->shadows, vertIn.worldPos)];
  }
  // Exposed here, so the HDR target's fp16 holds it; the clamp catches the rest, such as a mirror under a bright light
  return float4(min(radiance * globals->exposure, float3(64000.0)), base.a);
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

// Masked casters in the shadow pass, which has no color to shade: only the alpha test. Opaque casters draw there without
// a fragment shader.
[shader("fragment")]
void fragShadowMasked(VSOutput vertIn) {
  Material material = pc.materials[vertIn.materialIndex];
  if (baseColor(material, vertIn).a < material.alphaCutoff) {
    discard;
  }
}
