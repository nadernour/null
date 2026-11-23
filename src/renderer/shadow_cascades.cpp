#include "renderer/shadow_cascades.h"
#include "components/camera_component.h"
#include "renderer/gpu_types.h"

namespace ne {

namespace {

// Each light view's eye backs off this far from its box, so the near plane lies on the box's near face
constexpr float kLightNearClip = 0.01f;

// A slice's bounding sphere, centered on the view axis
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

  // A corner at depth d lies d * sqrt(kSq) from the axis. The axis point (near + far)(1 + kSq) / 2 is equally far from
  // both rings of corners; past the far ring, the far ring's own sphere is smaller and already holds the near ring.
  const float tanHalfFov = math::tan(iCamera.fov * 0.5f);
  const float kSq = tanHalfFov * tanHalfFov * (1.0f + iAspectRatio * iAspectRatio);
  const float centerDepth = math::min((iNear + iFar) * (1.0f + kSq) * 0.5f, iFar);
  const float toFar = iFar - centerDepth;
  return SliceSphere{.centerDepth = centerDepth, .radius = math::sqrt(toFar * toFar + iFar * iFar * kSq)};
}

} // namespace

std::optional<ShadowCascades> ShadowCascades::compute(const CameraComponent& iCamera, const Mat4& iCameraWorld,
                                                      float iAspectRatio, const Mat4& iLightWorld) {
  static_assert(kTileSize > 2 * kBorderTexels, "The border must leave room for the slice");
  const float nearDepth = iCamera.nearClip;
  const float farDepth = iCamera.farClip > 0.0f ? math::min(iCamera.farClip, iCamera.shadowDistance) : iCamera.shadowDistance;
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
    // The practical split scheme of Parallel-Split Shadow Maps (Zhang et al. 2006)
    const float t = static_cast<float>(i + 1) / static_cast<float>(kCount);
    const float logSplit = nearDepth * math::pow(farDepth / nearDepth, t);
    const float evenSplit = nearDepth + (farDepth - nearDepth) * t;
    const float sliceFar = i + 1 == kCount ? farDepth : math::lerp(evenSplit, logSplit, kSplitLambda);

    // The sphere ignores the camera's pose, so the texel size holds steady as the camera moves or turns
    const SliceSphere sphere = boundSlice(iCamera, iAspectRatio, sliceNear, sliceFar);
    const Vec3 center = cameraPosition + cameraForward * sphere.centerDepth;
    const float texelSize = 2.0f * sphere.radius / (resolution - 2.0f * static_cast<float>(kBorderTexels));
    const float halfSize = texelSize * resolution * 0.5f;

    // The light's rotation, so the texel grid doesn't turn with the camera
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

    // Snap the world origin to a texel corner, so world points keep their spot within a texel and edges don't crawl
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

  // Maps clip space's [-1, 1] across a tile onto the tile's part of the atlas
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
