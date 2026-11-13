#include "console_serial.h"
#include <sys/time.h>

ConsoleSerial &consoleSerial() {
  static ConsoleSerial port;
  return port;
}

uint64_t consoleEpochMs() {
  timeval clock{};
  gettimeofday(&clock, nullptr);
  if (clock.tv_sec < 1700000000) return 0;
  return uint64_t(clock.tv_sec) * 1000 + clock.tv_usec / 1000;
}

void ConsoleSerial::begin(unsigned long baud) { Serial.begin(baud); }
size_t ConsoleSerial::setTxBufferSize(size_t bytes) { return Serial.setTxBufferSize(bytes); }
int ConsoleSerial::available() { return Serial.available(); }
int ConsoleSerial::read() { return Serial.read(); }
int ConsoleSerial::availableForWrite() { return Serial.availableForWrite(); }

size_t ConsoleSerial::write(uint8_t byte) { return write(&byte, 1); }

size_t ConsoleSerial::write(const uint8_t *data, size_t size) {
  const size_t sent = Serial.write(data, size);
  if (sent) capture(data, sent);
  return sent;
}

void ConsoleSerial::finishLine(uint32_t uptimeMs, uint64_t epochMs) {
  if (length || truncated) {
    Line &line = lines[head];
    memcpy(line.text, partial, length);
    line.text[length] = '\0';
    line.truncated = truncated;
    line.uptimeMs = uptimeMs;
    line.epochMs = epochMs;
    head = (head + 1) % capacity;
    ++sequence;
    if (count < capacity) ++count;
  }
  length = 0;
  truncated = false;
}

void ConsoleSerial::capture(const uint8_t *data, size_t size) {
  // Bound capture work even for a very large UART write. The UART gets it all.
  constexpr size_t budget = 256;
  const size_t scanned = size < budget ? size : budget;
  const bool endsLine = data[size - 1] == '\n';
  const uint32_t uptime = millis();
  const uint64_t epoch = endsLine || memchr(data, '\n', scanned) ? consoleEpochMs() : 0;
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < scanned; ++i) {
    const uint8_t byte = data[i];
    if (byte == '\n') finishLine(uptime, epoch);
    else if (byte >= 32 && byte != 127) {
      if (length < sizeof(partial) - 1) partial[length++] = byte;
      else truncated = true;
    }
  }
  if (size > scanned) {
    truncated = true;
    if (endsLine) finishLine(uptime, epoch);
  }
  portEXIT_CRITICAL(&mux);
}

size_t ConsoleSerial::readLines(uint32_t after, Line *out, size_t limit, uint32_t &next, uint32_t &lost) {
  portENTER_CRITICAL(&mux);
  const uint32_t pending = sequence - after;
  // A future cursor is malformed, not a report of billions of lost lines.
  const uint32_t unread = pending <= INT32_MAX ? pending : count;
  const size_t retained = unread < count ? unread : count;
  lost = unread - retained;
  const size_t copied = retained < limit ? retained : limit;
  for (size_t i = 0; i < copied; ++i) out[i] = lines[(head + capacity - retained + i) % capacity];
  next = sequence - retained + copied;
  portEXIT_CRITICAL(&mux);
  return copied;
}
