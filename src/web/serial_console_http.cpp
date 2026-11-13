#include "web_ui.h"
#include "../diagnostics/console_serial.h"
#include "../diagnostics/serial_debug.h"
#include <esp_random.h>
#include <lwip/sockets.h>

namespace {

// Requests run on the existing WebServer loop. There is no console client task
// and no retained socket. Give a slow client one nonblocking send attempt.
void consoleReply(WebServer &server, int code, const char *reason, const char *text,
                  const JsonDocument *json = nullptr) {
  static char packet[4096];
  const size_t bytes = json ? measureJson(*json) : strlen(text);
  const int header = snprintf(packet, sizeof(packet),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n",
      code, reason, json ? "application/json" : "text/plain; charset=utf-8", unsigned(bytes),
      code == 401 ? "WWW-Authenticate: Basic realm=\"RouteurSolaireESP32\"\r\n" : "");
  if (header <= 0 || size_t(header) + bytes >= sizeof(packet)) return;
  if (json) serializeJson(*json, packet + header, sizeof(packet) - header);
  else memcpy(packet + header, text, bytes);
  const int socket = server.client().fd();
  if (socket >= 0) ::send(socket, packet, header + bytes, MSG_DONTWAIT);
}

bool parseCursor(const String &value, uint32_t &out) {
  out = 0;
  if (value.length() > 10) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c < '0' || c > '9' || out > (UINT32_MAX - uint32_t(c - '0')) / 10) return false;
    out = out * 10 + (c - '0');
  }
  return true;
}

}  // namespace

bool WebUi::authorizeConsole() {
  if (!authEnabled()) {
    consoleReply(server, 403, "Forbidden", "Activez l'authentification Web pour utiliser la console.");
    return false;
  }
  if (!isAuthenticated()) {
    consoleReply(server, 401, "Unauthorized", "Connexion requise.");
    return false;
  }
  return true;
}

void WebUi::consoleRoutes() {
  static const uint32_t bootId = esp_random();
  const char *headers[] = {"X-Serial-Console"};
  server.collectHeaders(headers, 1);

  server.on("/api/serial-console", HTTP_GET, [this]() {
    if (!authorizeConsole()) return;
    static uint32_t lastRead = 0;
    const uint32_t now = millis();
    if (now - lastRead < 160) {
      consoleReply(server, 429, "Too Many Requests", "Lecture trop frequente.");
      return;
    }
    lastRead = now;
    uint32_t after, browserBoot;
    if (!parseCursor(server.arg("after"), after) || !parseCursor(server.arg("boot"), browserBoot)) {
      consoleReply(server, 400, "Bad Request", "Curseur invalide.");
      return;
    }
    if (browserBoot != bootId) after = 0;
    ConsoleSerial::Line batch[8]{};
    uint32_t next, lost;
    const size_t count = consoleSerial().readLines(after, batch, 8, next, lost);
    JsonDocument doc;
    doc["boot"] = bootId;
    doc["next"] = next;
    doc["lost"] = lost;
    doc["more"] = count == 8;
    JsonArray lines = doc["lines"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) {
      JsonObject row = lines.add<JsonObject>();
      row["ms"] = batch[i].uptimeMs;
      if (batch[i].epochMs) row["epochMs"] = batch[i].epochMs;
      row["text"] = static_cast<const char *>(batch[i].text);
      if (batch[i].truncated) row["cut"] = true;
    }
    consoleReply(server, 200, "OK", "", &doc);
  });

  server.on("/api/serial-console/command", HTTP_POST, [this]() {
    if (!authorizeConsole()) return;
    if (server.header("X-Serial-Console") != "1") {
      consoleReply(server, 403, "Forbidden", "En-tete console requis.");
      return;
    }
    static uint32_t lastCommand = 0;
    const uint32_t now = millis();
    if (now - lastCommand < 400) {
      consoleReply(server, 429, "Too Many Requests", "Attendez avant la prochaine commande.");
      return;
    }
    lastCommand = now;
    String command = server.arg("plain");
    if (command.length() > 47 || command.length() != strlen(command.c_str())) {
      consoleReply(server, 400, "Bad Request", "Commande invalide.");
      return;
    }
    char reply[160];
    if (!serialDebug.executeCommand(command.c_str(), reply, sizeof(reply))) {
      consoleReply(server, 400, "Bad Request", reply[0] ? reply : "Commande vide. Tapez help.");
      return;
    }
    consoleReply(server, 200, "OK", reply);
  });
}
