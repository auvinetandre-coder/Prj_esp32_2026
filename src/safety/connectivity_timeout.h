#pragma once
#include <stdint.h>

// Both configured links must have been absent for five continuous minutes.
inline bool connectivityWatchdogExpired(bool wifiExpected, bool wifiConnected,
                                       bool radioExpected, uint32_t wifiDownMs,
                                       uint32_t radioSilentMs) {
  return wifiExpected && !wifiConnected && radioExpected &&
         wifiDownMs >= 300000UL && radioSilentMs >= 300000UL;
}
