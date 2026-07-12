#!/usr/bin/env bash
# Firmware-Release der Config-Box: baut, taggt vX.Y.Z, pusht und erstellt
# ein GitHub-Release mit der firmware.bin als Download-Asset.
#
# Aufruf:  bash release.sh
# Version: kommt aus src/Config.h (cfg::FW_MAJOR/MINOR/PATCH) – vorher
#          BEWUSST hochzaehlen und committen (siehe CLAUDE.md).
set -euo pipefail
cd "$(dirname "$0")"

export PATH="$HOME/.platformio/penv/bin:$PATH"

# --- Version aus den Quellen lesen -------------------------------------------
MAJOR=$(sed -n 's/.*FW_MAJOR = \([0-9]*\).*/\1/p' src/Config.h)
MINOR=$(sed -n 's/.*FW_MINOR = \([0-9]*\).*/\1/p' src/Config.h)
PATCH=$(sed -n 's/.*FW_PATCH = \([0-9]*\).*/\1/p' src/Config.h)
VER="v${MAJOR}.${MINOR}.${PATCH}"
ASSET="infinitag-config-${VER}.bin"

# --- Vorbedingungen -----------------------------------------------------------
if [[ -n "$(git status --porcelain)" ]]; then
  echo "FEHLER: Working tree nicht sauber – erst committen." >&2
  exit 1
fi
if git rev-parse "$VER" >/dev/null 2>&1; then
  echo "FEHLER: Tag $VER existiert schon – erst FW_PATCH in src/Config.h erhoehen." >&2
  exit 1
fi

# --- Bauen ---------------------------------------------------------------------
echo "== Baue Config-Box $VER =="
pio run -e configbox
BIN=".pio/build/configbox/firmware.bin"
[[ -f "$BIN" ]] || { echo "FEHLER: $BIN nicht gefunden." >&2; exit 1; }
mkdir -p dist
cp "$BIN" "dist/$ASSET"

# --- Taggen, pushen, Release ---------------------------------------------------
PREV=$(git describe --tags --abbrev=0 2>/dev/null || true)
if [[ -n "$PREV" ]]; then
  NOTES=$(git log --oneline "${PREV}..HEAD" | sed 's/^/- /')
else
  NOTES=$(git log --oneline -10 | sed 's/^/- /')
fi

git tag "$VER"
git push origin main --tags
gh release create "$VER" "dist/$ASSET" \
  --title "Config-Box $VER" \
  --notes "Firmware-Update per SoftAP: Tools → Update-Modus, dann ${ASSET} auf http://192.168.4.1 hochladen.

Aenderungen:
${NOTES}"

echo "== Release $VER erstellt =="
gh release view "$VER" --json url -q .url
