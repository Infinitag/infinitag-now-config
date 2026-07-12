# CLAUDE.md – infinitag-now-config

Firmware der **Config-Box**: Handheld-Konfigurator für alle Geräte des
Infinitag-Now-Systems (infrastrukturloses ESP-NOW-Setup, Halloween-
Schießbude von Tobias Stewen – **Solo-Projekt**, komplette Neuentwicklung,
nur der Namenskosmos stammt vom klassischen Infinitag).

## Sprache & Stil

- **Antworten und Doku auf Deutsch**, Code-Kommentare auf Englisch.
- Tobias ist Hobby-Elektroniker und will mitdenken: Optionen vorschlagen,
  nicht nur ausführen. Größere Änderungen erst absprechen.

## Wo was liegt

| Was | Wo |
|---|---|
| Diese Firmware | dieses Repo (`github.com/Infinitag/infinitag-now-config`, privat) |
| Funkprotokoll (Lib `InfinitagNow` + `EspNowService` + `WebUpdateService` + `PROTOCOL.md`) | Repo `infinitag-now-core` – via `lib_deps`-**Symlink** auf die lokale Arbeitskopie (GitHub-Tag als Alternative in der `platformio.ini` kommentiert) |
| System-Doku (Wissensbasis) | Repo `infinitag-now` unter `docs/` – **Master liegt auf dem NAS**: `/Volumes/Basteln/Infinitag/wissensbasis/` (Kopie im Repo bei Änderungen mitsyncen!) |
| Projekt-Leitlinie | `/Volumes/Basteln/Infinitag/CLAUDE.md` |
| Lokale Arbeitskopien aller Repos | `/Volumes/Basteln/Infinitag/repos/` |

**Maßgebliche Doku für dieses Gerät:** `docs/18-config-tool.md` (Konzept,
Bedienung, GPIO-Plan v3) und `docs/20-configbox-steckbrett.md`
(Verdrahtung, Bring-up, Troubleshooting) im `infinitag-now`-Repo bzw. der
NAS-Wissensbasis. Bei Widerspruch Code ↔ Doku: nicht stillschweigend
angleichen, sondern ansprechen.

## Build & Test

```bash
pio run -e configbox              # bauen (ESP32-C3 Super Mini)
pio run -e configbox -t upload    # flashen (native USB-CDC)
pio device monitor                # 115200 Baud
```

Protokoll-Tests laufen im Core-Repo (`pio test -e native`).

**Wichtig:** `platform = espressif32@^6.7.0` (Arduino-Core 2.x) ist
bewusst gepinnt – die `esp_now_register_recv_cb`-Signatur ändert sich in
Core 3.x. Nicht ungefragt hochziehen.

## Architektur (alles in `src/`)

`main.cpp` (Setup + Loop) → `InputController` (Encoder-Quadratur per
Interrupt – **kein PCNT auf dem C3!** – plus 5 Tasten entprellt),
`EspNowService` (Init, Peer-LRU, RX-Ringpuffer, CRC-Check),
`DeviceRegistry` (Discovery-Ergebnis, stateless, max. 32),
`UiController` (Screen-State-Machine + U8g2-Rendering, größtes Modul),
`Config.h` (Pins/Timing – GPIO-Plan v3: Encoder 0/1/21, VBAT 3, K1–K4 =
4/5/10/20, I²C 6/7; keine Status-LED, Feedback nur übers OLED).

## Stand (2026-07-12, FW 0.2.0)

- **Protokoll v0x02** (Core-Tag `v2.0.0`): keine Geräte-IDs mehr, MAC ist
  die Identität (Listen zeigen MAC-Suffixe), Setup-Flow entfallen. Neu:
  Geräte-Update per `UPDATE_BEGIN` (Geräte-Menü → „Update (OTA)"),
  eigener SoftAP-Update-Modus (Tools → „Update-Modus", mit VBAT-Check)
  und Versions-Check `^` in den Listen. Encoder-Richtung über
  `cfg::ENC_DIRECTION` invertierbar.
- **Noch NIE auf echter Hardware gelaufen.** Hardware liegt bereit;
  nächster Schritt ist der Steckbrett-Bring-up nach
  `docs/20-configbox-steckbrett.md` (6 Stufen). Das erste Flashen der
  OTA-fähigen Firmware geht noch per USB.
- Funk-Gegenüber: die **Station** (`infinitag-now-station`) spricht das
  Protokoll vollständig; Target-Firmware (`infinitag-now-target`) fehlt
  noch.

## Nächste Schritte (Reihenfolge sinnvoll)

1. Steckbrett-Bring-up (Doc 20) – Verifikationspunkte dort abhaken,
   Erkenntnisse in Doc 18/20 nachtragen.
2. OTA-Praxistest (Box + Station), danach IDF-Rollback-Punkt aus
   Doc 18 § 12 angehen.
3. Target-Firmware (neues Repo `infinitag-now-target`).
4. Web-UI V0.2 (SoftAP, Doc 18 § 9) – bewusst zurückgestellt.
5. Offen aus V0.1: Live-Monitor-Filter (K3), Sniffer.

## Firmware-Versionen & Releases (seit 2026-07-12)

- **Versionen entstehen BEWUSST, nie automatisch.** Claude zählt
  `cfg::FW_*` in `src/Config.h` NICHT eigenmächtig bei Änderungen hoch,
  sondern schlägt vor, wenn ein Stand release-würdig ist, und fragt nach.
  Zwischenstände beim Basteln behalten die Version.
- **Release-Prozess:** Version in `Config.h` erhöhen → committen →
  `bash release.sh`. Das Skript baut, taggt `vX.Y.Z`, pusht und erstellt
  ein **GitHub-Release mit `infinitag-config-vX.Y.Z.bin` als Download**
  – so ist zu jeder Version die passende Firmware-Datei archiviert
  (fürs SoftAP-Update einfach vom Release herunterladen).

## Konventionen

- **Protokolländerungen nur im Core-Repo** (Code + `PROTOCOL.md` + Tests
  in einem Commit). Regel: Protokollbruch = `version`-Byte erhöhen +
  Major-Tag (aktuell 0x02 ↔ `v2.0.0`). Die Geräte-Repos binden den Core
  als **Symlink auf die lokale Arbeitskopie** ein – immer aktueller
  Stand, kein libdeps-Cache-Drift; der GitHub-Tag steht als Alternative
  kommentiert in der `platformio.ini`.
- Wissensbasis-Änderungen immer auf dem NAS-Master machen und in
  `infinitag-now/docs/` mitsyncen.
- Lizenz: CC BY-NC-SA 4.0 – Tobias Stewen.
