#pragma once
#include <stdint.h>
#include <string.h>

// One physical owner arbitrates explicit user intents. Periodic power packets
// may refresh a session, but can never recreate one cancelled by AUTO/OFF.
struct SharedActuatorControl {
  enum Mode { AUTO, MANUAL, OFF };
  Mode mode = AUTO;
  uint32_t token = 0;
  char peer[18]{};

  void reset() {
    mode = AUTO;
    token = 0;
    peer[0] = 0;
  }
  void claim(Mode next, uint32_t session, const char *controller) {
    mode = next;
    token = session;
    strncpy(peer, controller, sizeof(peer) - 1);
    peer[sizeof(peer) - 1] = 0;
  }
  bool accepts(const char *controller, uint32_t session) const {
    if (mode == AUTO) return session == 0;
    return token != 0 && token == session && peer[0] && strcmp(peer, controller) == 0;
  }
  const char *name() const { return mode == AUTO ? "AUTO" : (mode == OFF ? "OFF" : "MANUAL"); }
};
