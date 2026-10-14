#include "core/time.h"
#include "core/math/math_utils.h"

namespace ne {

Time::State Time::sState{};

void Time::init() {
  const auto now = Clock::now();
  sState.startTime = now;
  sState.previousTime = now;
  sState.time = Nanoseconds{0};
  sState.delta = Nanoseconds{0};
  sState.unscaledDelta = Nanoseconds{0};
  sState.initialized = true;
}

void Time::reset() { init(); }

void Time::tick() {
  if (!sState.initialized) {
    init();
    return;
  }

  const auto now = Clock::now();
  const Nanoseconds elapsed = now - sState.previousTime;
  sState.previousTime = now;

  // Scaling rounds to whole nanoseconds (exact at 0x and 1x), and the total only ever adds integers
  sState.unscaledDelta = math::min(elapsed, kMaxDelta);
  sState.delta = Nanoseconds{math::roundToInt64(static_cast<double>(sState.unscaledDelta.count()) * sState.timeScale)};
  sState.time += sState.delta;
}

} // namespace ne
