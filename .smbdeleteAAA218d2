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
| Funkprotokoll (Lib `InfinitagNow` + `PROTOCOL.md`) | Repo `infinitag-now-core` – wird via `lib_deps`-Git-URL gezogen |
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

## Stand (2026-07-09)

- **V0.1 komplett geschrieben, aber noch NIE auf echter Hardware
  gelaufen.** Hardware liegt bereit; nächster Schritt ist der
  Steckbrett-Bring-up nach `docs/20-configbox-steckbrett.md` (6 Stufen).
- Kein Gegenüber im Funknetz: Station/Target sprechen das Protokoll noch
  nicht. Für Funktests wird eine **Stub-Firmware** für einen zweiten ESP
  gebraucht (antwortet auf DISCOVER_REQ/IDENTIFY/CFG_WRITE) – geplant als
  Grundstein des künftigen `infinitag-now-target`-Repos.

## Nächste Schritte (Reihenfolge sinnvoll)

1. Steckbrett-Bring-up (Doc 20) – Verifikationspunkte dort abhaken,
   Erkenntnisse in Doc 18/20 nachtragen.
2. Stub-/Target-Firmware (neues Repo `infinitag-now-target`), damit
   Discovery/Identify/CFG_WRITE end-to-end testbar wird.
3. Web-UI V0.2 (SoftAP, Doc 18 § 9) – bewusst zurückgestellt.
4. Offen aus V0.1: Live-Monitor-Filter (K3), Sniffer, OTA.

## Konventionen

- **Protokolländerungen nur im Core-Repo** (Code + `PROTOCOL.md` + Tests
  in einem Commit). Regel: Protokollbruch = `version`-Byte erhöhen +
  Major-Tag; Geräte-Repos pinnen `lib_deps` auf Tags.
- Wissensbasis-Änderungen immer auf dem NAS-Master machen und in
  `infinitag-now/docs/` mitsyncen.
- Lizenz: CC BY-NC-SA 4.0 – Tobias Stewen.
