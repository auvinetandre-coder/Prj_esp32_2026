#include "solar_router.h"

void SolarRouter::loop(uint32_t now) {
  JsonObject router = config.system()["router"];
  state.systemMode = router["mode"] | "AUTO";
  if (state.systemMode != "AUTO") return;

  const float threshold = router["injectionThresholdW"] | -200.0f;
  const float hysteresis = router["hysteresisW"] | 40.0f;
  const float kp = router["kp"] | 0.08f;
  const float ki = router["ki"] | 0.01f;
  const float kd = router["kd"] | 0.0f;
  const float error = (-threshold) - state.injectionW;

  if (state.gridPowerW < threshold) {
    integral = constrain(integral + error, -1000.0f, 1000.0f);
    float derivative = error - previousError;
    float next = state.ssr1PowerPct + (kp * -error) + (ki * -integral) + (kd * -derivative);
    actuators.setPower("ssr1_water_heater", constrain(next, 0.0f, 100.0f));
  } else if (state.gridPowerW > hysteresis) {
    integral = 0;
    actuators.setPower("ssr1_water_heater", max(0.0f, state.ssr1PowerPct - 5.0f));
  } else if (state.ssr1PowerPct > 0.0f) {
    actuators.setPower("ssr1_water_heater", state.ssr1PowerPct);
  }
  previousError = error;
}
