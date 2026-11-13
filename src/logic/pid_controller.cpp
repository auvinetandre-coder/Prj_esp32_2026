#include "pid_controller.h"

void PIDController::begin() { reset(); state.addLog("PIDController positionnel pret"); }

void PIDController::reset() {
  for (auto &i : instances) i = Instance();
  state.pidOutputPercent = 0;
  state.pidActive = false;
  state.pidErrorW = 0;
  state.pidGridSetpointW = NAN;
  state.pidContextId = "";
  state.pidStatus = "IDLE";
}

void PIDController::beginCycle() {
  for (auto &i : instances) i.used = false;
  state.pidContextId = "";
}

void PIDController::endCycle() {
  // Disabled/deleted rules, manual/radio takeover and safety release their states.
  for (auto &i : instances) if (!i.used) i = Instance();
  if (!state.pidContextId.length()) {
    state.pidOutputPercent = 0; state.pidErrorW = 0;
    state.pidActive = false; state.pidMeasuredW = NAN;
    state.pidGridSetpointW = NAN; state.pidStatus = "IDLE";
  }
}

PIDController::Instance *PIDController::instanceFor(const String &id) {
  if (!id.length()) return nullptr;
  for (auto &i : instances) if (i.id == id) return &i;
  for (auto &i : instances) if (!i.id.length()) { i.id = id; return &i; }
  return nullptr;
}

PIDSettings PIDController::settings(JsonObject a) {
  PIDSettings c;
  c.kp = a["kp"] | 0.02f; c.ki = a["ki"] | 0.002f; c.kd = a["kd"] | 0.0f;
  // Reactivity is an editor scale referenced to 200 ms, not the actual sample dt.
  // Enforce I-only here too: imported/invalid hidden gains can never activate P/D.
  if (String(a["regulation"] | "PID") == "I") {
    c.kp = 0; c.kd = 0;
    c.ki = (a["reactivity"] | 10.0f) / 2000.0f;
  }
  c.setpointW = a["gridSetpointW"] | 0.0f;
  c.deadbandW = a["deadbandW"] | 30.0f;
  c.biasPercent = a["biasPercent"] | 0.0f;
  c.maxPercent = a["maxPercent"] | 100.0f;
  c.risePerSecond = a["maxOutputRampPercentPerSecond"] | 5.0f;
  c.fallPerSecond = a["maxOutputDropPercentPerSecond"] | 100.0f;
  c.measurementFilterMs = a["measurementFilterMs"] | 0.0f;
  c.derivativeFilterMs = a["derivativeFilterMs"] | 500.0f;
  c.intervalMs = a["pidIntervalMs"] | 200UL;
  c.timeoutMs = a["measurementTimeoutMs"] | 5000UL;
  return c;
}

float PIDController::compute(JsonObject action, const SensorManager::PowerSample &sample,
                             uint32_t now, const String &context) {
  const uint32_t started = millis();
  const String id = action["actuatorId"] | "";
  Instance *i = instanceFor(id);
  if (!i) return 0;
  i->used = true;
  i->regulation = action["regulation"] | "PID";
  const PIDSettings c = settings(action);
  String signature;
  serializeJson(action, signature);
  signature += sample.source;
  const bool changed = i->signature != signature || i->context != context;
  const uint64_t resetToken = action["pidResetToken"] | uint64_t(0);
  if (i->resetToken != resetToken || i->source != sample.source) i->pid = PIDState();
  i->resetToken = resetToken;
  i->signature = signature; i->context = context; i->source = sample.source; i->settings = c;
  String mode = config.system()["router"]["mode"] | "AUTO";
  mode.toUpperCase();
  const bool enabled = action["pidEnabled"] | true;
  state.pidEnabled = enabled;
  const bool productionRequired = action["requireProduction"] | false;
  const bool noProduction = productionRequired && (!isfinite(state.productionW) || state.productionW <= 0 ||
      (state.mqttProductionEnabled && !state.mqttProductionAvailable));
  const char *failure = nullptr;
  if (state.safetyTripped || state.safetyActive) failure = "SAFETY";
  else if (mode != "AUTO" || !enabled) failure = "PID_OFF";
  else if (noProduction) failure = "NO_SOLAR_PRODUCTION";
  else if (!sample.valid) failure = "MEASUREMENT_UNAVAILABLE";
  else if (!validPIDSettings(c)) failure = "INVALID_PID_CONFIG";
  else if (action.containsKey("maxOutputPowerW") &&
      (!isfinite(action["maxOutputPowerW"] | 0.0f) || (action["maxOutputPowerW"] | 0.0f) <= 0)) failure = "INVALID_PID_CONFIG";
  if (!failure && !stepPID(i->pid, c, sample.watts, sample.timestampMs, millis(), changed)) failure = "INVALID_OR_STALE_MEASUREMENT";
  if (failure) {
    if (i->status != failure) state.logEvent("WARNING", "PID_STOP", id + ": " + failure, "PIDController");
    i->pid = PIDState(); i->rawMeasuredW = NAN; i->status = failure;
  } else {
    // Keep this paired with the accepted acquisition, including on held cycles.
    if (i->pid.lastMeasurementMs == sample.timestampMs) i->rawMeasuredW = sample.watts;
    i->status = fabsf(i->pid.previousError) <= c.deadbandW ? "DEADBAND" :
        (i->pid.saturated ? "SATURATION" : (i->pid.rampLimited ? "RAMP" : "ACTIVE"));
  }
  publish(*i);
  state.recordLoopPerf(PERF_PID, millis() - started);
  return i->pid.outputPercent;
}

void PIDController::applied(const String &id, float percent) {
  for (auto &i : instances) if (i.id == id && i.used) {
    const float accepted = isfinite(percent) ? pidClamp(percent, 0, 100) : 0;
    if (fabsf(accepted - i.pid.outputPercent) > 0.001f) {
      i.pid.integral = pidClamp(i.pid.integral + accepted - i.pid.outputPercent, -100, 100);
      i.pid.outputPercent = accepted;
    }
    i.pid.appliedPercent = accepted;
    publish(i);
  }
}

void PIDController::publish(const Instance &i) {
  // Historical scalar API represents the last effective PID; the array holds all outputs.
  state.pidContextId = i.context;
  state.pidActive = i.used && i.pid.initialized;
  state.pidOutputPercent = i.pid.outputPercent;
  state.pidErrorW = i.pid.initialized ? i.pid.previousError : NAN;
  state.pidGridSetpointW = i.settings.setpointW;
  state.pidMeasuredW = i.pid.initialized ? i.pid.measuredW : NAN;
  state.pidDeadbandW = i.settings.deadbandW;
  state.pidKp = i.settings.kp; state.pidKi = i.settings.ki; state.pidKd = i.settings.kd;
  state.pidRamp = i.settings.risePerSecond; state.pidDrop = i.settings.fallPerSecond;
  state.pidSensorSource = i.source;
  state.pidStatus = i.status;
}

void PIDController::toJson(JsonArray out) {
  for (const auto &i : instances) if (i.id.length()) {
    JsonObject o = out.add<JsonObject>();
    o["actuatorId"] = i.id; o["contextId"] = i.context; o["source"] = i.source;
    o["status"] = i.status; o["active"] = i.used && i.pid.initialized;
    o["regulation"] = i.regulation;
    o["targetGridW"] = i.settings.setpointW; o["deadbandW"] = i.settings.deadbandW;
    o["kp"] = i.settings.kp; o["ki"] = i.settings.ki; o["kd"] = i.settings.kd;
    o["measuredW"] = i.pid.initialized ? i.pid.measuredW : NAN;
    o["errorW"] = i.pid.initialized ? i.pid.previousError : NAN;
    o["p"] = i.pid.p; o["i"] = i.pid.integral; o["d"] = i.pid.d;
    o["outputPercent"] = i.pid.outputPercent; o["commandPercent"] = i.pid.appliedPercent;
    o["saturated"] = i.pid.saturated; o["rampLimited"] = i.pid.rampLimited;
    o["lastMeasurementMs"] = i.pid.lastMeasurementMs;
    o["rawMeasuredW"] = i.pid.initialized ? i.rawMeasuredW : NAN;
    o["lastCalculationMs"] = i.pid.lastUpdateMs;
    o["measurementAgeMs"] = i.pid.initialized ? uint32_t(millis() - i.pid.lastMeasurementMs) : 0;
    o["measurementFilterMs"] = i.settings.measurementFilterMs;
  }
}
