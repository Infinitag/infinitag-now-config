#include "WebLog.h"

#include <esp_attr.h>
#include <esp_system.h>
#include <stdarg.h>

namespace {

constexpr uint32_t MAGIC = 0x494E4C47;  // "INLG"
constexpr size_t CAP = 6144;            // hard cap: ring overwrites oldest

struct RingHdr {
  uint32_t magic;
  uint32_t head;  // next write position
  uint32_t used;  // valid bytes (<= CAP)
};
__NOINIT_ATTR RingHdr s_hdr;
__NOINIT_ATTR char s_buf[CAP];

void put(char c) {
  s_buf[s_hdr.head] = c;
  s_hdr.head = (s_hdr.head + 1) % CAP;
  if (s_hdr.used < CAP) s_hdr.used++;
}

}  // namespace

namespace weblog {

void begin() {
  // After power-on the noinit RAM is garbage - only trust it when the
  // header is plausible (survived a mere software reset).
  if (s_hdr.magic != MAGIC || s_hdr.head >= CAP || s_hdr.used > CAP) {
    s_hdr.magic = MAGIC;
    s_hdr.head = 0;
    s_hdr.used = 0;
  }
}

void clear() {
  s_hdr.head = 0;
  s_hdr.used = 0;
}

size_t size() { return s_hdr.used; }

const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Power-On";
    case ESP_RST_SW: return "Software-Reboot";
    case ESP_RST_PANIC: return "PANIC (Absturz!)";
    case ESP_RST_INT_WDT: return "INT-Watchdog";
    case ESP_RST_TASK_WDT: return "Task-Watchdog";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (Spannung!)";
    case ESP_RST_DEEPSLEEP: return "Deepsleep";
    default: return "unbekannt";
  }
}

void logf(const char *fmt, ...) {
  char line[192];
  const int p = snprintf(line, sizeof(line), "[%6lu] ",
                         (unsigned long)(millis() / 1000));
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(line + p, sizeof(line) - p, fmt, args);
  va_end(args);
  if (n <= 0) return;
  n += p;
  Serial.print(line);
  const size_t len =
      (size_t)n < sizeof(line) - 1 ? (size_t)n : sizeof(line) - 1;
  for (size_t i = 0; i < len; i++) put(line[i]);
}

void appendHtml(String &out) {
  out.reserve(out.length() + s_hdr.used + 64);
  size_t pos = (s_hdr.head + CAP - s_hdr.used) % CAP;
  for (size_t i = 0; i < s_hdr.used; i++) {
    const char c = s_buf[pos];
    pos = (pos + 1) % CAP;
    if (c == '&') {
      out += "&amp;";
    } else if (c == '<') {
      out += "&lt;";
    } else if (c == '>') {
      out += "&gt;";
    } else {
      out += c;
    }
  }
}

}  // namespace weblog
