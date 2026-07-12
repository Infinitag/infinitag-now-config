#include "VersionMemo.h"

#include <Preferences.h>

#include "InfinitagNow.h"

static const char *NVS_NAMESPACE = "inow-config";
static const char *KEYS[2] = {"vmaxsta", "vmaxtgt"};

int VersionMemo::slot(uint8_t deviceType) {
  if (deviceType == inow::DEV_STATION) return 0;
  if (deviceType == inow::DEV_TARGET) return 1;
  return -1;
}

void VersionMemo::load() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  _max[0] = prefs.getULong(KEYS[0], 0);
  _max[1] = prefs.getULong(KEYS[1], 0);
  prefs.end();
}

void VersionMemo::note(uint8_t deviceType, uint32_t versionKey) {
  const int s = slot(deviceType);
  if (s < 0 || versionKey <= _max[s]) return;
  _max[s] = versionKey;
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putULong(KEYS[s], versionKey);
  prefs.end();
}

uint32_t VersionMemo::maxKey(uint8_t deviceType) const {
  const int s = slot(deviceType);
  return s < 0 ? 0 : _max[s];
}

void VersionMemo::clear() {
  _max[0] = _max[1] = 0;
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.remove(KEYS[0]);
  prefs.remove(KEYS[1]);
  prefs.end();
}
