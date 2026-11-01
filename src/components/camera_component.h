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

  // Exposure, set as on a real camera. The defaults follow the "sunny 16" rule (EV100 15), for a 100,000 lux sun.
  float aperture{16.0f};             // f-number
  float shutterSpeed{1.0f / 125.0f}; // Seconds
  float iso{100.0f};

  // How little light the settings let in, in stops at ISO 100: each step up halves it. EV100 = log2(N^2 / t * 100 / S),
  // from Lagarde & de Rousiers, "Moving Frostbite to PBR".
  float getEv100() const { return math::log2(aperture * aperture / shutterSpeed * 100.0f / iso); }

  // Scales luminance (nits) so that 1.2 x 2^EV100, the luminance that just fills the sensor, becomes 1.0. 2^EV100 is
  // N^2 / t * 100 / S, so this skips getEv100()'s log2 and the exp2 that would undo it. The scene shader applies it,
  // so the HDR target holds exposed values.
  float getExposure() const { return 1.0f / (1.2f * (aperture * aperture / shutterSpeed * 100.0f / iso)); }

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
