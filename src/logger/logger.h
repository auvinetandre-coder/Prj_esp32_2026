#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../runtime/runtime_state.h"

class Logger {
public:
  static void begin(RuntimeState &runtime);
  static void logInfo(const String &code, const String &message, const String &source);
  static void logWarning(const String &code, const String &message, const String &source);
  static void logError(const String &code, const String &message, const String &source);
  static void logCritical(const String &code, const String &message, const String &source);
  static void logEvent(const String &level, const String &code, const String &message, const String &source);
  static void getRecentEvents(JsonArray out);
  static void clearEvents();
  static void exportEventsJson(String &out);

private:
  static RuntimeState *state;
};
