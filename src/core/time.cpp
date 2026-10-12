#include "core/time.h"
#include "core/math/math_utils.h"

namespace ne {

Time::State Time::sState{};

void Time::init() {
  const auto now = Clock::now();
  sState.startTime = now;
  sState.currentTime = now;
  sState.previousTime = now;
  sState.deltaTime = 0.0f;
  sState.unscaledDeltaTime = 0.0f;
  sState.timeSeconds = 0.0f;
  sState.initialized = true;
}

void Time::reset() { init(); }

void Time::tick() {
  if (!sState.initialized) {
    init();
    return;
  }

  const auto now = Clock::now();
  sState.previousTime = sState.currentTime;
  sState.currentTime = now;

  const float rawDelta = std::chrono::duration<float>(sState.currentTime - sState.previousTime).count();
  sState.unscaledDeltaTime = math::clamp(rawDelta, 0.0f, kMaxDeltaTime);
  sState.deltaTime = sState.unscaledDeltaTime * sState.timeScale;
  sState.timeSeconds += sState.deltaTime;
}

} // namespace ne
