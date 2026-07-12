// Internet self-supply of the box (Doc 21 Etappe 2): connect to the
// home WLAN, ask the GitHub releases of the three firmware repos for the
// latest versions, stream device images into the ImageStore and the
// box's own new firmware straight into the inactive OTA slot.
//
// All methods are blocking; the UiController drives the screens between
// the steps. Everything here runs OUTSIDE the ESP-NOW world – the caller
// tears ESP-NOW down first and reboots afterwards.

#pragma once
#include <Arduino.h>

class ImageStore;

struct ReleaseInfo {
  bool ok = false;
  uint8_t major = 0, minor = 0, patch = 0;
  char assetUrl[224] = "";
  char crcUrl[224] = "";   // .crc32 sidecar (empty for old releases)
  bool hasCrc = false;
  uint32_t expectCrc = 0;  // filled from the sidecar on download
  size_t expectSize = 0;
};

class NetUpdater {
 public:
  // WLAN credentials in NVS, set via the SoftAP page (Doc 21 §3.1).
  static bool hasWifiCredentials();
  static void setWifiCredentials(const char *ssid, const char *pass);
  static void getWifiSsid(char *out, size_t n);

  bool connectWifi(uint32_t timeoutMs = 20000);

  // Latest release of Infinitag/<repo>; asset must start with assetPrefix.
  bool fetchLatest(const char *repo, const char *assetPrefix,
                   ReleaseInfo &out);

  // Progress callback: bytes done / total (total 0 = unknown).
  using ProgressFn = void (*)(size_t done, size_t total);

  // Stream a device image into the store; verified against the release's
  // .crc32 sidecar (one automatic retry on corruption).
  bool downloadToStore(ReleaseInfo &rel, ImageStore &store,
                       ProgressFn progress);
  bool fetchCrcSidecar(ReleaseInfo &rel);

  // Stream the box's own new firmware into the OTA slot. true = flashed
  // and boot partition switched; caller shows "OK" and reboots.
  bool selfUpdate(const ReleaseInfo &rel, ProgressFn progress);

  const char *lastError() const { return _err; }

  static uint32_t versionKey(const ReleaseInfo &r) {
    return ((uint32_t)r.major << 16) | ((uint32_t)r.minor << 8) | r.patch;
  }

 private:
  void setError(const char *msg);
  char _err[64] = "";
};
