#pragma once

#include "core/math/math.h"

namespace ne {

struct CameraComponent {
  enum class ProjectionType { Perspective, Orthographic };

  ProjectionType projectionType{ProjectionType::Perspective};
  float fov{math::radians(45.0f)}; // Perspective: vertical field of view in radians
  float orthoSize{5.0f};           // Orthographic: vertical view size
  float nearClip{0.1f};
  float farClip{1000.0f};          // Perspective: <= 0 selects an infinite far plane
  bool primary{true};              // Renders to the window; at most one camera may be primary

  // The aspect ratio (width / height) comes from the render target, not the camera
  Mat4 getProjectionMatrix(float iAspectRatio) const;

  // View space is the ROS camera optical frame (+X right, +Y down, +Z forward), which matches Vulkan NDC,
  // so the view matrix is a pure rotation + translation. World scale is ignored.
  Mat4 getViewMatrix(const Mat4& iWorldMatrix) const;

  Mat4 getViewProjectionMatrix(const Mat4& iWorldMatrix, float iAspectRatio) const {
    return getProjectionMatrix(iAspectRatio) * getViewMatrix(iWorldMatrix);
  }
};

} // namespace ne
