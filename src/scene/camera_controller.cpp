#include "scene/camera_controller.h"
#include "components/transform_component.h"
#include "platform/input.h"

namespace ne {

namespace {

constexpr float kMaxPitch = math::radians(89.0f); // Stops short of straight up or down, where yaw is undefined

} // namespace

CameraController::CameraController(float iMoveSpeed, float iLookSensitivity)
    : mMoveSpeed(iMoveSpeed), mLookSensitivity(iLookSensitivity) {}

void CameraController::update(float iDeltaTime, TransformComponent& ioTransform) {
  if (Input::isMouseButtonPressed(MouseButton::Right)) {
    mIsLooking = true;
    Input::setCursorMode(CursorMode::Disabled);

    // Start from the current orientation
    Vec3 euler = ioTransform.getLocal().getEulerAngles();
    mPitch = euler.y;
    mYaw = euler.z;
  } else if (Input::isMouseButtonReleased(MouseButton::Right)) {
    if (mIsLooking) {
      mIsLooking = false;
      Input::setCursorMode(CursorMode::Normal);
    }
  }

  if (mIsLooking) {
    Vec2 mouseDelta = Input::getMouseDelta();
    mYaw -= mouseDelta.x * mLookSensitivity; // Positive yaw turns left (counter-clockwise about +Z)
    mPitch = math::clamp(mPitch + mouseDelta.y * mLookSensitivity, -kMaxPitch, kMaxPitch); // Positive pitch tilts down
    ioTransform.setLocalRotation(Quat::fromEuler(Vec3(0.0f, mPitch, mYaw)));
  }

  Vec2 scroll = Input::getMouseScroll();
  if (scroll.y != 0.0f) {
    mMoveSpeed = math::clamp(mMoveSpeed + scroll.y * 0.5f, 0.2f, 50.0f);
  }

  if (iDeltaTime > 0.0f) {
    Vec3 forward = ioTransform.getLocal().getForward();
    Vec3 left = ioTransform.getLocal().getLeft();
    Vec3 moveDir = Vec3::Zero;

    if (Input::isKeyDown(KeyCode::W)) moveDir += forward;
    if (Input::isKeyDown(KeyCode::S)) moveDir -= forward;
    if (Input::isKeyDown(KeyCode::D)) moveDir -= left;
    if (Input::isKeyDown(KeyCode::A)) moveDir += left;
    if (Input::isKeyDown(KeyCode::E)) moveDir += Vec3::Up;
    if (Input::isKeyDown(KeyCode::Q)) moveDir -= Vec3::Up;

    if (moveDir.lengthSquared() > math::kSmallNumber) {
      moveDir.normalize();
      float speed = mMoveSpeed * (Input::isShiftDown() ? 2.5f : 1.0f);
      ioTransform.setLocalPosition(ioTransform.getLocal().position + moveDir * (speed * iDeltaTime));
    }
  }
}

} // namespace ne
