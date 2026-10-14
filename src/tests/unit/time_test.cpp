#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/time.h"

#include <chrono>
#include <thread>

namespace ne::test {

NE_TEST_CASE("time", "Time Lifecycle & Reset") {
  Time::reset();

  NE_TEST_ASSERT(Time::getDeltaTime() == 0.0f, "Delta time must be 0.0f immediately after reset.");
  NE_TEST_ASSERT(Time::getTime() == Time::Nanoseconds{0}, "Total elapsed time must be 0 immediately after reset.");
  NE_TEST_ASSERT(Time::getTimeNow() >= 0.0, "Time::getTimeNow() must be non-negative.");
}

NE_TEST_CASE("time", "Time Tick & Delta Progression") {
  Time::init();

  // Sleep for ~10ms to simulate a frame duration
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  Time::tick();

  const float dt = Time::getDeltaTime();
  const double total = Time::getTimeSeconds();

  NE_TEST_ASSERT(dt >= 0.005f && dt <= 0.1f, "Delta time must reflect elapsed frame duration within bounds.");
  NE_TEST_ASSERT(total >= 0.005, "Total time must accumulate delta time.");
  NE_TEST_ASSERT(Time::getDelta() == Time::getTime(), "First frame total time must equal first frame delta time.");

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  Time::tick();

  const float dt2 = Time::getDeltaTime();
  const double total2 = Time::getTimeSeconds();

  NE_TEST_ASSERT(dt2 >= 0.005f && dt2 <= 0.1f, "Second frame delta time must be within bounds.");
  NE_TEST_ASSERT(total2 > total, "Total elapsed time must monotonically increase across frames.");
}

NE_TEST_CASE("time", "Time Max Delta Clamping") {
  Time::init();

  // Simulate a long lag spike / pause (120ms)
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  Time::tick();

  const float dt = Time::getDeltaTime();
  NE_TEST_ASSERT(dt <= 0.1001f, "Delta time must be clamped to kMaxDeltaTime (0.1s).");
  NE_TEST_ASSERT(dt >= 0.09f, "Delta time must reach near the maximum clamp limit on large delays.");
}

NE_TEST_CASE("time", "Time Total Tracks The Clock Without Drift") {
  const float previousScale = Time::getTimeScale();
  constexpr int kTicks = 10000;

  for (const float scale : {1.0f, 1.37f}) {
    Time::setTimeScale(scale);
    Time::init();

    Time::Nanoseconds summed{0};
    double before = 0.0;
    double after = 0.0;
    for (int i = 0; i < kTicks; ++i) {
      before = Time::getTimeNow();
      Time::tick();
      after = Time::getTimeNow();
      summed += Time::getDelta();
    }

    // Scaling rounds each delta to whole nanoseconds, at most 0.5 ns per tick
    const double tolerance = scale == 1.0f ? 0.0 : kTicks * 0.5e-9;
    const double total = Time::getTimeSeconds();
    NE_TEST_ASSERT(Time::getTime() == summed, "Total time must equal the sum of the frame deltas.");
    NE_TEST_ASSERT(total >= before * scale - tolerance && total <= after * scale + tolerance,
                   "Total time must equal the scaled time since init() at the last tick.");
  }

  Time::setTimeScale(previousScale);
}

NE_TEST_CASE("time", "Time Monotonic Timestamp (getTimeNow)") {
  Time::init();

  const double t1 = Time::getTimeNow();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const double t2 = Time::getTimeNow();

  NE_TEST_ASSERT(t2 > t1, "Time::getTimeNow() must be strictly monotonic.");
  const double elapsed = t2 - t1;
  NE_TEST_ASSERT(elapsed >= 0.005 && elapsed < 0.5, "Time::getTimeNow() delta must accurately reflect sleep duration.");
}

} // namespace ne::test

#endif
