# FAR-500

Force/Angle Recorder — 500 Newton. Een meetopstelling die kracht en hoek meet,
weergeeft op een OLED en via Bluetooth Low Energy uitgelezen en gekalibreerd
kan worden vanaf een laptop.

## Online UI

De laptop-UI (`FAR-500.html`) is een losstaande webpagina die via
[Web Bluetooth](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
rechtstreeks met het device verbindt — geen server, geen installatie.

**Live:** https://studiotijn.github.io/far-500/ (GitHub Pages)

Web Bluetooth vereist een secure context (https) en wordt alleen ondersteund
door Chromium-browsers (Chrome, Edge) — niet door Firefox of Safari.

## Architectuur

```
FAR-500 v5.1 — Force/Angle datalogger HUB
Target: ESP32-C6 SuperMini (TinyTronics) — 18650

  I2C  -> ADXL345 inclinometer  (hoek X/Y/Z, kalibreerbaar via UI)
  I2C  -> 1.3" OLED SH1106      (hoek / kracht / BT / accu)
  UART -> Sauter FH 500         (extern krachtmeetinstrument, poll met "9")
  ADC  -> 18650 spanning -> %
  BLE server -> laptop-UI       (stream "ms,deg,N,bat,...", commando's)
  LittleFS                      (gecombineerde CSV /log.csv)

Aan/uit = schuifschakelaar in 18650+ lijn (geen firmware nodig).
```

## Structuur

```
FAR-500_ESP32C6/     PlatformIO-project (env esp32-c6-devkitm-1), sketch in src/
FAR-500.html         Laptop-UI (Web Bluetooth), ook gehost via GitHub Pages
ARCHIEF/             Oude v2-bestanden (vóór de FAR-500-naamgeving)
CLAUDE.md            Projectcontext + upload-log voor AI-assisted development
```

## Firmware bouwen & uploaden

Vereist: [PlatformIO](https://platformio.org/), board-package
`esp32 by Espressif >= 3.0.x` ("ESP32C6 Dev Module", USB CDC On Boot = On).

Libraries (via PlatformIO Library Manager):
- `NimBLE-Arduino` (h2zero) — v2.x, getest met 2.5.0
- `U8g2` (olikraus)

```
pio run -d FAR-500_ESP32C6 -t upload
```

> **Windows path-length probleem:** de standaard PlatformIO core dir
> (`~/.platformio`) kan samen met diep geneste esp32-arduino-libs bestanden
> Windows' 260-tekens pad-limiet overschrijden. Workaround zonder
> adminrechten: zet `$env:PLATFORMIO_CORE_DIR = "C:\pio"` voor het
> `pio run`-commando. Zie `CLAUDE.md` voor het volledige upload-log.

## Status

- Optimalisatie van de UI
- Bouwen / valideren van hoek-justering (3D-vector-kalibratie)

## Licentie

Nog geen licentie toegevoegd — alle rechten voorbehouden totdat dit wordt
aangepast.
