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

   Optioneel (los van de hub): `far500-force-check/` is een Python
   build-tool die een meting-export (CSV/XLSX) toetst aan de
   bedienkracht-eisen (C1-C4: kracht vs. handvathoogte, breakaway-marge,
   max. bedienhoogte, snelheid/versnelling) en daar een opgemaakt
   XLSX-rapport (2 tabbladen: `setup_analyse` + `data`) + PDF (via CI) van
   maakt. Zie `far500-force-check/README.md` voor criteria/constanten/CLI.

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
- **Eerstvolgende prioriteit**: de meet-unit zelf valideren (hardware/meting).
- **2026-08-11** (eenmalige data-migratie): alle 9 bestaande metingen onder de
  `recordings`-release hadden de hoek in het oude (vóór-2026-08-06) teken
  vastgelegd en konden daardoor niet meer correct met de huidige (omgedraaide)
  conventie ingeladen/geanalyseerd worden. Op verzoek voor elk bestand een
  `..._flip.xlsx`-kopie gemaakt: `angle_deg` per sample geflipt (niet-nul
  waarden van teken gewisseld, 0 blijft 0) en, waar aanwezig, de
  kalibratie-hoeken "hoek hoog (deg)"/"hoek laag (deg)" in de metadata
  eveneens geflipt (puur informatief — de reeds berekende L/H-waarden waarmee
  de "Laden"-knop werkt zijn NIET herberekend en dus ongewijzigd, dat blijven
  geldige fysieke afmetingen). Overige kolommen (t_s, force_N, en de
  destijds-al-berekende arc_cm/speed/accel/handle_height, indien aanwezig)
  bewust ongewijzigd gelaten — die laatste vier worden door de "Laden"-knop
  toch nooit gelezen (alleen t_s/angle_deg/force_N + meta), dus blijven ze
  onschadelijk stil verouderd staan in de rauwe kolommen van dat ene tabblad.
  Uitgevoerd met een los Python-script (`zipfile`+`ElementTree`, niet
  gecommit — eenmalig, buiten de reguliere codebase) dat elk bestand
  downloadde via `gh release download recordings`, de hoekwaarden in
  `xl/worksheets/sheet1.xml` aanpaste, en de 9 nieuwe bestanden terugzette
  met `gh release upload recordings` (zelfde release, originelen ongemoeid
  gelaten). Gevalideerd op drie niveaus: (1) Python-vergelijking orig-vs-flip
  rij-voor-rij bevestigt dat alleen kolom B/D wijzigen en het teken correct
  omdraait; (2) het geflipte bestand door de daadwerkelijke
  `parseFarMetingWorkbook()`-lezer uit `FAR-500.html` gehaald (via jsdom) —
  laadt foutloos; (3) één geflipt bestand ook via de live
  `far500-upload-worker`-downloadproxy opgehaald en als geldig zip/XLSX
  bevestigd. Alle 18 assets (9 origineel + 9 `_flip`) staan nu naast elkaar
  onder `github.com/DynteqBV/far-500/releases/tag/recordings` en zijn dus
  beide zichtbaar in de "Oude meting laden"-dropdown.
- **2026-08-11**: bedienkracht-analyse (C1-C4) rechtstreeks in de laptop-UI
  toegevoegd, op verzoek. Drie knoppen i.p.v. één: de bestaande "Download
  XLSX" heet nu **"Meetgegevens XLSX"** (ongewijzigd gedrag — ruwe
  meetcellen), plus twee nieuwe (adv-only, vereisen L én H via "Handvat-
  posities → geometrie"): **"Rapportage + Analyse XLSX"** en **"Rapportage +
  Analyse PDF"**.
  - **Architectuurkeuze**: de UI heeft geen server/Python (`FAR-500.html` is
    een losstaande statische pagina, zie README "geen server, geen
    installatie") — de C1-C4-anker/envelope-logica uit
    `far500-force-check/src/far500_force_check/{constants,engine}.py` is
    daarom naar vanilla JS **gepoort** (nieuwe sectie "bedienkracht-analyse
    (C1-C4)" in `FAR-500.html`, functies `segmentMoves`/`findAnchor`/
    `buildEnvelope`/`runForceCheck`). Dit is nu **twee implementaties van
    dezelfde criteria** (Python voor CLI/CI, JS voor de UI) — hou ze in sync
    als de norm-interpretatie wijzigt; de JS-poort verwijst in commentaar
    terug naar de Python-bron.
  - **Rapport-weergave**: i.p.v. een "echte" (interactieve) Excel-grafiek
    hand-rollen in de al-bestaande zip/OOXML-writer, wordt het hele
    setup_analyse-rapport (titel/setup/criteria/PASS-FAIL-tabel/eindoordeel
    + envelope-grafiek + hoogte-grafiek) op **één canvas getekend en als
    JPEG gerasterd** — diezelfde afbeelding wordt zowel in de Excel-tab
    `setup_analyse` ingesloten (nieuwe OOXML-drawing/media-plumbing,
    `buildReportXlsxBlob()`) als in een **zelfgeschreven PDF** (één pagina,
    DCTDecode-JPEG-XObject, geen library, `buildAnalysisPdfBlob()`) — zo
    tonen Excel en PDF gegarandeerd hetzelfde. De `data`-tab blijft wel
    gewone/filterbare cellen (met extra kolommen region/limit_N/in_grace/
    force_ok t.o.v. de gewone meetgegevens-export).
  - **Getest**: volledige pijplijn (engine + canvas-render + XLSX-zip +
    PDF-bytes) gevalideerd via een headless jsdom-run met een gestubde
    Canvas 2D-context (dit environment heeft geen browsertooling, zelfde
    aanpak als bij eerdere UI-fixes) — synthetische meting door `onData()`
    gevoerd, geen exceptions. De output-bestanden zijn daarna geopend met
    **openpyxl** (bevestigt: afbeelding aanwezig op `setup_analyse` als
    `OneCellAnchor`) en **pypdf** (bevestigt: 1 pagina, A4-landscape
    mediabox, ingesloten JPEG correct leesbaar) — dus niet alleen "geen JS-
    fouten" maar ook "de output-bestanden zijn structureel geldig volgens
    onafhankelijke Excel/PDF-lezers". **Niet visueel in een browser
    gecontroleerd** (canvas-tekencode zelf, exacte layout/opmaak) — graag
    zelf even een echte meting met geometrie draaien en beide knoppen
    proberen.
- **2026-08-11** (vervolg): op verzoek een **"Oude meting laden (GitHub)"**-kaart
  toegevoegd (adv-only) zodat eerder via "Naar GitHub" geüploade metingen
  (release-tag `recordings`) alsnog door de nieuwe bedienkracht-analyse
  gehaald kunnen worden — dropdown met de assets, "Laden"-knop zet
  samples/geometrie/naam/notities terug alsof het een live meting is,
  waarna de bestaande "Rapportage + Analyse XLSX/PDF"-knoppen gewoon werken.
  - **CORS-blokkade ontdekt (met curl, niet gegokt)**: de asset-lijst ophalen
    kan wél rechtstreeks vanuit de browser (`api.github.com` stuurt
    `Access-Control-Allow-Origin: *` op publieke GET's), maar de **inhoud**
    van een asset downloaden niet — de download-redirect eindigt op
    `release-assets.githubusercontent.com` (Azure Blob) zonder CORS-header.
    Daarom is `far500-upload-worker/src/index.js` uitgebreid met
    `GET /download?name=<asset>` (bewust zonder `X-Upload-Secret`, want het
    proxyt alleen al-publieke assets uit deze ene release — geen vrije
    URL-doorgifte).
  - **JS XLSX-lezer toegevoegd** aan `FAR-500.html` (`unzipStoredFile`/
    `parseSheetRows`/`parseFarMetingWorkbook`) — leest uitsluitend bestanden
    die de eigen `xlsxWorkbook()` heeft geschreven (ongecomprimeerd/"STORE",
    dus geen DEFLATE-decompressie nodig); een in Excel bewerkt/opnieuw
    opgeslagen bestand wordt herkend en geweigerd i.p.v. als brij verwerkt.
    Alleen `t_s`/`angle_deg`/`force_N` worden uit de datarijen overgenomen —
    arc/speed/accel/hoogte worden na het laden gewoon opnieuw door
    `analyze()` berekend met de (uit de meta teruggezette) L/H-geometrie,
    i.p.v. de destijds-opgeslagen afgeleide kolommen te vertrouwen.
  - **Getest**: het volledige rondje (synthetische meting → `buildMetingXlsx()`
    → via de "Laden"-knop met een gemockte `fetch()` weer terug parsen →
    `haveHL()`/samples-aantal/naam/notities/geometrie correct hersteld →
    `buildAnalyseReportCanvas()` bouwt zonder exceptie) gevalideerd via
    dezelfde headless-jsdom-aanpak als eerder vandaag.
  - **Gedeployed en live getest** — zie Deploy-log hieronder.
- **2026-08-06**: hoek-teken omgedraaid + justeer-toggle + vaste grafiek-assen + kracht-teken-constante.
  - **Firmware**: justeerpunt stap2 gaat van +45° naar **-45°** (`calTargetAngle`,
    `FAR-500_ESP32C6.ino`) — via `gainV = calTargetAngle/rawAngle` wisselen daardoor
    automatisch alle toekomstige hoekmetingen (live, BLE, log) van teken. Nieuw:
    tijdens stap2 kan met **2 zeer korte klikjes** (elk 50-300ms, binnen 400ms van
    elkaar; `BTN_VSHORT_*`/`BTN_DBLCLICK_GAP_MS`) het justeerdoel getoggled worden
    naar **-30°** (nogmaals dubbelklikken → terug naar -45°); het OLED-scherm toont
    live welk doel actief is. Elke nieuwe justering (stap1→stap2) reset het doel
    naar -45°. Ook toegevoegd: `FORCE_SIGN`-constante (default `+1.0f`) in
    `sauterParse()` — **nog niet fysiek gevalideerd** dat "+" overeenkomt met
    fysiek omhoog optrekken/duwen; test dit na upload (aan de goot trekken moet een
    positief getal geven) en zet op `-1.0f` als het andersom is.
  - **UI (`FAR-500.html`)**: de hoek-as (XY-grafiek) en de hoek/kracht-lijnen in de
    tijd-grafiek gebruiken nu vaste assen i.p.v. automatisch op de meetdata
    schalend: hoek **-50° (links) .. +5° (rechts)**, kracht **-250N .. +250N**
    (`ANGLE_MIN/MAX`, `FORCE_MIN/MAX`). Waarden buiten die range worden bij het
    tekenen geklemd (`clamp()`) op de rand i.p.v. van het canvas te verdwijnen.
    De XY-grafiek se X-mapping is van "omgekeerd, data-gedreven" naar "gewoon
    oplopend, vast" gegaan — dat geeft dankzij de tekenwissel dezelfde fysieke
    links/rechts-oriëntatie als voorheen (fysiek hoge kanteling blijft links).
    Gevalideerd met een jsdom-headless-run (dit environment heeft geen browser-tooling,
    zelfde aanpak als bij eerdere UI-fixes): geen JS-fouten, constanten aanwezig,
    clamp() getest op waarden buiten bereik —
    **niet visueel in een browser bekeken**, graag zelf even de XY-grafiek en de
    OLED-tekst tijdens justeren controleren.
  - Compileren + uploaden naar COM10 gelukt met het gebruikelijke commando
    (`$env:PLATFORMIO_CORE_DIR="C:\pio"; & "$HOME\.platformio\penv\Scripts\pio.exe" run -d "C:\DEV\FAR-500\FAR-500_ESP32C6" -t upload --upload-port COM10`).
    Build: RAM 8.2%, Flash 62.9%.
- **2026-08-06**: `far500-force-check/` toegevoegd — Python-tool (openpyxl) die een
  meting-export toetst aan de bedienkracht-criteria C1-C4 en een XLSX+PDF-rapport
  genereert; `.github/workflows/force-check.yml` bouwt/publiceert dit in CI. Twee
  norm-interpretatiekeuzes zijn met de opdrachtgever afgestemd en vastgelegd in
  `far500-force-check/README.md`: (1) het 20cm-breakaway-venster (150% marge) geldt
  altijd vanaf het anker, ook zonder vroege overschrijding (anker=bewegingsstart is
  dan alleen de positie van dat venster, niet de afwezigheid ervan); (2) de overige
  aannames uit de oorspronkelijke opdracht (marge ook >135cm, toetsing op |force_N|,
  >170cm=harde FAIL) zijn ongewijzigd overgenomen. Er was geen echte device-export
  beschikbaar — alles is gebouwd/getest op synthetische fixtures
  (`far500-force-check/tests/`); 1x valideren tegen een echte export is een
  openstaand actiepunt vóór productiegebruik. Nog niet getest: de CI-workflow zelf
  (LibreOffice-PDF-render) is alleen tegen de lokale mechanica gevalideerd, niet
  live op GitHub Actions gedraaid.
- **2026-08-11**: bevestigd dat `.github/workflows/force-check.yml` ook **live op
  GitHub Actions succesvol draait** (run `31097502011` op commit `679ab4a`,
  master, 2026-08-06) — het bovenstaande "nog niet getest"-punt was dus
  achterhaald. Het gepubliceerde build-artefact (`far500-force-check-rapport`)
  bevat zowel `far500-force-check-rapport.xlsx` als
  `far500-force-check-setup_analyse.pdf`; de volledige keten (toetsing →
  Excel-rapport-met-grafiek → PDF-render) werkt dus end-to-end in CI. Nog steeds
  open: 1x valideren tegen een echte FAR-500.html-device-export (nu nog alleen
  synthetische fixtures) vóór productiegebruik.
- "Naar GitHub"-upload (Cloudflare Worker relay) toegevoegd aan de UI, gedeployed en end-to-end getest (2026-07-30) — werkt met een classic PAT (zie Architectuur-sectie voor het waarom). Bestandsnamen (export + upload) beginnen nu met een `yyyymmdd_hhmmss`-tijdstempel.
- Repo verplaatst van persoonlijk account (studiotijn) naar org `DynteqBV` (2026-07-30).
- Later (nog niet actueel): GitHub-org hernoemen van `DynteqBV` naar `dynteq` zodra die naam vrijkomt (nu nog in gebruik door een collega).
- Optimalisatie van UI
- Bouwen / valideren van hoek justering


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
- **2026-07-31**: na live-tests (PC + Bluefy/iOS) 6 UX-issues gemeld: grafiek verdwijnt bij start meting, geschiedenis-download werkt niet, Excel-download werkt niet in Bluefy, 2e meting downloaden lukt niet, light mode werkt niet in Bluefy, GitHub-upload werkt niet. Eerste 4 (kernpad start/weergeven/stop/download) zijn nu gefixt:
  - **Firmware**: `NimBLEDevice::setMTU(185)` toegevoegd na `init()` — zonder MTU-onderhandeling bleef de ATT-MTU op de default 23 byte (20 byte payload); de telemetrieregel (8 velden) groeit zodra `t` (ms sinds start) meer cijfers krijgt en ging dan over die grens, waardoor de notify stilzwijgend werd afgekapt en de UI-grafiek precies bij het starten van een meting leek te bevriezen/verdwijnen. `measNum` is niet langer sessie-only: wordt nu ook in Preferences (`measNum`) opgeslagen en bij boot teruggelezen, zodat metingnummers na een stroomcyclus niet opnieuw bij 1 beginnen (dat veroorzaakte dubbele `#MEAS,1,...`-markers in `/log.csv` en liet de 2e meting in de UI-geschiedenis de 1e overschrijven/onvindbaar worden).
  - **UI (`FAR-500.html`)**: `onData()` negeert nu regels met een niet-numeriek eerste veld (afgekapte/kapotte BLE-notify) i.p.v. de grafiek te breken. DUMP-import (`onLog()`/geschiedenis-import/device-log-download) heeft nu een 5s-timeout (`armDumpTimeout()`) zodat een weggevallen `<<EOF>>`-notify niet meer voor altijd blijft hangen zonder foutmelding. `download()` toont voortaan ook altijd een zichtbare data:-link (`dlFallback`/`histDlFallback`) als fallback, omdat Bluefy's minimale iOS-webview de synthetic `<a download>`-click vaak niet oppikt (geen download-dialoog, geen foutmelding) — met een lange tik op die link kan het bestand alsnog via "Bewaar bestand"/Delen opgeslagen worden.
  - **Nieuw**: Standard/Advanced-toggle (`.seg` naast de licht/donker-knop, default Standard, persistent via `localStorage["far500mode"]`). In Standard mode volledig verborgen (CSS-klasse `adv-only`, `body.standard .adv-only{display:none}`): kaart "Limieten & live" (snelheid/versnelling), knop "Wis device", beide "Naar GitHub"-knoppen + de "GitHub-upload"-kaart, knop "Device-log", kaart "Handvat-posities → geometrie", en de hele "Geschiedenis"-kaart.
  - Nog open (bewust uitgesteld, geen firmware/UI-wijziging voor gedaan): dark/light mode op Bluefy (oorzaak vermoedelijk `getCol()` in `FAR-500.html` die kleuren van `<html>` leest i.p.v. `<body>`, waar de `.light`-class op staat) en GitHub-upload-fout (Worker/CORS/foutafhandeling testen zelf goed, dus vermoedelijk een config-probleem: secret/token of Bluefy die grote POST-bodies blokkeert).
  - Firmware succesvol geüpload naar COM10 met hetzelfde commando als eerder (`$env:PLATFORMIO_CORE_DIR="C:\pio"; & "$HOME\.platformio\penv\Scripts\pio.exe" run -d "C:\DEV\FAR-500\FAR-500_ESP32C6" -t upload`). Build: RAM 8.2%, Flash 62.8%. Chip herkend als ESP32-C6FH4 (QFN32), MAC f0:f5:bd:2c:c9:18.
  - UI-wijzigingen zijn **niet visueel getest in een browser** — dit environment heeft geen chromium-cli/Playwright beschikbaar (geen browser-tooling geïnstalleerd) en die installeren zou tijd kosten terwijl het device net aangesloten was. Wel zorgvuldig gecontroleerd door de aangepaste HTML/JS terug te lezen. Graag zelf even doorklikken (Standard/Advanced, licht/donker, downloads) bij het live testen.
- **2026-07-31** (vervolg): na live-test in Edge bleek de licht/donker-knop, de Standard/Advanced-toggle én de grafiek allemaal kapot — bleek een pre-existing bug (zat al vóór deze sessie in de code, niet veroorzaakt door de wijzigingen hierboven): `tsC`/`xyC` (canvas-referenties) stonden pas laat in het script gedeclareerd via `const`, terwijl de "Limieten & live"-instelling bij het laden van de pagina meteen `redraw()`→`drawTS()` aanriep — een temporal-dead-zone `ReferenceError` die de rest van het script (incl. licht/donker- en mode-knop, en alle canvas-tekencode) liet afbreken. Gevonden door `FAR-500.html` headless te draaien met `jsdom` (`npm install jsdom` in de scratchpad-map — dit environment heeft geen browser-tooling) i.p.v. te gokken; bevestigd dat dezelfde crash ook al in de vorige commit zat. Fix: `tsC`/`xyC` meteen bovenaan het script gedeclareerd, vóór alle code die `redraw()` kan aanroepen. Bevestigd met dezelfde jsdom-simulatie (incl. gestubde canvas-context, want jsdom heeft zelf geen echte Canvas 2D-implementatie) dat licht/donker en Standard/Advanced nu foutloos werken.
- **2026-07-31** (vervolg 2): OLED-uitlezing herontworpen op verzoek — hoek en kracht nu in dezelfde lettergrootte (`u8g2_font_logisoso18_tn`, was 24pt voor hoek / 13pt bold voor kracht), decimaalpunten van beide altijd verticaal uitgelijnd (rechts uitlijnen van het gehele getal op een vaste x-positie, `drawReading()` in `FAR-500_ESP32C6.ino`), halve/decimale eenheid in kleiner lettertype erachter, gradensymbool na de hoek. Kracht-teken (+/-) is nu een getekend pijltje (`drawArrow()`) i.p.v. tekst: + = pijl omlaag, - = pijl omhoog. Hoek wordt op het OLED-scherm afgevlakt (EMA, factor 0,75/0,25) en afgerond op 0,5 graad om ruis te onderdrukken — **alleen voor het OLED-scherm**; de ruwe hoek die via BLE naar de laptop-UI gaat en in `/log.csv` komt blijft ongewijzigd/volledige precisie (bewuste keuze, met gebruiker afgestemd). Layout kon niet visueel worden voorgetest (geen fysieke OLED-toegang in dit environment) — alleen compile-check gedaan; graag na uploaden zelf even naar het scherm kijken en positie/marges laten weten als iets niet klopt. Zelfde upload-commando/COM10 (device tijdelijk niet zichtbaar op COM10 tijdens deze sessie — bleek een losse USB-verbinding; na opnieuw vastmaken werkte de upload weer), build succesvol.

## Deploy-log (far500-upload-worker -> Cloudflare)
- **2026-07-30**: eerste deploy via `npx wrangler login` (OAuth-browserflow) + `npx wrangler deploy` vanuit `far500-upload-worker/`. Live op `https://far500-upload-worker.far500-upload-worker.workers.dev` (URL ingevuld in `GH_RELAY_URL` in `FAR-500.html`).
  - **wrangler.toml `compatibility_date`**: mag niet in de toekomst liggen (Cloudflare valideert tegen de echte kalenderdatum) — foutcode 10021 als dat wel zo is. Stond aanvankelijk op de (foutieve, toekomstige) systeemdatum; teruggezet naar `2024-09-23`.
  - **Secrets**: `UPLOAD_SECRET` (gedeeld wachtwoord voor de UI, willekeurig gegenereerd) en `GH_TOKEN` gezet via `wrangler secret put <NAAM>` (waarde non-interactief doorgepiped, i.p.v. de interactieve prompt — die werkt niet vanuit een niet-interactieve shell).
  - **GH_TOKEN moet een classic PAT zijn** (scope `public_repo`) — zie de opmerking bij Architectuur hierboven voor waarom een fine-grained PAT hier niet werkt.
  - Na de fixes (compatibility_date, `Authorization: token` i.p.v. `Bearer`, classic PAT) end-to-end getest met een curl-upload: asset kwam succesvol aan op `github.com/DynteqBV/far-500/releases/tag/recordings`.
- **2026-08-11**: `GET /download?name=` toegevoegd (zie Huidige status) voor de
  "Oude meting laden"-knop in de UI. Gedeployed via `npx wrangler deploy`
  (wrangler was al ingelogd als `Tijn@dynteq.nl`), versie-ID
  `b83829be-83c8-40d7-a409-facd2c00892a`. Live getest met curl tegen een
  bestaande asset (`20260807_150340_far500.xlsx`): 200 OK, correcte
  `Access-Control-Allow-Origin`/`Content-Disposition`, content-length klopt
  (35141 bytes) en het resultaat is een geldig zip/XLSX-bestand (`python -m
  zipfile` leest de verwachte onderdelen: `xl/worksheets/sheet1.xml` etc.).