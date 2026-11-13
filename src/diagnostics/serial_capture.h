#pragma once
#include "console_serial.h"

// Opt in only in project translation units, after their library includes.
#undef Serial
#define Serial (consoleSerial())
