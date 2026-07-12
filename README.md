# Infinitag Now – Config-Box

**Die Fernbedienung des Zauberstab-Spiels:** Ein Handheld mit OLED und
Drehencoder, der alle Infinitag-Now-Geräte per Funk findet, einstellt,
testet und aktualisiert – draußen im Einsatz genauso wie am Basteltisch,
ganz ohne WLAN-Router, App oder Laptop.

![Plattform](https://img.shields.io/badge/Plattform-ESP32--C3-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20PlatformIO-orange)
![Funk](https://img.shields.io/badge/Funk-ESP--NOW-purple)
![Lizenz](https://img.shields.io/badge/Lizenz-PolyForm%20NC%201.0.0-lightgrey)

<!-- TODO: Hero-Foto der Config-Box einfügen:
     ![Infinitag-Config-Box](docs/config-box.jpg) -->

## Features

- **Geräte finden statt einrichten:** Ein Discovery-Broadcast listet
  alle Stationen und Targets in Reichweite (MAC-Kennung + Signalstärke);
  das markierte Gerät blinkt weiß – man sieht sofort, welches gemeint ist
- **Alles einstellbar:** Lautstärke, Stab-Statusfarben, Ziel-Zuordnung,
  Trefferzeiten & Co. – Werte werden direkt ins Gerät geschrieben und
  dort dauerhaft gespeichert
- **Testen aus der Ferne:** Sound-Vorhören und kompletter
  Stations-Selbsttest (Sound, LEDs, Laser, IR mit Selbstempfang,
  Trigger) auf Knopfdruck
- **Updates über die Luft:** Schickt jedes Gerät in den
  SoftAP-Update-Modus, meldet den Erfolg samt neuer Version zurück und
  markiert veraltete Firmware in der Liste (`^`); die eigene Firmware
  aktualisiert sich über denselben Weg
- **Live-Monitor:** Zeigt eingehende Treffer-Meldungen in Echtzeit –
  praktisch beim Aufbau und bei der Fehlersuche
- **Batteriebetrieb:** 4×AA, Batteriespannung im Tools-Menü; vor einem
  Update wird der Akkustand geprüft

## Das Infinitag-Now-System

| Gerät | Repo | Aufgabe |
|---|---|---|
| **Config-Box** | dieses Repo | Handheld-Konfigurator: Discovery, Einstellungen, Updates, Live-Monitor |
| **Station** | [infinitag-now-station](https://github.com/Infinitag/infinitag-now-station) | Sound + Zauberstab (Zauber-Auslösung, Laser, Status-LEDs) |
| **Targets** | infinitag-now-target *(in Arbeit)* | IR-Empfänger an den Zielen, melden Treffer per Funk |
| Protokoll-Lib | [infinitag-now-core](https://github.com/Infinitag/infinitag-now-core) | Paketformat, `EspNowService`, SoftAP-Updater, `PROTOCOL.md` |
| Doku | [infinitag-now](https://github.com/Infinitag/infinitag-now) | Wissensbasis (Hardware, Protokoll, Konzepte) |

Geräte identifizieren sich allein über ihre MAC-Adresse – auspacken,
einschalten, „Neu suchen", fertig. Kein Pairing, keine ID-Vergabe.

## Hardware

ESP32-C3 Super Mini (IPEX + externe Antenne), 0,96"-OLED SSD1315 mit
4-Tasten-Board (I²C), KY-040-Drehencoder, 4×AA-Batteriefach mit
Schiebeschalter, Batteriemessung per Spannungsteiler. Pinbelegung in
`src/Config.h` (GPIO-Plan v3); Konzept, Bedienung und Schaltung in
Doc 18 der [Wissensbasis](https://github.com/Infinitag/infinitag-now).

## Loslegen

```bash
pio run -e configbox              # bauen
pio run -e configbox -t upload    # flashen (USB-C, native USB-CDC)
pio device monitor                # Log, 115200 Baud
```

Nur das allererste Flashen braucht USB – danach kommen Updates über
die Luft (siehe unten).

## Bedienung

Alles geht mit dem Encoder allein: drehen = navigieren oder Wert
ändern (schnell drehen = ×10), drücken = OK. Jede Menüebene hat einen
„< Zurück"-Eintrag. Die vier OLED-Tasten sind optionale Shortcuts:
K1 = zurück, K2 = neu suchen / ×10 (halten), K3 = Identify-Blinken
an/aus, K4 = OK.

## Updates

Jede Version gibt es als [GitHub-Release](../../releases) mit fertiger
`infinitag-config-vX.Y.Z.bin`. Einspielen ohne Kabel:

1. Tools → **„Update-Modus"** (prüft vorher den Batteriestand)
2. Die Box öffnet ein WLAN `infinitag-cfg-XXXXXX`
3. Mit Laptop/Handy verbinden, `.bin` auf `http://192.168.4.1` hochladen
4. Die Box prüft das Image und startet mit der neuen Version neu

Die Upload-Seite zeigt an, welches Gerät man vor sich hat, und lehnt
falsch benannte Firmware-Dateien ab. Ein abgebrochener Upload kann
nicht booten – die alte Firmware bleibt aktiv.

## Entwicklung

| Pfad | Inhalt |
|---|---|
| `src/Config.h` | Pins, Funkkanal, Timing, FW-Version |
| `src/InputController.*` | Encoder (Interrupt-Quadratur) + 5 Tasten entprellt |
| `src/DeviceRegistry.*` | Geräteliste des letzten Discovery-Zyklus (stateless) |
| `src/UiController.*` | Menü-State-Machine + OLED-Rendering (größtes Modul) |

Änderungen laufen über Pull Requests (Squash-Merge, Typ-Label –
Template liegt in `.github/`). Releases entstehen bewusst über
`release.sh` (baut, taggt, erstellt das GitHub-Release inkl. `.bin`).
Protokolländerungen gehören ins Core-Repo. Arduino-Core 2.x ist
bewusst gepinnt (ESP-NOW-Callback-Signatur).

Geplant: Web-UI über SoftAP (Übersicht + Bearbeiten im Browser),
Live-Monitor-Filter, ESP-NOW-Sniffer.

## Lizenz

[PolyForm Noncommercial 1.0.0](LICENSE) – © 2026 Tobias Stewen.
Nicht-kommerzielle Nutzung, Änderung und Weitergabe sind erlaubt
(Urheber-Hinweis muss erhalten bleiben); **kommerzielle Nutzung nur mit
vorheriger Genehmigung** – Anfragen an info@hallow-tech.de.
Ursprung des Namens und der Idee: das Lasertag-Projekt
[Infinitag](https://github.com/Infinitag) (2017); Infinitag Now ist eine
komplette Neuentwicklung als Zauberstab-Spiel, entstanden für einen
Halloween-Zauberstand.
