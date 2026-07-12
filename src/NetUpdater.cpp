#include "NetUpdater.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "GithubRoots.h"
#include "ImageStore.h"
#include "WebLog.h"

static const char *NVS_NAMESPACE = "inow-config";

// ---------------------------------------------------------------------------
// WLAN credentials (NVS)
// ---------------------------------------------------------------------------

bool NetUpdater::hasWifiCredentials() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  const bool has = prefs.isKey("wifi_ssid");
  prefs.end();
  return has;
}

void NetUpdater::setWifiCredentials(const char *ssid, const char *pass) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  prefs.end();
}

void NetUpdater::getWifiSsid(char *out, size_t n) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  prefs.getString("wifi_ssid", out, n);
  prefs.end();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

void NetUpdater::setError(const char *msg) {
  strncpy(_err, msg, sizeof(_err) - 1);
  logf("[NET] Fehler: %s\n", _err);
}

bool NetUpdater::connectWifi(uint32_t timeoutMs) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  prefs.end();
  if (ssid.isEmpty()) {
    setError("Kein WLAN konfiguriert");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  const uint32_t deadline = millis() + timeoutMs;
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() >= deadline) {
      setError("WLAN-Verbindung fehlgeschlagen");
      return false;
    }
    delay(100);
  }
  logf("[NET] WLAN verbunden: %s, IP %s\n", ssid.c_str(),
       WiFi.localIP().toString().c_str());
  return true;
}

// Extract `"key":"value"` from a JSON blob (good enough for the GitHub
// release response; avoids a full JSON parser dependency).
static bool jsonFind(const String &body, const char *key, int from,
                     String &out, int *pos = nullptr) {
  String needle = String("\"") + key + "\":\"";
  const int k = body.indexOf(needle, from);
  if (k < 0) return false;
  const int start = k + needle.length();
  const int end = body.indexOf('"', start);
  if (end < 0) return false;
  out = body.substring(start, end);
  if (pos) *pos = end;
  return true;
}

bool NetUpdater::fetchLatest(const char *repo, const char *assetPrefix,
                             ReleaseInfo &out) {
  out = ReleaseInfo{};

  WiFiClientSecure client;
  client.setCACert(GITHUB_ROOT_CAS);

  HTTPClient http;
  String url = String("https://api.github.com/repos/Infinitag/") + repo +
               "/releases/latest";
  if (!http.begin(client, url)) {
    setError("HTTP-Init fehlgeschlagen");
    return false;
  }
  http.addHeader("User-Agent", "infinitag-config");
  http.addHeader("Accept", "application/vnd.github+json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    snprintf(_err, sizeof(_err), "GitHub-API: HTTP %d (%s)", code, repo);
    logf("[NET] Fehler: %s\n", _err);
    return false;
  }
  String body = http.getString();
  http.end();

  String tag;
  if (!jsonFind(body, "tag_name", 0, tag) || tag[0] != 'v') {
    setError("Kein Release-Tag gefunden");
    return false;
  }
  unsigned maj, min, pat;
  if (sscanf(tag.c_str() + 1, "%u.%u.%u", &maj, &min, &pat) != 3) {
    setError("Tag-Format unbekannt");
    return false;
  }

  // First browser_download_url whose file name starts with the prefix.
  int pos = 0;
  String u;
  while (jsonFind(body, "browser_download_url", pos, u, &pos)) {
    const int slash = u.lastIndexOf('/');
    if (slash >= 0 && u.substring(slash + 1).startsWith(assetPrefix)) {
      strncpy(out.assetUrl, u.c_str(), sizeof(out.assetUrl) - 1);
      break;
    }
  }
  if (out.assetUrl[0] == '\0') {
    setError("Kein passendes .bin-Asset");
    return false;
  }

  out.ok = true;
  out.major = (uint8_t)maj;
  out.minor = (uint8_t)min;
  out.patch = (uint8_t)pat;
  logf("[NET] %s: v%u.%u.%u\n", repo, maj, min, pat);
  return true;
}

// Shared streaming download; sink returns false to abort. finishFn runs
// right after the transfer, BEFORE http.end()/TLS teardown - the sink
// must close its file there (the teardown closes foreign fds).
template <typename WriteFn, typename FinishFn>
static bool streamDownload(const char *url, char *err, size_t errLen,
                           NetUpdater::ProgressFn progress, size_t *totalOut,
                           WriteFn writeFn, FinishFn finishFn) {
  WiFiClientSecure client;
  client.setCACert(GITHUB_ROOT_CAS);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // asset -> CDN
  if (!http.begin(client, url)) {
    snprintf(err, errLen, "HTTP-Init fehlgeschlagen");
    logf("[NET] Fehler: %s\n", err);
    return false;
  }
  http.addHeader("User-Agent", "infinitag-config");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    finishFn();
    http.end();
    snprintf(err, errLen, "Download: HTTP %d", code);
    logf("[NET] Fehler: %s\n", err);
    return false;
  }

  const int len = http.getSize();  // -1 = unknown/chunked
  if (totalOut) *totalOut = len > 0 ? (size_t)len : 0;
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t done = 0;
  uint32_t lastData = millis();
  while (http.connected() && (len < 0 || done < (size_t)len)) {
    const size_t avail = stream->available();
    if (avail) {
      const int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf)
                                                               : avail);
      if (n <= 0) break;
      if (!writeFn(buf, (size_t)n)) {
        finishFn();
        http.end();
        snprintf(err, errLen, "Schreibfehler");
        logf("[NET] Fehler: %s\n", err);
        return false;
      }
      done += (size_t)n;
      lastData = millis();
      if (progress) progress(done, len > 0 ? (size_t)len : 0);
    } else {
      if (millis() - lastData > 15000) break;  // stalled
      delay(1);
    }
  }
  finishFn();  // close the sink while the connection still owns its fds
  http.end();
  if (len > 0 && done != (size_t)len) {
    snprintf(err, errLen, "Download unvollstaendig");
    logf("[NET] Fehler: %s (%u von %u Bytes)\n", err, (unsigned)done,
         (unsigned)len);
    return false;
  }
  logf("[NET] Download komplett: %u Bytes\n", (unsigned)done);
  return true;
}

bool NetUpdater::downloadToStore(const ReleaseInfo &rel, ImageStore &store,
                                 ProgressFn progress) {
  if (!store.uploadBegin()) {
    setError(store.resultText());
    return false;
  }
  size_t total = 0;
  const bool ok = streamDownload(
      rel.assetUrl, _err, sizeof(_err), progress, &total,
      [&](const uint8_t *d, size_t n) { return store.uploadWrite(d, n); },
      [&]() { store.uploadSync(); });
  if (!store.uploadEnd(ok)) {
    if (ok) setError(store.resultText());  // marker/FS problem
    return false;
  }
  return true;
}

bool NetUpdater::selfUpdate(const ReleaseInfo &rel, ProgressFn progress) {
  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    setError("OTA-Slot nicht verfuegbar");
    return false;
  }
  const bool ok = streamDownload(
      rel.assetUrl, _err, sizeof(_err), progress, nullptr,
      [&](const uint8_t *d, size_t n) {
        return Update.write(const_cast<uint8_t *>(d), n) == n;
      },
      []() {});  // Update.h writes to flash, no fd to protect
  if (!ok) {
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {  // validates image + switches boot partition
    Update.printError(Serial);
    setError("Image-Validierung fehlgeschlagen");
    return false;
  }
  logf("[NET] Self-Update geflasht, Boot-Slot gewechselt\n");
  return true;
}
