#include "renderer/shadow_cascades.h"
#include "components/camera_component.h"
#include "renderer/gpu_types.h"

namespace ne {

namespace {

// The light views' near plane, this far in front of their eye. The eye backs off from the tile's box by as much, so the
// plane lies on the box's near face.
constexpr float kLightNearClip = 0.01f;

// The view depth of a slice's bounding sphere center, and its radius
struct SliceSphere {
  float centerDepth;
  float radius;
};

SliceSphere boundSlice(const CameraComponent& iCamera, float iAspectRatio, float iNear, float iFar) {
  if (iCamera.projectionType == CameraComponent::ProjectionType::Orthographic) {
    // A box: centered between its ends, out to a far corner
    const float halfHeight = iCamera.orthoSize * 0.5f;
    const float halfWidth = halfHeight * iAspectRatio;
    const float halfDepth = (iFar - iNear) * 0.5f;
    return SliceSphere{.centerDepth = iNear + halfDepth,
                       .radius = math::sqrt(halfWidth * halfWidth + halfHeight * halfHeight + halfDepth * halfDepth)};
  }

  // Every corner at depth d lies d * k from the view axis. Both rings of corners are equally far from the axis point at
  // (near + far)(1 + k^2) / 2; when that's past the far ring, the far ring's own center is closer, and its sphere already
  // holds the near ring. Either way the far corners are on the sphere.
  const float tanHalfFov = math::tan(iCamera.fov * 0.5f);
  const float kSq = tanHalfFov * tanHalfFov * (1.0f + iAspectRatio * iAspectRatio);
  const float centerDepth = math::min((iNear + iFar) * (1.0f + kSq) * 0.5f, iFar);
  const float toFar = iFar - centerDepth;
  return SliceSphere{.centerDepth = centerDepth, .radius = math::sqrt(toFar * toFar + iFar * iFar * kSq)};
}

} // namespace

std::optional<ShadowCascades> ShadowCascades::compute(const CameraComponent& iCamera, const Mat4& iCameraWorld,
                                                      float iAspectRatio, const Mat4& iLightWorld, const Config& iConfig) {
  static_assert(kTileSize > 2 * kBorderTexels, "The border must leave room for the slice");
  const float nearDepth = iCamera.nearClip;
  const float farDepth = iCamera.farClip > 0.0f ? math::min(iCamera.farClip, iConfig.distance) : iConfig.distance;
  if (farDepth <= nearDepth) {
    return std::nullopt;
  }

  const Vec3 cameraPosition = iCameraWorld[3].xyz();
  const Vec3 cameraForward = iCameraWorld[0].xyz().getSafeNormal();
  const Vec3 lightForward = iLightWorld[0].xyz().getSafeNormal();
  const float resolution = static_cast<float>(kTileSize);
  const float texelsPerClipUnit = resolution * 0.5f; // Clip space spans 2 units across the tile

  ShadowCascades cascades;
  cascades.depthPlane = Vec4(cameraForward, -cameraForward.dot(cameraPosition));
  float sliceNear = nearDepth;
  for (uint32_t i = 0; i < kCount; ++i) {
    // The practical split scheme: perspective shrinks things with distance, which logarithmic splits follow, and the
    // even share keeps the first slice from shrinking to a sliver
    const float t = static_cast<float>(i + 1) / static_cast<float>(kCount);
    const float logSplit = nearDepth * math::pow(farDepth / nearDepth, t);
    const float evenSplit = nearDepth + (farDepth - nearDepth) * t;
    const float sliceFar = i + 1 == kCount ? farDepth : math::lerp(evenSplit, logSplit, iConfig.splitLambda);

    // The sphere depends only on the projection and the split depths, never on the camera's pose, so the texel size
    // stays the same while the camera moves or turns
    const SliceSphere sphere = boundSlice(iCamera, iAspectRatio, sliceNear, sliceFar);
    const Vec3 center = cameraPosition + cameraForward * sphere.centerDepth;
    const float texelSize = 2.0f * sphere.radius / (resolution - 2.0f * static_cast<float>(kBorderTexels));
    const float halfSize = texelSize * resolution * 0.5f;

    // An orthographic camera with the light's rotation, which keeps the texel grid from turning with the camera. Its
    // near and far planes lie on the tile's box. Casters in front of the near plane, however far toward the light, still
    // cast: the shadow pipelines clamp their depth onto the plane instead of clipping them.
    Mat4 lightWorld = iLightWorld;
    lightWorld[3] = Vec4(center - lightForward * (halfSize + kLightNearClip), 1.0f);
    const CameraComponent lightCamera{
        .projectionType = CameraComponent::ProjectionType::Orthographic,
        .orthoSize = 2.0f * halfSize,
        .nearClip = kLightNearClip,
        .farClip = kLightNearClip + 2.0f * halfSize,
        .primary = false,
    };
    Mat4 viewProj = lightCamera.getViewProjectionMatrix(lightWorld, 1.0f);

    // Move the projection by less than a texel so the world origin lands on a texel corner. The light's rotation and the
    // texel size don't change, so every world point then keeps its spot within its texel, and edges don't crawl.
    const float originX = viewProj[3].x * texelsPerClipUnit;
    const float originY = viewProj[3].y * texelsPerClipUnit;
    viewProj[3].x += (math::round(originX) - originX) / texelsPerClipUnit;
    viewProj[3].y += (math::round(originY) - originY) / texelsPerClipUnit;

    cascades.viewProj[i] = viewProj;
    cascades.splitDepths[i] = sliceFar;
    cascades.texelSizes[i] = texelSize;
    sliceNear = sliceFar;
  }
  return cascades;
}

VkRect2D ShadowCascades::getTile(uint32_t iCascade) {
  return VkRect2D{
      .offset = {static_cast<int32_t>((iCascade % 2) * kTileSize), static_cast<int32_t>((iCascade / 2) * kTileSize)},
      .extent = {kTileSize, kTileSize},
  };
}

gpu::ShadowData ShadowCascades::makeShadowData(uint32_t iAtlasTexture) const {
  const float atlasSize = static_cast<float>(kAtlasSize);
  gpu::ShadowData data{.depthPlane = depthPlane, .atlasTexture = iAtlasTexture, .texelSize = 1.0f / atlasSize};

  // Clip space runs from -1 to 1 across a tile, so half a tile's share of the atlas scales it, and the tile's center
  // offsets it
  const float scale = 0.5f * static_cast<float>(kTileSize) / atlasSize;
  for (uint32_t i = 0; i < kCount; ++i) {
    const VkRect2D tile = getTile(i);
    Mat4 toAtlas(1.0f);
    toAtlas[0].x = scale;
    toAtlas[1].y = scale;
    toAtlas[3].x = static_cast<float>(tile.offset.x) / atlasSize + scale;
    toAtlas[3].y = static_cast<float>(tile.offset.y) / atlasSize + scale;
    data.cascades[i] = toAtlas * viewProj[i];
    data.splitDepths[i] = splitDepths[i];
    data.normalOffsets[i] = texelSizes[i] * kNormalOffsetTexels;
  }
  return data;
}

} // namespace ne
