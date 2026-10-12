#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "components/camera_component.h"
#include "core/math/transform.h"

namespace ne::test {

namespace {

constexpr float kAspectRatio = 16.0f / 9.0f;

float clipDepth(const Mat4& iProjection, float iViewDepth) {
  Vec4 clip = iProjection * Vec4(0.0f, 0.0f, iViewDepth, 1.0f);
  return clip.z / clip.w;
}

} // namespace

NE_TEST_CASE("camera", "CameraComponent Default Perspective Projection") {
  CameraComponent camera;
  NE_TEST_ASSERT(camera.projectionType == CameraComponent::ProjectionType::Perspective, "Default projection type must be Perspective.");

  Mat4 projection = camera.getProjectionMatrix(kAspectRatio);
  NE_TEST_ASSERT((projection * projection.inversed()).equals(Mat4::Identity, 1e-3f), "Default projection must be invertible.");
  NE_TEST_ASSERT(math::equals(clipDepth(projection, camera.nearClip), 1.0f, 1e-4f), "Default near plane must map to depth 1.0.");
}

NE_TEST_CASE("camera", "CameraComponent Perspective Reverse-Z Projection") {
  CameraComponent camera{.fov = math::radians(45.0f), .nearClip = 0.1f, .farClip = 1000.0f};
  Mat4 projection = camera.getProjectionMatrix(kAspectRatio);

  NE_TEST_ASSERT(math::equals(clipDepth(projection, 0.1f), 1.0f, 1e-4f), "Near plane clip depth in Reverse-Z must map to 1.0.");
  NE_TEST_ASSERT(math::equals(clipDepth(projection, 1000.0f), 0.0f, 1e-4f), "Far plane clip depth in Reverse-Z must map to 0.0.");
  NE_TEST_ASSERT(math::equals(projection[1][1], 1.0f / math::tan(math::radians(22.5f)), 1e-4f), "Vertical scale must follow the FOV.");
  NE_TEST_ASSERT(math::equals(projection[0][0], projection[1][1] / kAspectRatio, 1e-4f), "Horizontal scale must follow the aspect.");
}

NE_TEST_CASE("camera", "CameraComponent Perspective Infinite Far Clip") {
  CameraComponent camera{.nearClip = 0.1f, .farClip = 0.0f}; // Far clip <= 0 selects the infinite far plane
  Mat4 projection = camera.getProjectionMatrix(kAspectRatio);

  NE_TEST_ASSERT(math::equals(clipDepth(projection, 0.1f), 1.0f, 1e-4f), "Near plane clip depth in Infinite Far Reverse-Z must map to 1.0.");
  NE_TEST_ASSERT(math::equals(clipDepth(projection, 1e6f), 0.0f, 1e-3f), "Distant point depth in Infinite Far Reverse-Z must approach 0.0.");
}

NE_TEST_CASE("camera", "CameraComponent Orthographic Projection") {
  CameraComponent camera{.projectionType = CameraComponent::ProjectionType::Orthographic,
                         .orthoSize = 10.0f,
                         .nearClip = 0.1f,
                         .farClip = 100.0f};
  Mat4 projection = camera.getProjectionMatrix(kAspectRatio);

  NE_TEST_ASSERT((projection * projection.inversed()).equals(Mat4::Identity, 1e-3f), "Orthographic projection must be invertible.");
  NE_TEST_ASSERT(math::equals(clipDepth(projection, 0.1f), 1.0f, 1e-4f), "Ortho near plane must map to depth 1.0.");
  NE_TEST_ASSERT(math::equals(clipDepth(projection, 100.0f), 0.0f, 1e-4f), "Ortho far plane must map to depth 0.0.");
}

NE_TEST_CASE("camera", "CameraComponent View and View-Projection Matrix Calculation") {
  CameraComponent camera{.fov = math::radians(45.0f), .nearClip = 0.1f, .farClip = 1000.0f};

  // Aligned camera looking along +X, +Z up
  Transform pose(Vec3(-4.0f, 0.0f, 0.0f));
  Mat4 view = camera.getViewMatrix(pose.toMatrix());

  Vec4 eyeInView = view * Vec4(pose.position, 1.0f);
  NE_TEST_ASSERT(eyeInView.equals(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 1e-4f), "Camera position must map to view-space origin.");

  // Null Engine world axes (+X Forward, +Y Left, +Z Up) map to the optical frame (+X Right, +Y Down, +Z Forward)
  NE_TEST_ASSERT((view * Vec4(pose.getLeft(), 0.0f)).equals(Vec4(-1.0f, 0.0f, 0.0f, 0.0f), 1e-4f), "Camera Left (+Y) must map to View -X.");
  NE_TEST_ASSERT((view * Vec4(pose.getUp(), 0.0f)).equals(Vec4(0.0f, -1.0f, 0.0f, 0.0f), 1e-4f), "Camera Up (+Z) must map to View -Y.");
  NE_TEST_ASSERT((view * Vec4(pose.getForward(), 0.0f)).equals(Vec4(0.0f, 0.0f, 1.0f, 0.0f), 1e-4f), "Camera Forward (+X) must map to View +Z.");

  Mat4 viewProj = camera.getViewProjectionMatrix(pose.toMatrix(), kAspectRatio);
  NE_TEST_ASSERT(viewProj.equals(camera.getProjectionMatrix(kAspectRatio) * view), "getViewProjectionMatrix must equal Projection * View.");

  // Arbitrary orientation; world scale is ignored
  Transform rotatedPose(Vec3(10.0f, -5.0f, 2.0f), Quat::fromEuler(math::radians(Vec3(20.0f, 35.0f, -15.0f))), Vec3(3.0f));
  Mat4 rotatedView = camera.getViewMatrix(rotatedPose.toMatrix());

  NE_TEST_ASSERT((rotatedView * Vec4(rotatedPose.position, 1.0f)).equals(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 1e-4f),
                 "Rotated camera position must map to view-space origin.");
  NE_TEST_ASSERT((rotatedView * Vec4(rotatedPose.getLeft(), 0.0f)).equals(Vec4(-1.0f, 0.0f, 0.0f, 0.0f), 1e-4f),
                 "Rotated Camera Left must map to View -X.");
  NE_TEST_ASSERT((rotatedView * Vec4(rotatedPose.getUp(), 0.0f)).equals(Vec4(0.0f, -1.0f, 0.0f, 0.0f), 1e-4f),
                 "Rotated Camera Up must map to View -Y.");

  // World point in front of camera maps to positive Z in view space
  Vec3 targetWorld = rotatedPose.position + rotatedPose.getForward() * 5.0f;
  Vec4 targetView = rotatedView * Vec4(targetWorld, 1.0f);
  NE_TEST_ASSERT(targetView.equals(Vec4(0.0f, 0.0f, 5.0f, 1.0f), 1e-4f), "Point 5 units forward must have view (0, 0, 5).");
}

NE_TEST_CASE("camera", "CameraComponent Screen Orientation Is Not Mirrored") {
  CameraComponent camera{.fov = math::radians(60.0f), .nearClip = 0.1f, .farClip = 100.0f};
  Mat4 pose = Transform(Vec3(-4.0f, 0.0f, 0.0f)).toMatrix();

  // Vulkan NDC: +X right, +Y down. World +Y (Left) must land on the left half, world +Z (Up) on the top half.
  Mat4 viewProj = camera.getViewProjectionMatrix(pose, kAspectRatio);
  Vec4 leftClip = viewProj * Vec4(0.0f, 1.0f, 0.0f, 1.0f);
  Vec4 upClip = viewProj * Vec4(0.0f, 0.0f, 1.0f, 1.0f);
  NE_TEST_ASSERT(leftClip.x / leftClip.w < 0.0f, "World +Y (Left) must project to NDC -X (screen left).");
  NE_TEST_ASSERT(math::equals(leftClip.y / leftClip.w, 0.0f, 1e-4f), "World +Y must stay on the horizon.");
  NE_TEST_ASSERT(upClip.y / upClip.w < 0.0f, "World +Z (Up) must project to NDC -Y (screen top).");
  NE_TEST_ASSERT(math::equals(upClip.x / upClip.w, 0.0f, 1e-4f), "World +Z must stay on the vertical center line.");

  // Orthographic projection must keep the same orientation
  camera.projectionType = CameraComponent::ProjectionType::Orthographic;
  camera.orthoSize = 10.0f;
  viewProj = camera.getViewProjectionMatrix(pose, kAspectRatio);
  leftClip = viewProj * Vec4(0.0f, 1.0f, 0.0f, 1.0f);
  upClip = viewProj * Vec4(0.0f, 0.0f, 1.0f, 1.0f);
  NE_TEST_ASSERT(leftClip.x / leftClip.w < 0.0f, "Ortho: World +Y (Left) must project to NDC -X (screen left).");
  NE_TEST_ASSERT(upClip.y / upClip.w < 0.0f, "Ortho: World +Z (Up) must project to NDC -Y (screen top).");
}

} // namespace ne::test

#endif
