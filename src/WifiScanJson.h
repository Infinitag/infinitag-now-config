// WLAN scan result -> JSON for the SoftAP "/scan" route (WLAN page).
//
// Pure, Arduino-free (natively unit-tested, see test/test_wifiscan):
// hidden networks (empty SSID) are dropped, duplicate SSIDs (mesh /
// repeater) collapse to the strongest entry, the list is sorted strongest
// first and SSIDs are JSON-escaped - they are untrusted bytes from the air.

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace wifiscan {

struct Entry {
  const char *ssid;
  int rssi;
  bool enc;  // any encryption (needs a password)
};

constexpr size_t MAX_ENTRIES = 32;

// Appends one JSON-escaped string (with quotes) to out; false = no room.
inline bool putEscaped(const char *s, char *out, size_t outLen, size_t &pos) {
  auto put = [&](const char *frag) {
    const size_t n = strlen(frag);
    if (pos + n >= outLen) return false;
    memcpy(out + pos, frag, n);
    pos += n;
    return true;
  };
  if (!put("\"")) return false;
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
    char buf[8];
    if (*p == '"') {
      strcpy(buf, "\\\"");
    } else if (*p == '\\') {
      strcpy(buf, "\\\\");
    } else if (*p < 0x20) {
      snprintf(buf, sizeof(buf), "\\u%04x", *p);
    } else {
      buf[0] = (char)*p;  // UTF-8 bytes pass through
      buf[1] = '\0';
    }
    if (!put(buf)) return false;
  }
  return put("\"");
}

// Writes `[{"ssid":"..","rssi":-50,"enc":1},...]` into out (always
// NUL-terminated). Entries that do not fit are dropped from the end.
// Returns the number of entries written.
inline size_t toJson(const Entry *in, size_t n, char *out, size_t outLen) {
  if (outLen < 3) {
    if (outLen) out[0] = '\0';
    return 0;
  }
  // pick: skip hidden + weaker duplicates (ties: keep the first)
  size_t idx[MAX_ENTRIES];
  size_t cnt = 0;
  for (size_t i = 0; i < n && cnt < MAX_ENTRIES; i++) {
    if (!in[i].ssid || !in[i].ssid[0]) continue;
    bool weaker = false;
    for (size_t j = 0; j < n; j++) {
      if (j == i || !in[j].ssid || strcmp(in[i].ssid, in[j].ssid) != 0) continue;
      if (in[j].rssi > in[i].rssi || (in[j].rssi == in[i].rssi && j < i)) {
        weaker = true;
        break;
      }
    }
    if (!weaker) idx[cnt++] = i;
  }
  // sort strongest first (insertion sort, n <= 32)
  for (size_t a = 1; a < cnt; a++) {
    const size_t v = idx[a];
    size_t b = a;
    while (b > 0 && in[idx[b - 1]].rssi < in[v].rssi) {
      idx[b] = idx[b - 1];
      b--;
    }
    idx[b] = v;
  }

  size_t pos = 0;
  out[pos++] = '[';
  size_t written = 0;
  for (size_t k = 0; k < cnt; k++) {
    const Entry &e = in[idx[k]];
    const size_t mark = pos;
    char tail[40];
    snprintf(tail, sizeof(tail), ",\"rssi\":%d,\"enc\":%d}", e.rssi,
             e.enc ? 1 : 0);
    const char *head = written ? ",{\"ssid\":" : "{\"ssid\":";
    const size_t hn = strlen(head);
    bool ok = pos + hn < outLen;
    if (ok) {
      memcpy(out + pos, head, hn);
      pos += hn;
      ok = putEscaped(e.ssid, out, outLen, pos);
    }
    const size_t tn = strlen(tail);
    if (ok && pos + tn + 2 <= outLen) {  // + "]" + NUL
      memcpy(out + pos, tail, tn);
      pos += tn;
      written++;
    } else {
      pos = mark;  // roll back the partial entry
      break;
    }
  }
  out[pos++] = ']';
  out[pos] = '\0';
  return written;
}

}  // namespace wifiscan
