#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "components/camera_component.h"
#include "core/math/transform.h"
#include "renderer/shadow_cascades.h"

#include <array>

namespace ne::test {

namespace {

constexpr float kAspectRatio = 1.2f;

// BasicApp's camera and sun: the camera 4 m behind the origin looking along +X, the sun pitched down 50 degrees and
// turned 30 degrees right
const CameraComponent kCamera{.fov = math::radians(45.0f), .nearClip = 0.1f, .farClip = 100.0f};
const Mat4 kCameraWorld = Transform(Vec3(-4.0f, 0.0f, 0.0f)).toMatrix();
const Mat4 kSunWorld = Transform(Vec3::Zero, Quat::fromEuler(math::radians(Vec3(0.0f, 50.0f, -30.0f)))).toMatrix();

// The eight corners of the camera's view between two view depths, in world space
std::array<Vec3, 8> sliceCorners(const CameraComponent& iCamera, const Mat4& iWorld, float iNear, float iFar) {
  const Vec3 forward = iWorld[0].xyz().getSafeNormal();
  const Vec3 left = iWorld[1].xyz().getSafeNormal();
  const Vec3 up = iWorld[2].xyz().getSafeNormal();
  const bool isOrthographic = iCamera.projectionType == CameraComponent::ProjectionType::Orthographic;
  std::array<Vec3, 8> corners;
  size_t count = 0;
  for (float depth : {iNear, iFar}) {
    const float halfHeight = isOrthographic ? iCamera.orthoSize * 0.5f : depth * math::tan(iCamera.fov * 0.5f);
    const float halfWidth = halfHeight * kAspectRatio;
    for (float side : {-1.0f, 1.0f}) {
      for (float height : {-1.0f, 1.0f}) {
        corners[count++] = iWorld[3].xyz() + forward * depth + left * (side * halfWidth) + up * (height * halfHeight);
      }
    }
  }
  return corners;
}

// Whether every slice's corners land inside its cascade, at least as far in from the tile's edges as the border (less
// the half texel snapping may move it), and between its near and far planes
bool slicesFitTheirCascades(const CameraComponent& iCamera, const Mat4& iWorld, const ShadowCascades& iCascades,
                            const ShadowCascades::Config& iConfig) {
  const float halfResolution = static_cast<float>(iConfig.resolution) * 0.5f;
  const float limit = 1.0f - (static_cast<float>(ShadowCascades::kBorderTexels) - 0.5f) / halfResolution + 1e-5f;
  float sliceNear = iCamera.nearClip;
  for (uint32_t i = 0; i < ShadowCascades::kCount; ++i) {
    for (const Vec3& corner : sliceCorners(iCamera, iWorld, sliceNear, iCascades.splitDepths[i])) {
      const Vec4 clip = iCascades.viewProj[i] * Vec4(corner, 1.0f);
      if (math::abs(clip.x) > limit || math::abs(clip.y) > limit || clip.z <= 0.0f || clip.z >= 1.0f) {
        return false;
      }
    }
    sliceNear = iCascades.splitDepths[i];
  }
  return true;
}

// Where a world point lands within its texel of a cascade, as a fraction of a texel along x and y
Vec2 spotInTexel(const ShadowCascades& iCascades, uint32_t iCascade, const Vec3& iPoint, float iResolution) {
  const Vec4 clip = iCascades.viewProj[iCascade] * Vec4(iPoint, 1.0f);
  const Vec2 texel((clip.x + 1.0f) * iResolution * 0.5f, (clip.y + 1.0f) * iResolution * 0.5f);
  return Vec2(texel.x - math::round(texel.x), texel.y - math::round(texel.y));
}

// The distance between two spots within a texel, the shorter way around
float texelSpotDistance(const Vec2& iA, const Vec2& iB) {
  const float dx = (iA.x - iB.x) - math::round(iA.x - iB.x);
  const float dy = (iA.y - iB.y) - math::round(iA.y - iB.y);
  return math::max(math::abs(dx), math::abs(dy));
}

} // namespace

// For BasicApp's camera and the 60 m distance: lambda 0.8 of n (f / n)^(i / 4) plus 0.2 of the even split
NE_TEST_CASE("shadows", "Cascade Splits Rise To The Shadow Distance") {
  const ShadowCascades::Config config;
  const std::optional<ShadowCascades> cascades = ShadowCascades::compute(kCamera, kCameraWorld, kAspectRatio, kSunWorld, config);
  NE_TEST_ASSERT(cascades.has_value(), "The camera sees 60 m of shadows.");
  const std::array expected{3.411f, 7.970f, 18.703f, 60.0f};
  for (uint32_t i = 0; i < ShadowCascades::kCount; ++i) {
    NE_TEST_ASSERT(math::equals(cascades->splitDepths[i], expected[i], 1e-3f), "Cascade {} ends at {} m, got {}.", i, expected[i],
                   cascades->splitDepths[i]);
  }

  CameraComponent nearCamera = kCamera;
  nearCamera.farClip = 30.0f;
  const std::optional<ShadowCascades> shortened =
      ShadowCascades::compute(nearCamera, kCameraWorld, kAspectRatio, kSunWorld, config);
  NE_TEST_ASSERT(shortened && shortened->splitDepths[3] == 30.0f, "A far plane nearer than the distance ends the last cascade.");

  NE_TEST_ASSERT(!ShadowCascades::compute(kCamera, kCameraWorld, kAspectRatio, kSunWorld, {.distance = 0.05f}),
                 "Shadows that end before the near plane leave nothing to shadow.");
}

// The slices' far corners lie on their bounding spheres, so this fails if a sphere is too small, the light's eye too
// close, or the snapping moves a tile by more than half a texel
NE_TEST_CASE("shadows", "Every Slice Lands Inside Its Cascade") {
  const ShadowCascades::Config config;
  const Mat4 turnedCamera =
      Transform(Vec3(3.0f, -2.0f, 1.5f), Quat::fromEuler(math::radians(Vec3(10.0f, 25.0f, 140.0f)))).toMatrix();
  for (const Mat4& cameraWorld : {kCameraWorld, turnedCamera}) {
    const std::optional<ShadowCascades> perspective =
        ShadowCascades::compute(kCamera, cameraWorld, kAspectRatio, kSunWorld, config);
    NE_TEST_ASSERT(perspective && slicesFitTheirCascades(kCamera, cameraWorld, *perspective, config),
                   "Every corner of every perspective slice lands inside its cascade's tile and depth range.");

    const CameraComponent orthographic{
        .projectionType = CameraComponent::ProjectionType::Orthographic, .orthoSize = 10.0f, .nearClip = 0.1f, .farClip = 100.0f};
    const std::optional<ShadowCascades> boxes =
        ShadowCascades::compute(orthographic, cameraWorld, kAspectRatio, kSunWorld, config);
    NE_TEST_ASSERT(boxes && slicesFitTheirCascades(orthographic, cameraWorld, *boxes, config),
                   "Every corner of every orthographic slice lands inside its cascade's tile and depth range.");
  }
}

// Shadow edges shimmer when the shadow map's texels slide over the world from one frame to the next. Snapping keeps each
// world point at the same spot within its texel however the camera moves, and turning the camera leaves the texel size
// alone.
NE_TEST_CASE("shadows", "Cascades Keep World Points On Their Texels As The Camera Moves") {
  const ShadowCascades::Config config;
  const float resolution = static_cast<float>(config.resolution);
  const std::optional<ShadowCascades> start = ShadowCascades::compute(kCamera, kCameraWorld, kAspectRatio, kSunWorld, config);
  NE_TEST_ASSERT(start.has_value(), "The camera sees 60 m of shadows.");

  const Vec3 point(-1.5f, 0.3f, -0.2f); // In cascade 0, about 2.5 m in front of the camera
  const std::array moves{Vec3(0.0013f, 0.0f, 0.0f), Vec3(0.0f, 0.0007f, 0.0021f), Vec3(-0.31f, 0.17f, 0.05f),
                         Vec3(1.7f, -2.3f, 0.4f)};
  for (const Vec3& move : moves) {
    const Mat4 movedCamera = Transform(Vec3(-4.0f, 0.0f, 0.0f) + move).toMatrix();
    const std::optional<ShadowCascades> moved = ShadowCascades::compute(kCamera, movedCamera, kAspectRatio, kSunWorld, config);
    NE_TEST_ASSERT(moved.has_value(), "The moved camera sees 60 m of shadows.");
    for (uint32_t i = 0; i < ShadowCascades::kCount; ++i) {
      const float distance =
          texelSpotDistance(spotInTexel(*start, i, point, resolution), spotInTexel(*moved, i, point, resolution));
      NE_TEST_ASSERT(distance < 2e-3f,
                     "Moving the camera by {} keeps the point's spot in cascade {}'s texel, but it moved {} texels.",
                     move.toString(), i, distance);
    }
  }

  const Mat4 turnedCamera =
      Transform(Vec3(-4.0f, 0.0f, 0.0f), Quat::fromEuler(math::radians(Vec3(5.0f, 10.0f, 30.0f)))).toMatrix();
  const std::optional<ShadowCascades> turned = ShadowCascades::compute(kCamera, turnedCamera, kAspectRatio, kSunWorld, config);
  NE_TEST_ASSERT(turned && turned->texelSizes == start->texelSizes, "Turning the camera keeps every cascade's texel size.");
}

// Reverse-Z: depth 1 is next to the light and 0 far from it. Each cascade looks along the light's +X, so moving along
// the light's +Y or +Z leaves the depth alone.
NE_TEST_CASE("shadows", "A Cascade's Depth Falls Along The Light's Direction") {
  const std::optional<ShadowCascades> cascades =
      ShadowCascades::compute(kCamera, kCameraWorld, kAspectRatio, kSunWorld, ShadowCascades::Config{});
  NE_TEST_ASSERT(cascades.has_value(), "The camera sees 60 m of shadows.");
  const Vec3 point(-1.5f, 0.3f, -0.2f);
  for (uint32_t i = 0; i < ShadowCascades::kCount; ++i) {
    const Mat4& viewProj = cascades->viewProj[i];
    const float depth = (viewProj * Vec4(point, 1.0f)).z;
    const float farther = (viewProj * Vec4(point + kSunWorld[0].xyz(), 1.0f)).z;
    const float aside = (viewProj * Vec4(point + kSunWorld[1].xyz() + kSunWorld[2].xyz(), 1.0f)).z;
    NE_TEST_ASSERT(farther < depth, "In cascade {}, a point 1 m farther along the light has a smaller depth.", i);
    NE_TEST_ASSERT(math::equals(aside, depth, 1e-5f), "In cascade {}, moving across the light keeps the depth.", i);
  }
}

} // namespace ne::test

#endif
