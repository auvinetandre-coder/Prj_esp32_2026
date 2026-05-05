#include "logger.h"

RuntimeState *Logger::state = nullptr;

void Logger::begin(RuntimeState &runtime) {
  state = &runtime;
  logInfo("BOOT", "Logger initialise", "Logger");
}

void Logger::logInfo(const String &code, const String &message, const String &source) {
  logEvent("INFO", code, message, source);
}

void Logger::logWarning(const String &code, const String &message, const String &source) {
  logEvent("WARNING", code, message, source);
}

void Logger::logError(const String &code, const String &message, const String &source) {
  logEvent("ERROR", code, message, source);
}

void Logger::logCritical(const String &code, const String &message, const String &source) {
  logEvent("CRITICAL", code, message, source);
}

void Logger::logEvent(const String &level, const String &code, const String &message, const String &source) {
  if (!state) return;
  state->logEvent(level, code, message, source);
}

void Logger::getRecentEvents(JsonArray out) {
  if (!state) return;
  state->eventsToJson(out);
}

void Logger::clearEvents() {
  if (!state) return;
  state->clearEvents();
}

void Logger::exportEventsJson(String &out) {
  out = "{}";
  if (!state) return;
  DynamicJsonDocument doc(8192);
  JsonArray events = doc["events"].to<JsonArray>();
  state->eventsToJson(events);
  serializeJson(doc, out);
}
