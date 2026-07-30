# Project Context

## Doel
Bouw een meetopstelling die kracht en hoek meet, weergeeft op OLED en via BLE verbinding uit te lezen is en te justeren is.

## Architectuur

   FAR-500 v5.1  -  Force/Angle datalogger HUB
   Target : ESP32-C6 SuperMini (TinyTronics) - 18650 

   Architectuur (alles bedraad, 1 hub):
     - I2C  -> ADXL345 inclinometer  (hoek X,Y,Z-as, kantelhoek X-as fine-tuning (kalibreerbaar) via UI)
     - I2C  -> 1.3" OLED SH1106       (hoek / kracht / BT / accu)
     - UART -> Sauter FH 500          (poll met "9", parse teken + waarde)
     - ADC  -> 18650 spanning -> %
     - BLE server -> laptop           (stream "ms,deg,N,bat", commando's)
     - LittleFS                       (gecombineerde CSV /log.csv)
   Aan/uit = schuifschakelaar in 18650+ lijn (geen firmware nodig).

   Optioneel (los van de hub): `far500-upload-worker/` is een Cloudflare
   Worker die de "Naar GitHub"-knoppen in de laptop-UI bedient — zet een
   geüploade meting (.xlsx) door naar een GitHub Release-asset (tag
   `recordings` op DynteqBV/far-500). Reden: directe browser-upload naar
   GitHub kan niet (uploads.github.com heeft geen CORS, en de CORS-vriendelijke
   Actions-dispatch-triggers hebben een payloadlimiet van 64KB). Het
   GitHub-token blijft server-side in de Worker; de UI kent alleen een
   gedeeld wachtwoord. Zie `far500-upload-worker/README.md` voor deploy.
   **Belangrijk:** gebruik hiervoor een **classic PAT** (scope `public_repo`),
   geen fine-grained token — fine-grained PATs gaven bij het aanmaken van een
   release consequent `403 Resource not accessible by personal access token`,
   ook met de juiste "Contents: Read and write"-permissie en repository-access
   correct ingesteld (GitHub-gat, geen configuratiefout). Verder gebruikt de
   Worker de header `Authorization: token <PAT>` i.p.v. `Bearer` — met
   `Bearer` gaf GitHub `401 Bad credentials` op een geldige classic PAT.

   LIBRARIES (Library Manager):
     - "NimBLE-Arduino" (h2zero)  -> deze versie is voor v2.x (getest 2.5.0)
     - "U8g2" (oliver kraus)
   BOARD: esp32 by Espressif >= 3.0.x, "ESP32C6 Dev Module", USB CDC On Boot = On
   
## Huidige status
- Optimalisatie van UI
- Bouwen / valideren van hoek justering
- "Naar GitHub"-upload (Cloudflare Worker relay) toegevoegd aan de UI, gedeployed en end-to-end getest (2026-07-30) — werkt met een classic PAT (zie Architectuur-sectie voor het waarom)


## Belangrijke beslissingen
- Gebruik JWT authentication
- Geen Redux, alleen React Query

## Werkwijze
- Geef eerst een plan voordat je grote wijzigingen maakt.
- Pas bestaande architectuur niet aan zonder overleg.
- Houdt bij hoe (via welke COM-poort en software instellingen) je succesvol de firmware geupload hebt (in dit document CLAUDE.md)

## Naamgeving
- Project/device heet **FAR-500** (BLE naam, OLED-tekst, UI). Let op: "Sauter FH 500" in de architectuurbeschrijving is de naam van het externe krachtmeetinstrument (UART-bron) en is dus NIET hernoemd.
- PlatformIO-project: `FAR-500_ESP32C6/` (env `esp32-c6-devkitm-1`), sketch in `src/`.
- Oude v2-bestanden (vóór de FAR-500-naamgeving) staan in `ARCHIEF/`.

## Upload-log (firmware -> ESP32-C6)
- **2026-07-27**: `FAR-500_ESP32C6_v5.ino` succesvol geupload naar COM10 via PlatformIO (project: `FAR-500_ESP32C6/`, env `esp32-c6-devkitm-1`).
  - Sketch stond los in de projectroot en is gekopieerd naar `FAR-500_ESP32C6/src/`.
  - Commando: `pio run -d "C:\DEV\Claude\FAR-500_ESP32C6" -t upload`
  - **Bekend probleem (Windows path length)**: standaard PlatformIO core dir (`C:\Users\<user>\.platformio`) + de diep geneste esp32-arduino-libs/esp_matter bestanden overschrijden Windows' 260-char pad-limiet -> `FileNotFoundError` tijdens unpack. Fix zonder adminrechten: zet `$env:PLATFORMIO_CORE_DIR = "C:\pio"` voor het `pio run` commando (verkort het pad genoeg). Permanente fix (admin): Windows Long Path Support inschakelen via `New-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name 'LongPathsEnabled' -Value 1 -PropertyType DWORD -Force` + herstart.
  - Build resultaat: RAM 8.2%, Flash 62.6%. Board herkend als ESP32-C6FH4 (QFN32), MAC f0:f5:bd:2c:c9:18.
- **2026-07-27**: `FAR-500_ESP32C6_v5.1.ino` succesvol geupload naar COM10 (zelfde commando/project). `svc->start()` (deprecated NimBLE-aanroep) verwijderd -> build zonder warnings. Tijdelijke `SAUTER_DEBUG`-logging toegevoegd (raw bytes + regels op USB-Serial 115200) om de RS232/TTL-koppeling met de Sauter FH 500 te troubleshooten.
- **2026-07-30**: hoekmeting omgebouwd naar volledige 3D-vector-kalibratie (`ref0`/`axisV`/`gainV`) i.p.v. 1 raw as + scalaire offset/gain — het bestand stond op dat moment niet compilabel (halfafgemaakte refactor, verwees naar niet-gedefinieerde `offs`/`gain`/`calRaw0`/`MEAS_AXIS`). De justering (0 gr / 45 gr) legt nu de kantelas + nulvector als 3D-vector vast, zodat de hoek zuiver blijft ook bij een vaste montage-scheefstand (bv. 10 gr op de X-as). Live diagnosecijfer tijdens justeren toont ruwe Z-hoek i.p.v. X-hoek. Succesvol geupload naar COM10 met `$env:PLATFORMIO_CORE_DIR = "C:\pio"; & "$HOME\.platformio\penv\Scripts\pio.exe" run -d "C:\DEV\Claude\FAR-500_ESP32C6" -t upload` (plain `pio` niet op PATH in deze PowerShell-sessie, vandaar het volledige pad naar `pio.exe`).
- **2026-07-30** (vervolg): telemetrie-string uitgebreid met een 8e veld `logging` (0/1) zodat de laptop-UI (`FAR-500_UI_v7.html`) de werkelijke logstatus van het device kan volgen. Reden: stopte je de meting via de drukknop (i.p.v. de UI-knop), dan bleef de UI denken dat er nog gemeten werd en vulde hij de grafiek verder met `t=0`-samples (device stuurt `t=0` zodra `logging=false`) — dat plette de tijdas en liet de opname visueel "verdwijnen". De UI mirrort nu start/stop-transities van het device-veld, ook als die via de knop gebeuren. Zelfde upload-commando/COM10, build succesvol.
- **2026-07-30** (vervolg 2): meting-geschiedenis toegevoegd. Firmware: `measNum` (uint16, sessie-only, reset bij herstart) verhoogt bij elke START en wordt als `#MEAS,<nr>,<startms>`-markerregel in `/log.csv` geschreven, plus getoond op de OLED (vierkant rechtsboven, rechts uitgelijnd, naast de hoek). UI: nieuwe "Geschiedenis"-kaart importeert het volledige devicelog via de bestaande DUMP/pLog-weg, splitst op de `#MEAS`-markers, toont de laatste 50 metingen met een invulbaar commentaarveld per meting, en exporteert alles als één Excel-werkboek (overzichtblad + 1 tabblad per meting) via een uitgebreide (multi-sheet) versie van de zelfgeschreven XLSX/zip-writer. Ontwerpkeuzes (met gebruiker afgestemd): device blijft ongelimiteerd loggen (geen firmware-side pruning, "Wis device" blijft de manier om op te ruimen) — de cap van 50 zit alleen in de UI-weergave bij importeren; Excel-vorm is overzichtblad + los tabblad per meting i.p.v. 1 platte tabel. Zelfde upload-commando/COM10, build succesvol.

## Deploy-log (far500-upload-worker -> Cloudflare)
- **2026-07-30**: eerste deploy via `npx wrangler login` (OAuth-browserflow) + `npx wrangler deploy` vanuit `far500-upload-worker/`. Live op `https://far500-upload-worker.far500-upload-worker.workers.dev` (URL ingevuld in `GH_RELAY_URL` in `FAR-500.html`).
  - **wrangler.toml `compatibility_date`**: mag niet in de toekomst liggen (Cloudflare valideert tegen de echte kalenderdatum) — foutcode 10021 als dat wel zo is. Stond aanvankelijk op de (foutieve, toekomstige) systeemdatum; teruggezet naar `2024-09-23`.
  - **Secrets**: `UPLOAD_SECRET` (gedeeld wachtwoord voor de UI, willekeurig gegenereerd) en `GH_TOKEN` gezet via `wrangler secret put <NAAM>` (waarde non-interactief doorgepiped, i.p.v. de interactieve prompt — die werkt niet vanuit een niet-interactieve shell).
  - **GH_TOKEN moet een classic PAT zijn** (scope `public_repo`) — zie de opmerking bij Architectuur hierboven voor waarom een fine-grained PAT hier niet werkt.
  - Na de fixes (compatibility_date, `Authorization: token` i.p.v. `Bearer`, classic PAT) end-to-end getest met een curl-upload: asset kwam succesvol aan op `github.com/DynteqBV/far-500/releases/tag/recordings`.