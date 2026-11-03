#pragma once
#include <math.h>
#include <stdint.h>

// Units: W, seconds, percent. No sensor, network or actuator dependency.
struct PIDSettings {
  float kp = 0.02f, ki = 0.002f, kd = 0.0f;
  float setpointW = 0, deadbandW = 30, biasPercent = 0;
  float maxPercent = 100, risePerSecond = 5, fallPerSecond = 100;
  float measurementFilterMs = 0, derivativeFilterMs = 500;
  uint32_t intervalMs = 200, timeoutMs = 5000;
};

struct PIDState {
  float integral = 0, previousError = 0, previousMeasurement = 0;
  float filteredDerivative = 0, outputPercent = 0, appliedPercent = 0;
  float measuredW = 0, p = 0, d = 0;
  uint32_t lastUpdateMs = 0, lastMeasurementMs = 0;
  bool initialized = false, saturated = false, rampLimited = false;
};

inline float pidClamp(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }

inline bool validPIDSettings(const PIDSettings &c) {
  return isfinite(c.kp) && c.kp >= 0 && isfinite(c.ki) && c.ki >= 0 &&
      isfinite(c.kd) && c.kd >= 0 && isfinite(c.setpointW) &&
      isfinite(c.deadbandW) && c.deadbandW >= 0 &&
      isfinite(c.biasPercent) && c.biasPercent >= 0 && c.biasPercent <= 100 &&
      isfinite(c.maxPercent) && c.maxPercent >= 0 && c.maxPercent <= 100 &&
      isfinite(c.risePerSecond) && c.risePerSecond > 0 &&
      isfinite(c.fallPerSecond) && c.fallPerSecond > 0 &&
      isfinite(c.measurementFilterMs) && c.measurementFilterMs >= 0 &&
      isfinite(c.derivativeFilterMs) && c.derivativeFilterMs >= 0 &&
      c.intervalMs >= 20 && c.intervalMs <= 10000 &&
      c.timeoutMs >= c.intervalMs && c.timeoutMs <= 60000;
}

// Returns false on invalid/stale data and resets immediately to OFF.
// Repeated samples hold the command, without advancing I, D, filters or ramps.
inline bool stepPID(PIDState &s, const PIDSettings &c, float measurement,
                    uint32_t sampleMs, uint32_t now, bool reconfigure = false) {
  if (!validPIDSettings(c) || !isfinite(measurement) || !sampleMs ||
      uint32_t(now - sampleMs) >= c.timeoutMs ||
      !isfinite(s.integral) || !isfinite(s.appliedPercent) ||
      !isfinite(s.previousMeasurement) || !isfinite(s.filteredDerivative) ||
      !isfinite(s.outputPercent)) {
    s = PIDState();
    return false;
  }
  if (s.initialized && !reconfigure && sampleMs == s.lastMeasurementMs) return true;
  uint32_t elapsed = s.initialized ? uint32_t(sampleMs - s.lastMeasurementMs) : c.intervalMs;
  if (s.initialized && !reconfigure && elapsed < c.intervalMs) return true;
  // A measurement outage cannot be integrated retrospectively.
  if (elapsed >= c.timeoutMs) { s = PIDState(); elapsed = c.intervalMs; }
  const float dt = elapsed / 1000.0f;
  const bool starting = !s.initialized;
  float filtered = measurement;
  if (!starting && !reconfigure && c.measurementFilterMs > 0) {
    const float alpha = 1.0f - expf(-float(elapsed) / c.measurementFilterMs);
    filtered = s.previousMeasurement + alpha * (measurement - s.previousMeasurement);
  }
  const float error = c.setpointW - filtered;
  const float p = c.kp * error;
  float derivative = 0;
  if (!starting && !reconfigure) {
    const float raw = (filtered - s.previousMeasurement) / dt;
    const float alpha = c.derivativeFilterMs > 0
        ? 1.0f - expf(-float(elapsed) / c.derivativeFilterMs) : 1.0f;
    derivative = s.filteredDerivative + alpha * (raw - s.filteredDerivative);
  }
  const float d = -c.kd * derivative;
  if (!isfinite(error) || !isfinite(p) || !isfinite(d) || !isfinite(derivative)) {
    s = PIDState(); return false;
  }
  if (reconfigure && !starting) {
    // Track the last accepted command when changing gains, target or source.
    s.integral = pidClamp(s.appliedPercent - c.biasPercent - p, -100, 100);
  }
  s.measuredW = filtered;
  s.previousError = error;
  s.previousMeasurement = filtered;
  s.filteredDerivative = derivative;
  s.lastMeasurementMs = sampleMs;
  s.lastUpdateMs = now;
  s.initialized = true;
  s.p = p; s.d = d;
  s.saturated = false; s.rampLimited = false;
  if (fabsf(error) <= c.deadbandW) {
    s.outputPercent = pidClamp(s.appliedPercent, 0, c.maxPercent);
    s.integral = pidClamp(s.outputPercent - c.biasPercent - p - d, -100, 100);
    return true;
  }
  const float deltaI = c.ki * error * dt;
  if (!isfinite(deltaI)) { s = PIDState(); return false; }
  float integral = pidClamp(s.integral + deltaI, -100, 100);
  float target = c.biasPercent + p + integral + d;
  const float low = fmaxf(0, s.appliedPercent - c.fallPerSecond * dt);
  const float high = fminf(c.maxPercent, s.appliedPercent + c.risePerSecond * dt);
  // Conditional integration accounts for both output saturation and ramp limits.
  if ((target > high && deltaI > 0) || (target < low && deltaI < 0)) {
    integral = s.integral;
  }
  if (!isfinite(target) || !isfinite(filtered) || !isfinite(derivative)) {
    s = PIDState(); return false;
  }
  s.integral = integral;
  s.saturated = target < 0 || target > c.maxPercent;
  const float bounded = pidClamp(target, 0, c.maxPercent);
  s.outputPercent = pidClamp(bounded, low, fmaxf(low, high));
  s.outputPercent = pidClamp(s.outputPercent, 0, c.maxPercent);
  s.rampLimited = fabsf(s.outputPercent - bounded) > 0.0001f;
  return true;
}
