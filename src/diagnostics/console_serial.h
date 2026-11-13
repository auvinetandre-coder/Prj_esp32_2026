#pragma once

#include <Arduino.h>

// The firmware's Print calls still write to the original UART.
class ConsoleSerial : public Print {
public:
  struct Line {
    uint64_t epochMs;
    uint32_t uptimeMs;
    char text[192];
    bool truncated;
  };
  // Holds a complete diagnostic report plus background logs during Web polling.
  static constexpr size_t capacity = 96;

  void begin(unsigned long baud);
  size_t setTxBufferSize(size_t bytes);
  int available();
  int read();
  int availableForWrite() override;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *data, size_t size) override;
  using Print::write;
  size_t readLines(uint32_t after, Line *out, size_t limit, uint32_t &next, uint32_t &lost);

private:
  void capture(const uint8_t *data, size_t size);
  void finishLine(uint32_t uptimeMs, uint64_t epochMs);
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  Line lines[capacity]{};
  char partial[sizeof(Line::text)]{};
  size_t length = 0, count = 0, head = 0;
  uint32_t sequence = 0;
  bool truncated = false;
};

ConsoleSerial &consoleSerial();
uint64_t consoleEpochMs();
