#pragma once

#include <chrono>

namespace ne {

class Time {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Nanoseconds = std::chrono::nanoseconds; // int64, ±292 years

  // Exact totals, sampled once per frame by tick(); scaled by the time scale
  static Nanoseconds getTime() { return sState.time; }
  static Nanoseconds getDelta() { return sState.delta; }

  // Seconds, converted from the nanosecond totals on read, never accumulated
  static float getDeltaTime() { return std::chrono::duration<float>(sState.delta).count(); }
  static float getUnscaledDeltaTime() { return std::chrono::duration<float>(sState.unscaledDelta).count(); }
  static double getTimeSeconds() { return std::chrono::duration<double>(sState.time).count(); }

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
  static constexpr Nanoseconds kMaxDelta = std::chrono::milliseconds(100); // Clamps lag spikes

  struct State {
    TimePoint startTime;
    TimePoint previousTime;

    Nanoseconds time{0};
    Nanoseconds delta{0};
    Nanoseconds unscaledDelta{0};
    float timeScale{1.0f};
    bool initialized{false};
  };

  static State sState;
};

} // namespace ne
