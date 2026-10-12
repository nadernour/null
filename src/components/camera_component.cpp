#include "components/camera_component.h"
#include "core/assert.h"

namespace ne {

Mat4 CameraComponent::getProjectionMatrix(float iAspectRatio) const {
  NE_ASSERT(iAspectRatio > math::kSmallNumber);
  NE_ASSERT(nearClip > math::kSmallNumber);

  Mat4 projection(0.0f);
  if (projectionType == ProjectionType::Perspective) {
    const float tanHalfFovy = math::tan(fov * 0.5f);

    projection[0][0] = 1.0f / (iAspectRatio * tanHalfFovy);
    projection[1][1] = 1.0f / tanHalfFovy; // View +Y (down) already matches Vulkan NDC +Y (down)
    projection[2][3] = 1.0f;               // Clip w = view depth (+Z forward)

    // Floating-point Reverse-Z (Near -> 1.0, Far -> 0.0)
    if (farClip <= 0.0f) {
      projection[2][2] = 0.0f;
      projection[3][2] = nearClip;
    } else {
      NE_ASSERT(farClip > nearClip);
      projection[2][2] = -nearClip / (farClip - nearClip);
      projection[3][2] = (farClip * nearClip) / (farClip - nearClip);
    }
  } else {
    NE_ASSERT(orthoSize > math::kSmallNumber);
    NE_ASSERT(farClip > nearClip);
    const float halfHeight = orthoSize * 0.5f;
    const float halfWidth = halfHeight * iAspectRatio;

    projection[0][0] = 1.0f / halfWidth;
    projection[1][1] = 1.0f / halfHeight; // View +Y (down) already matches Vulkan NDC +Y (down)
    projection[3][3] = 1.0f;

    // Floating-point Reverse-Z (Near -> 1.0, Far -> 0.0)
    projection[2][2] = -1.0f / (farClip - nearClip);
    projection[3][2] = farClip / (farClip - nearClip);
  }
  return projection;
}

Mat4 CameraComponent::getViewMatrix(const Mat4& iWorldMatrix) const {
  const Vec3 eye(iWorldMatrix[3].x, iWorldMatrix[3].y, iWorldMatrix[3].z);
  const Vec3 forward = Vec3(iWorldMatrix[0].x, iWorldMatrix[0].y, iWorldMatrix[0].z).getSafeNormal();
  const Vec3 right = -Vec3(iWorldMatrix[1].x, iWorldMatrix[1].y, iWorldMatrix[1].z).getSafeNormal();
  const Vec3 down = -Vec3(iWorldMatrix[2].x, iWorldMatrix[2].y, iWorldMatrix[2].z).getSafeNormal();

  Mat4 view{1.0f};
  view[0][0] = right.x;   view[1][0] = right.y;   view[2][0] = right.z;   view[3][0] = -right.dot(eye);
  view[0][1] = down.x;    view[1][1] = down.y;    view[2][1] = down.z;    view[3][1] = -down.dot(eye);
  view[0][2] = forward.x; view[1][2] = forward.y; view[2][2] = forward.z; view[3][2] = -forward.dot(eye);
  return view;
}

} // namespace ne
