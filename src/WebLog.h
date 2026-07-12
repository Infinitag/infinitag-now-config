// Persistent in-RAM log behind the SoftAP "Log" page.
//
// logf() mirrors every line to Serial AND into a fixed 6 KB ring buffer
// in __NOINIT_ATTR RAM: the buffer can never grow beyond its capacity -
// new lines simply overwrite the oldest ones, so it always holds the
// most recent ~120 lines without any cleanup and without flash wear.
// The noinit section survives ESP.restart() (every mode switch on the
// box reboots), so the log of an internet-update run is still readable
// in the next update mode. Power-off/brownout clears it.
// Loop context only - never call from an ISR.

#pragma once
#include <Arduino.h>

namespace weblog {

// Validate the noinit buffer (reset after power-on). Call once early in
// setup(), before anything logs.
void begin();

// Every line gets a "[  123] " seconds-since-boot prefix.
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Human-readable reset reason of this boot ("Power-On", "PANIC", ...).
const char *resetReasonText();

// Append the buffered log, HTML-escaped, oldest line first.
void appendHtml(String &out);

size_t size();  // bytes currently buffered
void clear();

}  // namespace weblog

using weblog::logf;  // call sites read like printf
