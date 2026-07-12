#include "ImageStore.h"

#include <LittleFS.h>
#include <fcntl.h>
#include <unistd.h>

#include "FwMarker.h"
#include "InfinitagNow.h"
#include "WebLog.h"

static const char *TMP_PATH = "/img/upload.tmp";
// Full VFS path (LittleFS default mount point) for the raw POSIX file
// API - the upload bypasses Arduino File/stdio on purpose: the stdio
// buffer layer lost the final partial block of large streams silently
// (File::close() is void), raw write/fsync/close report every error.
static const char *TMP_VFS_PATH = "/littlefs/img/upload.tmp";

// Assembled at runtime from the split prefix so this binary does not
// contain the contiguous marker pattern (it would match itself).
static uint8_t MARKER_PREFIX[8];
static bool markerPrefixInit() {
  memcpy(MARKER_PREFIX, INOW_FW_MARKER_PREFIX_A, 4);
  memcpy(MARKER_PREFIX + 4, INOW_FW_MARKER_PREFIX_B, 4);
  return true;
}

const char *ImageStore::path(uint8_t deviceType) {
  if (deviceType == inow::DEV_STATION) return "/img/station.bin";
  if (deviceType == inow::DEV_TARGET) return "/img/target.bin";
  return nullptr;
}

ImageInfo *ImageStore::slot(uint8_t deviceType) {
  if (deviceType == inow::DEV_STATION) return &_station;
  if (deviceType == inow::DEV_TARGET) return &_target;
  return nullptr;
}

const ImageInfo &ImageStore::info(uint8_t deviceType) const {
  static const ImageInfo kEmpty;
  if (deviceType == inow::DEV_STATION) return _station;
  if (deviceType == inow::DEV_TARGET) return _target;
  return kEmpty;
}

uint32_t ImageStore::versionKey(uint8_t deviceType) const {
  const ImageInfo &i = info(deviceType);
  if (!i.present) return 0;
  return ((uint32_t)i.major << 16) | ((uint32_t)i.minor << 8) | i.patch;
}

bool ImageStore::begin() {
  static const bool init = markerPrefixInit();
  (void)init;
  if (!LittleFS.begin(true)) {
    logf("[IMG] LittleFS nicht mountbar!\n");
    return false;
  }
  LittleFS.mkdir("/img");
  LittleFS.remove(TMP_PATH);  // stale temp from an interrupted upload

  for (uint8_t t : {(uint8_t)inow::DEV_STATION, (uint8_t)inow::DEV_TARGET}) {
    ImageInfo probe;
    if (LittleFS.exists(path(t)) && scanFile(path(t), probe) &&
        probe.deviceType == t) {
      *slot(t) = probe;
      logf("[IMG] Image Typ %u: v%u.%u.%u (%u Bytes)\n", t, probe.major,
           probe.minor, probe.patch, (unsigned)probe.size);
    }
  }
  return true;
}

bool ImageStore::remove(uint8_t deviceType) {
  ImageInfo *s = slot(deviceType);
  if (!s || !s->present) return false;
  LittleFS.remove(path(deviceType));
  *s = ImageInfo{};
  return true;
}

// --- rolling marker scan ------------------------------------------------------

void ImageStore::resetScan() {
  _carryLen = 0;
  _markerFound = false;
}

void ImageStore::feedScan(const uint8_t *data, size_t len) {
  if (_markerFound) return;
  // Work on carry + chunk so markers spanning chunk borders are found.
  // INOW_FW_MARKER_LEN is 13; carry keeps the last 12 bytes.
  uint8_t *buf = _scanBuf;
  while (len > 0) {
    const size_t take = len > 1460 ? 1460 : len;
    memcpy(buf, _carry, _carryLen);
    memcpy(buf + _carryLen, data, take);
    const size_t n = _carryLen + take;
    for (size_t i = 0; i + INOW_FW_MARKER_LEN <= n; i++) {
      if (memcmp(buf + i, MARKER_PREFIX, 8) == 0 &&
          buf[i + 12] == '@') {
        _markerFound = true;
        _markerType = buf[i + 8];
        _markerMaj = buf[i + 9];
        _markerMin = buf[i + 10];
        _markerPat = buf[i + 11];
        return;
      }
    }
    _carryLen = n < 12 ? n : 12;
    memcpy(_carry, buf + n - _carryLen, _carryLen);
    data += take;
    len -= take;
  }
}

bool ImageStore::scanFile(const char *p, ImageInfo &out) {
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  resetScan();
  uint32_t crc = 0;
  uint8_t buf[1024];
  while (f.available()) {  // full read: marker AND file crc
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    feedScan(buf, (size_t)n);
    crc = inow::crc32(crc, buf, (size_t)n);
  }
  const size_t size = f.size();
  f.close();
  if (!_markerFound) return false;
  out.crc = crc;
  out.present = true;
  out.deviceType = _markerType;
  out.major = _markerMaj;
  out.minor = _markerMin;
  out.patch = _markerPat;
  out.size = size;
  return true;
}

// --- streaming upload (StoreHooks) ---------------------------------------------

void ImageStore::wipeAll() {
  _station = ImageInfo{};
  _target = ImageInfo{};
  logf("[IMG] Formatiere FS (%u KB belegt)\n",
       (unsigned)(LittleFS.usedBytes() / 1024));
  LittleFS.format();  // box FS holds nothing but the image store
  LittleFS.mkdir("/img");
  logf("[IMG] Format fertig, %u KB belegt\n",
       (unsigned)(LittleFS.usedBytes() / 1024));
}

bool ImageStore::uploadBegin() {
  // Single-slot store: the 1.5 MB FS holds one image at a time - drop
  // everything stored so tmp + old image never exceed the partition.
  // Remove by PATH, not via remove(): after a broken write a file can
  // exist on disk while its slot is empty (boot scan found no marker)
  // and would silently eat the partition forever.
  _station = ImageInfo{};
  _target = ImageInfo{};
  LittleFS.remove(path(inow::DEV_STATION));
  LittleFS.remove(path(inow::DEV_TARGET));
  LittleFS.remove(TMP_PATH);
  // Store is empty now; substantial usage left = orphaned blocks from a
  // corrupt entry that remove() cannot reach -> only a format helps.
  if (LittleFS.usedBytes() > 64 * 1024) wipeAll();
  LittleFS.mkdir("/img");

  _fd = ::open(TMP_VFS_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (_fd < 0) {
    logf("[IMG] open fehlgeschlagen (errno %d)\n", errno);
    snprintf(_result, sizeof(_result), "Speicher voll oder FS-Fehler");
    return false;
  }
  _uploadActive = true;
  logf("[IMG] Upload beginnt (FS %u/%u KB belegt)\n",
       (unsigned)(LittleFS.usedBytes() / 1024),
       (unsigned)(LittleFS.totalBytes() / 1024));
  _rxBytes = 0;
  _upCrc = 0;
  resetScan();
  return true;
}

bool ImageStore::uploadWrite(const uint8_t *data, size_t len) {
  if (_fd < 0) return false;
  feedScan(data, len);
  _upCrc = inow::crc32(_upCrc, data, len);
  size_t done = 0;
  while (done < len) {
    const ssize_t n = ::write(_fd, data + done, len - done);
    if (n <= 0) {
      logf("[IMG] Schreibfehler bei %u Bytes (errno %d, FS %u/%u KB)\n",
           (unsigned)(_rxBytes + done), errno,
           (unsigned)(LittleFS.usedBytes() / 1024),
           (unsigned)(LittleFS.totalBytes() / 1024));
      return false;
    }
    done += (size_t)n;
  }
  _rxBytes += len;
  return true;
}

// Sync + close with visible errors (Arduino File::close() swallows
// them). MUST run before the transport is torn down: the TLS/socket
// cleanup was seen closing foreign fds (errno 9 on our fsync/close) -
// close early so a recycled fd number can never be ours.
void ImageStore::uploadSync() {
  if (_fd < 0) return;
  if (fsync(_fd) != 0) logf("[IMG] fsync-Fehler (errno %d)\n", errno);
  if (::close(_fd) != 0) logf("[IMG] close-Fehler (errno %d)\n", errno);
  _fd = -1;
}

bool ImageStore::uploadEnd(bool ok) {
  if (!_uploadActive) return false;
  _uploadActive = false;
  uploadSync();
  // Verify the on-disk size against the byte count from uploadWrite -
  // the last partial block must not get lost silently.

  if (!ok || !_markerFound) {
    LittleFS.remove(TMP_PATH);
    snprintf(_result, sizeof(_result), "Kein Infinitag-Marker gefunden");
    return false;
  }
  const char *dst = path(_markerType);
  if (!dst) {
    LittleFS.remove(TMP_PATH);
    snprintf(_result, sizeof(_result), "Unbekannter Geraetetyp %u",
             _markerType);
    return false;
  }

  const size_t size = _rxBytes;
  File chk = LittleFS.open(TMP_PATH, "r");
  const size_t onDisk = chk ? chk.size() : 0;
  chk.close();
  if (onDisk != size) {
    LittleFS.remove(TMP_PATH);
    snprintf(_result, sizeof(_result), "FS-Fehler: %u statt %u Bytes",
             (unsigned)onDisk, (unsigned)size);
    return false;
  }

  LittleFS.remove(dst);
  if (!LittleFS.rename(TMP_PATH, dst)) {
    LittleFS.remove(TMP_PATH);
    snprintf(_result, sizeof(_result), "FS-Fehler beim Ablegen");
    return false;
  }
  ImageInfo *s = slot(_markerType);
  s->present = true;
  s->deviceType = _markerType;
  s->major = _markerMaj;
  s->minor = _markerMin;
  s->patch = _markerPat;
  s->size = size;
  s->crc = _upCrc;
  _lastType = _markerType;
  snprintf(_result, sizeof(_result), "%s v%u.%u.%u (%u KB) gespeichert",
           _markerType == inow::DEV_STATION ? "Station" : "Target",
           _markerMaj, _markerMin, _markerPat, (unsigned)(size / 1024));
  logf("[IMG] %s\n", _result);
  return true;
}
