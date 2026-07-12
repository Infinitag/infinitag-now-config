// Device firmware image store on the box's LittleFS (Doc 21 Etappe 1).
//
// Holds ONE image at a time ("/img/station.bin" OR "/img/target.bin" -
// the 1.5 MB FS cannot fit two; a new upload replaces whatever is
// stored). Uploads stream into a temp file while scanning for
// the INOW_FW_MARKER; only files with a valid marker are accepted and
// moved to their type's slot. The image versions feed the '^' marker
// (stage 2) and are the payload source for the ESP-NOW push (Etappe 3).

#pragma once
#include <Arduino.h>
#include <FS.h>

struct ImageInfo {
  bool present = false;
  uint8_t deviceType = 0;  // inow::DeviceType
  uint8_t major = 0, minor = 0, patch = 0;
  size_t size = 0;
};

class ImageStore {
 public:
  // Mounts LittleFS and scans existing images. False = FS unusable.
  bool begin();

  const ImageInfo &info(uint8_t deviceType) const;
  uint32_t versionKey(uint8_t deviceType) const;  // 0 = no image
  bool remove(uint8_t deviceType);
  static const char *path(uint8_t deviceType);  // nullptr for other types

  // Streaming upload target (wired into WebUpdateService::StoreHooks).
  bool uploadBegin();
  bool uploadWrite(const uint8_t *data, size_t len);
  bool uploadEnd(bool ok);  // verify marker, move into the type's slot
  const char *resultText() const { return _result; }

 private:
  // Scan a stored file for the marker (used at begin() for existing files).
  bool scanFile(const char *p, ImageInfo &out);
  void resetScan();
  void feedScan(const uint8_t *data, size_t len);
  ImageInfo *slot(uint8_t deviceType);

  ImageInfo _station, _target;
  File _tmp;
  char _result[64] = "";

  // rolling marker scan across chunk boundaries
  uint8_t _carry[16];
  size_t _carryLen = 0;
  bool _markerFound = false;
  uint8_t _markerType = 0, _markerMaj = 0, _markerMin = 0, _markerPat = 0;
};
