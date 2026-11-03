#pragma once
#include <stdint.h>

struct SsrFrame {
  uint8_t on;
  uint8_t total;
};

// Match F1ATB V17.13 for integer requests. The tolerance window is
// narrower than one step, so only the nearest parity-compatible ON count
// can qualify at each frame length.
inline SsrFrame ssrFrameFor(uint8_t percent) {
  if (percent == 0) return {0, 1};
  if (percent >= 100) return {1, 1};

  for (uint8_t total = 20; total <= 100; ++total) {
    const uint32_t target = uint32_t(percent) * total;
    const uint8_t quantum = total % 2 ? 1 : 2;
    const uint32_t on = ((target + 50 * quantum) / (100 * quantum)) * quantum;
    const uint32_t actual = on * 100;
    const uint32_t error = actual > target ? actual - target : target - actual;
    if (error * 5 < 2 * total) return {uint8_t(on), total};
  }
  return {0, 1};
}

struct SsrHalfWave {
  uint16_t credit = 0;
  bool phase = false;
  bool lastOnPhase = false;
};

// Alternate the parity of emitted pulses on the internal timer, as in F1ATB.
// Timer parity is independent of actual mains polarity.
inline __attribute__((always_inline)) bool ssrHalfWaveAdvance(SsrHalfWave &pulse, uint8_t percent) {
  pulse.phase = !pulse.phase;
  if (percent == 0 || percent >= 100) {
    pulse.credit = 0;
    pulse.lastOnPhase = pulse.phase;
    return percent >= 100;
  }
  pulse.credit += percent;
  if (pulse.credit < 100 || pulse.phase == pulse.lastOnPhase) return false;
  pulse.credit -= 100;
  pulse.lastOnPhase = pulse.phase;
  return true;
}

// Called by the timer ISR. Force inlining to keep the step code in IRAM.
inline __attribute__((always_inline)) bool ssrAdvance(SsrFrame &frame, const SsrFrame &requested, uint8_t &step) {
  if (step == 0 || requested.total == 1) {
    step = 0;
    frame.on = requested.on;
    frame.total = requested.total;
  }
  const bool on = step < frame.on;
  if (++step >= frame.total) step = 0;
  return on;
}
