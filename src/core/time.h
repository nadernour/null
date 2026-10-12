#pragma once

#include <chrono>

namespace ne {

class Time {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  // Seconds, sampled once per frame by tick()
  static float getDeltaTime() { return sState.deltaTime; }
  static float getUnscaledDeltaTime() { return sState.unscaledDeltaTime; }
  static float getTimeSeconds() { return sState.timeSeconds; }

  // 0 pauses, 1 is normal speed
  static void setTimeScale(float iScale) { sState.timeScale = iScale >= 0 ? iScale : 0; }
  static float getTimeScale() { return sState.timeScale; }

  // Seconds since init(), read now
  static double getTimeNow() { return std::chrono::duration<double>(Clock::now() - sState.startTime).count(); }

  // Restarts the clocks; the time scale is kept
  static void init();
  static void tick();
  static void reset();

private:
  static constexpr float kMaxDeltaTime = 0.1f; // Clamps lag spikes

  struct State {
    TimePoint startTime;
    TimePoint currentTime;
    TimePoint previousTime;

    float deltaTime{0.0f};
    float unscaledDeltaTime{0.0f};
    float timeSeconds{0.0f};
    float timeScale{1.0f};
    bool initialized{false};
  };

  static State sState;
};

} // namespace ne
