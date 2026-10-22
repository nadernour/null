#language slang latest

// The bindless set, declared as in base_shader.slang
[vk::binding(0, 0)]
SamplerState gSamplers[];

[vk::binding(1, 0)]
Texture2D gTextures[];

struct TonemapPushConstants {
  uint hdrTexture;
  uint encodeSrgb;
};

[vk::push_constant]
TonemapPushConstants pc;

// Khronos PBR Neutral (reference implementation); linear in, linear out
float3 pbrNeutral(float3 color) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;
  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;
  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) {
    return color;
  }
  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  color *= newPeak / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

float linearToSrgb(float value) {
  return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

// One triangle covering the screen: (-1, -1), (3, -1), (-1, 3)
[shader("vertex")]
float4 vertMain(uint vertexId : SV_VertexID) : SV_Position {
  float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
  return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

// The HDR image and the attachment are the same size, so each pixel loads its own texel: no sampler, no UVs
[shader("fragment")]
float4 fragMain(float4 position : SV_Position) : SV_TARGET {
  float3 color = pbrNeutral(gTextures[pc.hdrTexture].Load(int3(int2(position.xy), 0)).rgb);
  if (pc.encodeSrgb != 0) {
    color = float3(linearToSrgb(color.r), linearToSrgb(color.g), linearToSrgb(color.b));
  }
  return float4(color, 1.0);
}
