# infinitag-now-config

Firmware der **Config-Box** – des Handheld-Konfigurators für alle
Infinitag-Now-Geräte (Stationen & Targets) im ESP-NOW-Netz.

Konzept & Bedienung: Repo [`infinitag-now`](https://github.com/Infinitag/infinitag-now),
`docs/18-config-tool.md`. Funkprotokoll: Repo
[`infinitag-now-core`](https://github.com/Infinitag/infinitag-now-core)
(wird als PlatformIO-Lib über `lib_deps` eingebunden – privates Repo,
lokales Git braucht Org-Zugriff).

**Stand: V0.1** (2026-07-08) – Kern ohne Web-UI, noch nie auf echter
Hardware gelaufen (Lochraster-Prototyp steht aus).

## Hardware

ESP32-C3 Super Mini (IPEX + ext. Antenne), 0,96"-OLED SSD1315 mit
4-Tasten-Board (I²C), KY-040-Encoder, 1× SK6812RGBW, 4×AA über
Schiebeschalter + 1N5817 an 5V-Pin. Pinbelegung: `src/Config.h`.

## Bauen & Flashen

```bash
pio run -e configbox              # bauen
pio run -e configbox -t upload    # flashen (USB-C, native USB-CDC)
pio device monitor                # Log ansehen
```

## Struktur

| Pfad | Inhalt |
|---|---|
| `src/Config.h` | Pins, Kanal, Timing-Konstanten, FW-Version |
| `src/InputController.*` | Encoder (Interrupt-Quadratur, kein PCNT auf C3) + 5 Tasten mit Entprellung |
| `src/EspNowService.*` | ESP-NOW-Init, Peer-LRU, RX-Ringpuffer, CRC-Validierung |
| `src/DeviceRegistry.*` | Geräteliste des letzten Discovery-Zyklus (stateless, max. 32) |
| `src/UiController.*` | Menü-State-Machine + OLED-Rendering |
| `src/StatusLed.*` | SK6812-Farbcodes (lila = Setup, blau = TX, grün = OK, rot = Fehler) |

## Umgesetzt in V0.1

Discovery + Geräteliste (RSSI, `??` bei ID 0, `!` bei ID-Duplikat),
Identify-Blink (500-ms-Refresh, K3 an/aus), Feld-Editor mit CFG_WRITE →
CFG_ACK (Timeout 800 ms), „Neue Station" (SETUP_BEGIN/SETUP_TAKE, 60 s),
Sound-Test (0x32 `CFG_TEST_SOUND`), Live-Monitor (HIT_REPORTs), Firmware-Info.

## Bewusst noch offen (V0.2+)

Web-UI/SoftAP inkl. `sounds.json`, Live-Monitor-Filter, Sniffer, OTA.
Gegenstellen (Station/Target) sprechen das Protokoll noch nicht – zum
Testen einen zweiten ESP mit Stub-Firmware bespielen.

## Bedienung

Encoder drehen = navigieren / Wert ändern (K2 halten = ×10),
Push/K4 = OK, K1 = zurück/abbrechen, K3 = Identify-Blink an/aus,
K2 kurz in Listen = Discovery neu anstoßen.

## Lizenz

CC BY-NC-SA 4.0 – Tobias Stewen.
