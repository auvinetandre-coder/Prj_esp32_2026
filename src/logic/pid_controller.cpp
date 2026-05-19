#include "pid_controller.h"

void PIDController::begin() {
  reset();
  state.addLog("PIDController pret");
}

void PIDController::reset() {
  integral = 0.0f;
  previousError = 0.0f;
  lastUpdateMs = 0;
  outputPercent = 0.0f;
  state.pidOutputPercent = 0.0f;
  state.commandPercent = 0.0f;
  state.pidStatus = "IDLE";
}

void PIDController::update(uint32_t now) {
  JsonObject router = config.system()["router"];
  String mode = router["mode"] | "AUTO";
  mode.toUpperCase();
  state.systemMode = mode;
  state.pidEnabled = router["pidEnabled"] | true;

  const float heaterMaxW = router["heaterMaxPowerW"] | router["ssr1MaxW"] | 1500.0f;
  const uint32_t elapsedMs = lastUpdateMs ? now - lastUpdateMs : 100;
  lastUpdateMs = now;

  if (state.safetyTripped || mode == "OFF" || !state.pidEnabled) {
    reset();
    state.pidStatus = state.safetyTripped ? "SAFETY" : (mode == "OFF" ? "OFF" : "PID_OFF");
    actuators.setPower("ssr1_water_heater", 0);
    return;
  }

  if (mode == "FORCED" || mode == "FORCE" || mode == "FORCE_") {
    const float forced = constrain(router["forcedPercent"] | 0.0f, 0.0f, 100.0f);
    outputPercent = rampLimit(forced, router["maxOutputRampPercentPerSecond"] | 15.0f, elapsedMs);
    state.pidStatus = "FORCED";
    state.pidOutputPercent = outputPercent;
    state.commandPercent = outputPercent;
    state.heaterPowerW = heaterMaxW * outputPercent / 100.0f;
    actuators.setPower("ssr1_water_heater", outputPercent);
    return;
  }

  if (mode != "AUTO") return;

  const float measured = isnan(state.gridPowerFilteredW) ? state.gridPowerW : state.gridPowerFilteredW;
  const float setpoint = router["gridSetpointW"] | 0.0f;
  const float deadband = max(0.0f, router["deadbandW"] | 30.0f);
  const float kp = router["kp"] | 0.08f;
  const float ki = router["ki"] | 0.01f;
  const float kd = router["kd"] | 0.0f;
  const float maxRamp = router["maxOutputRampPercentPerSecond"] | 15.0f;

  float error = setpoint - measured;
  state.pidErrorW = error;
  if (fabs(error) <= deadband) {
    state.pidStatus = "DEADBAND";
    state.pidOutputPercent = outputPercent;
    state.commandPercent = outputPercent;
    state.heaterPowerW = heaterMaxW * outputPercent / 100.0f;
    actuators.setPower("ssr1_water_heater", outputPercent);
    previousError = error;
    return;
  }

  const float dt = max(0.02f, elapsedMs / 1000.0f);
  integral = constrain(integral + (error * dt), -5000.0f, 5000.0f);
  const float derivative = (error - previousError) / dt;
  previousError = error;

  float target = outputPercent + (kp * error) + (ki * integral) + (kd * derivative);
  if (measured > setpoint + deadband) target = min(target, outputPercent - 2.0f);
  target = constrain(target, 0.0f, 100.0f);
  outputPercent = rampLimit(target, maxRamp, elapsedMs);

  state.pidStatus = measured < -deadband ? "INJECTION" : "CONSUMPTION";
  state.pidOutputPercent = outputPercent;
  state.commandPercent = outputPercent;
  state.heaterPowerW = heaterMaxW * outputPercent / 100.0f;
  actuators.setPower("ssr1_water_heater", outputPercent);
}

float PIDController::rampLimit(float target, float maxRampPercentPerSecond, uint32_t elapsedMs) {
  maxRampPercentPerSecond = max(0.1f, maxRampPercentPerSecond);
  const float maxDelta = maxRampPercentPerSecond * (max<uint32_t>(elapsedMs, 20) / 1000.0f);
  if (target > outputPercent + maxDelta) return outputPercent + maxDelta;
  if (target < outputPercent - maxDelta) return outputPercent - maxDelta;
  return target;
}
