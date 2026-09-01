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
   `recordings` op dynteq/far-500). Reden: directe browser-upload naar
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
- **2026-09-01** (nieuw veld "Temperatuur (°C)" toegevoegd, op verzoek, alles
  in `FAR-500.html`):
  - **Nieuw invulveld** `#temperatuur` in de "Meting"-kaart (naast Naam/
    Notities). Verplicht: de hele kaart knippert oranje (bestaand
    `needs-fill`-mechanisme, `updateMetingBlink()`) zolang niet alle 3 velden
    (Naam, Notities, Temperatuur) gevuld zijn — was voorheen alleen Naam+
    Notities. Ook toegevoegd aan `reportPrereqsOk()`, dus zonder ingevulde
    temperatuur geen rapport (XLSX/PDF) te genereren.
  - **Automatisch voorgevuld met de actuele buitentemperatuur in Nederland**
    (`fetchOutdoorTemp()`/`applyOutdoorTemp()`, aangeroepen bij het laden van
    de pagina): via `navigator.geolocation` (5s timeout) de eigen locatie, of
    bij weigering/ontbreken een vaste terugval-coördinaat (De Bilt,
    52.10992/5.18069 — KNMI-referentiepunt, midden van NL) — opgevraagd bij
    **Open-Meteo** (`api.open-meteo.com/v1/forecast?...&current=temperature_2m`,
    geen API-key nodig, CORS-vriendelijk, dus rechtstreeks vanuit de browser
    bruikbaar zonder de bestaande Cloudflare-Worker-relay). Vult het veld
    alleen als het nog leeg is (nooit een handmatige waarde overschrijven) en
    blijft daarna gewoon een vrij bewerkbaar invulveld.
  - **Komt terug in alle exports**: `metaRows()` (dus zowel de ruwe
    "Meetgegevens XLSX"/CSV als de metadata-header) heeft een nieuwe regel
    "Temperatuur (°C)"; het canvas-gerasterde rapport (XLSX-tabblad
    `setup_analyse` + PDF, gedeelde `buildAnalyseReportCanvas()`) toont hem
    als extra regel in het "Setup"-blok (`TOP_BLOCK_H` opgehoogd van 7 naar
    8 tekstregels om ruimte te maken). Bij het laden van een oude meting via
    "Oude meting laden (GitHub)" (`btnOldMeasLoad`) wordt de opgeslagen
    temperatuur nu ook teruggezet in het veld (`metaStr(meta,"Temperatuur
    (°C)",1)`) — bestanden van vóór deze wijziging missen die metadata-regel
    gewoon, dan blijft het veld leeg (blinkt dan weer oranje tot opnieuw
    ingevuld, geen crash).
  - **Getest**: `node --check` op het geëxtraheerde scriptblok, en een
    headless jsdom-run (15 checks, canvas-2D-context gestubd — dit
    environment heeft geen browser-tooling) die bevestigt: het veld bestaat
    en blokkeert een mislukte/geen-netwerk auto-fetch niet (geen crash, veld
    blijft gewoon leeg); de Meting-kaart blijft knipperen zolang temperatuur
    leeg is en stopt zodra gevuld; `metaRows()` bevat de juiste
    Temperatuur-regel/waarde; `reportPrereqsOk()` faalt zonder temperatuur en
    slaagt zodra ook die (naast de al bestaande velden) gevuld is; en de
    volledige rapport-pijplijn (synthetische meting →
    `buildAnalyseReportAssets()` → XLSX + PDF) bouwt nog steeds foutloos —
    de output is met `openpyxl` (3 tabbladen: `setup_analyse`/`data`/
    `Kracht_hoek`, 11 zip-onderdelen) en `pypdf` (1 pagina, A4-portrait
    mediabox 595x842) als structureel geldig bevestigd. Ook los bevestigd
    (`openpyxl` op een via `buildMetingXlsx()` gebouwd bestand) dat de ruwe
    "Meetgegevens XLSX" de temperatuur als een echte numerieke cel (bv.
    `21.3`) in de metadata-rijen bevat. **Niet visueel gecontroleerd** in
    een echte browser/mobiel — met name de geolocation-permissie-flow en of
    Open-Meteo op een echte mobiele verbinding in het veld snel genoeg
    reageert zijn niet live getest; graag bij de volgende veldmeting
    bevestigen dat het temperatuurveld vanzelf een redelijke waarde toont en
    dat de knipperende rand/rapportgeneratie er goed uitzien.
- **2026-08-24** (3e tabblad "Kracht_hoek" toegevoegd aan het XLSX-rapport, op
  verzoek): in `FAR-500.html`.
  - **Nieuwe helperfuncties** (direct na `buildReportDataRows()`): `median()`,
    `robustMeanForce()` (mediaan +/- 3x MAD-uitschieterfilter, alleen bij >=4
    punten per graad, anders gewoon het gemiddelde), `perDegreeForce()`
    (groepeert per heel-graad `Math.round(angle)`), `interpolateDegrees()`
    (lineair interpoleren van ontbrekende graden binnen het bereik),
    `buildKrachtHoekRows(angles, forces)` (splitst de meting op het keerpunt
    -- de hoogste hoekwaarde -- in een omhoog- en een omlaag-tak, en
    retourneert `[["angle_deg","force_omhoog_N","force_omlaag_N"], ...]`
    oplopend gesorteerd op hoek) en `degreesRange()`. Zelfde aanpak als een
    los Python-nabewerkingsscript dat dezelfde dag op de Metingen-fileshare
    op losse xlsx-exports werd toegepast (buiten deze repo, puur ter context).
  - **`buildReportXlsxBlob()` uitgebreid** met een 3e tabblad `Kracht_hoek`
    (`sheet3Xml`, zelfde stramien als het bestaande `sheet2Xml`: alleen
    `sheetXmlRows()`, geen drawing/afbeelding) gevuld met
    `buildKrachtHoekRows(dataRows.slice(1).map(r=>r[1]), dataRows.slice(1).map(r=>r[2]))`.
    `[Content_Types].xml`, `xl/workbook.xml`, `xl/_rels/workbook.xml.rels` en
    de `files`-array zijn alle 4 bijgewerkt zodat het 3e tabblad daadwerkelijk
    meegenomen wordt. Zowel de "Opslaan"-knop (`btnReportPdf`) als het losse
    (advanced-only) `btnReportXlsx`-knopje roepen dezelfde
    `buildReportXlsxBlob()` aan, dus beide exportpaden krijgen het nieuwe
    tabblad zonder verdere wijziging.
  - **Getest**: `node --check` op het geëxtraheerde scriptblok (geen
    syntaxfouten). Daarnaast een los Node-scriptje (niet gecommit, in de
    scratchpad-map) dat de relevante functies uit het bestand extraheert en
    met een synthetische meting (hoek van -48° naar -2° en weer terug, met
    ruis + een paar bewuste uitschieters per graad) + een dummy-jpeg
    daadwerkelijk een `.xlsx` bouwt en naar schijf schrijft. Dat bestand is
    vervolgens met Python/`openpyxl` geopend en gecontroleerd: 3 tabbladen
    (`setup_analyse`, `data`, `Kracht_hoek`), `Kracht_hoek` heeft exact de 3
    verwachte kolommen, oplopend gesorteerd op hele graden, de uitschieters
    zijn weggefilterd (waarden blijven in het verwachte bereik), en
    `setup_analyse`/`data` zijn ongewijzigd/intact gebleven (`data` nog
    steeds 189 rijen, `setup_analyse` met een geldige drawing-relatie naar
    de afbeelding). Ook de ruwe zip-structuur gecontroleerd (`unzip -l`):
    alle 11 verwachte onderdelen aanwezig, inclusief `sheet3.xml`.
  - **Niet getest**: geen visuele controle in een echte browser/Excel --
    dit environment heeft geen browser-tooling; de knoppen zelf
    (`btnReportPdf`/`btnReportXlsx`) zijn niet live aangeklikt. Graag bij een
    volgende live-test een echt rapport genereren en het `Kracht_hoek`-
    tabblad in Excel openen om te bevestigen dat het er ook visueel klopt.
- **2026-08-24** (vervolg: echte volgnummer-gaten gevonden tijdens een
  live-test in Chrome + DUMPCANCEL, op verzoek naar aanleiding van herhaalde
  "volgnummer X oversprongen"-meldingen):
  - **De gap-detectie uit de vorige entry werkte zoals bedoeld** en ving nu
    voor het eerst een écht probleem: tijdens een live "Importeer
    geschiedenis" in Chrome vielen structureel 1-2 regels per keer weg
    (bv. verwacht 211, gekregen 213). Waarschijnlijke oorzaak: `notify()`
    bevestigt alleen dat de ESP32 een verzendverzoek heeft geaccepteerd, NIET
    dat de vórige waarde al daadwerkelijk over de lucht ging -- bij een
    BLE-connection-interval die groter is dan de verzendpacing (12ms) kan
    `setValue()+notify()` de nog-niet-verstuurde vorige regel stilzwijgend
    overschrijven (geen foutcode, gewoon weg). Pacing in `stepDumpBle()`
    verhoogd van 12ms naar **40ms** om ruim binnen 1 verbindings-event te
    blijven.
  - **Bijkomend, verklarend gevonden probleem**: één gemelde melding
    ("volgnummer 0 oversprongen") wees op **stream-interleaving** -- de
    gebruiker klikte na een mislukte import op "opnieuw", maar het device
    was zelf nog gewoon de VORIGE (allang opgegeven) overdracht aan het
    afmaken (bevestigd: "de ESP32 blijft gewoon doorgaan tot 100%", een
    losstaande observatie die niets met de dataverlies-bug te maken had
    maar wel bevestigde dat er geen cancel-mechanisme was) -- de restjes van
    de oude stream kwamen dan door elkaar met de net gestarte nieuwe stream
    binnen op dezelfde characteristic. Fix: nieuw CTRL-commando
    **`DUMPCANCEL`** (`reqDumpCancel`/`finishDumpBle()` in de firmware) dat
    de laptop-UI nu verstuurt zodra ZIJ een overdracht afbreekt (zowel bij
    een gedetecteerd volgnummer-gat als bij de 10s-stilte-timeout, zie
    `armDumpTimeout()`/`onLog()` in `FAR-500.html`) -- het device stopt dan
    meteen i.p.v. de rest van een groot logbestand nutteloos door te blijven
    sturen naar een kant die al lang niet meer luistert.
  - **Getest**: `pio run` (compileert schoon, RAM 8.2%/Flash 63.1%), `node
    --check`, en alle 4 bestaande jsdom-testbestanden opnieuw gedraaid (geen
    regressies -- `send()` is in de tests een no-op zolang `ctrlChar` null
    is, dus de nieuwe `send("DUMPCANCEL")`-aanroepen breken niets).
    Geflashed naar COM10, hash-geverifieerd.
  - **✅ Live bevestigd werkend in Edge** (gebruiker, zelfde dag): geen
    volgnummer-gaten meer, "Importeer geschiedenis" rondt nu betrouwbaar af.
    Hiermee is de hele BLE-DUMP-betrouwbaarheidssaga (zie de entries
    hierboven/hieronder: INDICATE-poging → teruggedraaid → niet-blokkerende
    state machine → millis()-bug → stale-cache-omweg → protocol-mismatch →
    40ms-pacing + DUMPCANCEL) afgerond en bevestigd. Openstaand voor een
    volgende sessie, indien ooit weer relevant: dezelfde 40ms-pacing is nog
    niet expliciet getest met een écht groot logbestand (honderden
    metingen) over een langere tijdsduur — als daar op termijn alsnog
    incidentele gaten optreden, is een adaptievere pacing (of alsnog een
    niet-blokkerende INDICATE met correcte async-afhandeling, zie de
    gearchiveerde poging hierboven) de voor de hand liggende vervolgstap.
- **2026-08-24** (afronding van de BLE-DUMP-betrouwbaarheidssaga hieronder —
  meerdere iteraties, uiteindelijk werkend bevestigd door de gebruiker,
  gecommit+gepusht): de 2026-08-21-entry direct hieronder ("BLE-overdracht
  van 'Importeer geschiedenis'/'Device-log' betrouwbaarder gemaakt") beschrijft
  de EERSTE aanpak (INDICATE+retry) — die bleek **averechts te werken** en is
  teruggedraaid; lees die entry dus als historisch, niet als eindstand. Wat
  er sindsdien is gebeurd, in volgorde:
  1. **INDICATE teruggedraaid naar NOTIFY**: op het echte device (na
     eindelijk een succesvolle reflash, zie Upload-log) bleek INDICATE de
     zaak juist erger te maken — "Importeer geschiedenis" lukte niet meer
     en de OLED-hoekweergave werd "onzettend traag" zodra de laptop
     verbonden was. Oorzaak: `sendLogLineReliable()` blokkeerde de
     hoofdloop tot 1,5s per regel wachtend op een ATT-confirm die er dankzij
     onbetrouwbare INDICATE-ondersteuning op dit platform (Windows/Edge-Web
     Bluetooth) vaak nooit kwam. Teruggedraaid naar NOTIFY (fire-and-forget,
     geen wachttijd); de per-regel volgnummers (`"<seq>:<inhoud>"`) bleven
     staan als vangnet tegen een stilzwijgend gemiste regel.
  2. **Echte root cause van de OLED-traagheid gevonden**: niet INDICATE op
     zich, maar dat `logDumpBle()` (ongeacht NOTIFY/INDICATE) altijd al 1
     grote BLOKKERENDE functie was (`while(f.available()){...}`), aangeroepen
     vanuit `loop()` — bij een groot logbestand (100+ metingen) bleef `loop()`
     (en dus ook `drawOled()`, in dezelfde loop) minutenlang hangen in die ene
     aanroep. Herschreven naar een niet-blokkerende state machine
     (`startDumpBle(now)` + `stepDumpBle(now)`, max 1 regel per loop()-tick)
     — `loop()` blijft nu gewoon op zijn normale 150ms-tempo doorlopen tijdens
     een overdracht.
  3. **Nieuwe OLED-overdracht-scherm toegevoegd** (op verzoek): tijdens een
     DUMP-transfer toont de OLED nu `drawDumpScreen()` (titel + BT-
     verbindingsstatus + live %) i.p.v. de normale hoek/kracht-uitlezing;
     live-telemetrie naar de laptop (`pData`) wordt tijdens een transfer ook
     gepauzeerd (minder BLE-contentie, en de UI toont toch geen live
     waarden tijdens een import). Bij een mislukte/gestalde overdracht
     (10s geen enkele succesvolle `notify()`, of BLE-verbinding weg) herstelt
     de OLED zichzelf automatisch (via de bestaande `showMsg()`/`M_MSG`-
     infrastructuur) i.p.v. voor altijd op het overdracht-scherm te blijven
     hangen.
  4. **2 losse, echte bugs gevonden tijdens het testen van bovenstaande**:
     (a) `startDumpBle()` riep zelf een verse `millis()` op i.p.v. de
     `now`-snapshot van de aanroepende `loop()`-tick door te geven — kon een
     fractie later uitkomen dan die `now`, wat bij de eerstvolgende
     `stepDumpBle(now)`-aanroep een **uint32_t-underflow** gaf op
     `now-dumpLastOkMs` (wrapt naar ~4 miljard) en de 10s-stall-check
     ONMIDDELLIJK liet afgaan i.p.v. na een echte 10s — verklaarde de
     gemelde "direct" timeout. Fix: `now` wordt nu doorgegeven i.p.v. dat
     `startDumpBle()` zelf `millis()` opvraagt. (b) De OLED-foutmelding
     "mislukt (timeout)" (18 tekens) vulde bij het vaste-breedte 7px/teken
     "7x13B"-font exact de volle 128px-schermbreedte, waardoor de laatste
     ")" werd afgesneden (door de gebruiker zelf opgemerkt) — verkort naar
     "timeout" (7 tekens); nieuwe kanttekening toegevoegd bij `showMsg()`-
     aanroepen in `stepDumpBle()` dat 2-regelige OLED-berichten op dit font
     ruim onder de ~18 tekens moeten blijven.
  5. **Blijvend probleem na bovenstaande fixes bleek GEEN firmware-bug**:
     "Importeer geschiedenis" liep na een verse BLE-herverbinding nog steeds
     vast op exact dezelfde manier. Uitgezocht via PowerShell of Windows een
     verouderde BLE-cache voor FAR-500 vasthield:
     `Get-PnpDevice`/registry-onderzoek vond een LE-bond/fingerprint-cache
     op `HKLM:\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Devices\
     f0f5bd2cc91a` (BLE-MAC, offset t.o.v. de eerder genoteerde base-MAC
     `f0:f5:bd:2c:c9:18`) — bevestigd als FAR-500 via de `Name`/`LEName`-
     bytewaarden (decoderen naar "FAR-...''). Met toestemming van de
     gebruiker verwijderd (elevated PowerShell, `Remove-Item` op die ene
     device-subkey — andere Bluetooth-apparaten ongemoeid). **Loste het
     probleem niet op** (zelfde fout na een compleet verse pairing) — dus
     achteraf bleek dit geen stale-cache-probleem te zijn geweest, wél de
     moeite waard om uit te sluiten.
  6. **Werkelijke laatste blokkade gevonden**: nadat de OLED-teller na
     firmware-fix keurig tot 100% doorliep, gaf de laptop-UI ALSNOG "5s geen
     data"-foutmelding — de "5s"-tekst verried dat de **live HTML nog de
     oude, vóór-volgnummers-versie** was (`FAR-500.html`/de JS-kant van de
     volgnummer-validatie was deze hele sessie al klaar en getest, maar
     bewust nog niet gecommit/gepusht in afwachting van een succesvolle
     veldtest). Met de nieuwe firmware die alle regels als `"<seq>:<inhoud>"`
     verstuurt, herkende de oude/live JS (die dat voorvoegsel niet
     wegstript) `"<<EOF>>"` niet meer omdat elke regel nu bv. als
     `"247:<<EOF>>"` binnenkwam -- een pure protocol-versie-mismatch tussen
     een al-bijgewerkte firmware en een nog niet gepushte UI, geen nieuwe
     bug. Fix: gewoon de al-geschreven, al-geteste `FAR-500.html`-wijzigingen
     nu committen+pushen (zie hieronder).
  - **Uiteindelijk bevestigd werkend door de gebruiker** (na deze laatste
    push): OLED blijft responsief tijdens een overdracht, "Importeer
    geschiedenis" rondt af tot 100% en de UI verwerkt de data correct.
  - **Getest** (naast de al eerder genoemde jsdom-suites, allemaal opnieuw
    gedraaid ná deze laatste JS-push): `node --check`, alle 4 bestaande
    jsdom-testbestanden (dump-progress/opnamenummer/grafiek-opmaak/
    volgnummer-detectie) — alle groen, geen regressies. Firmware: `pio run`
    compileert schoon bij elke tussenstap; alle reflashes hash-geverifieerd.
- **2026-08-21** (vervolg: BLE-overdracht van "Importeer geschiedenis"/
  "Device-log" betrouwbaarder gemaakt — sequentienummers + INDICATE+retry,
  op verzoek naar aanleiding van 2 gemelde problemen; **HISTORISCH: de
  INDICATE-aanpak hieronder is teruggedraaid, zie de 2026-08-24-entry
  hierboven voor de uiteindelijke, werkende oplossing**):
  - **Vraag beantwoord**: geen UDP/TCP — de device↔laptop-verbinding is
    Bluetooth Low Energy (BLE) GATT, een heel ander protocol zonder IP-laag
    (TCP/HTTPS wordt alleen gebruikt door de losse GitHub-upload-relay, niet
    voor deze DUMP-overdracht). BLE's linklaag heeft wel een CRC + automatische
    retransmissie voor losse radiopakketten, maar dat beschermt niet tegen
    een volle notificatiequeue of RF-congestie op applicatieniveau.
  - **Root cause gevonden voor 2 gemelde problemen**: (1) de 5s-stilte-
    timeout die de import soms afbrak, en (2) dat opname #42 in de
    geschiedenis-dropdown de data van zowel #42 als #43 bevatte, terwijl
    #43 zelf niet in de lijst stond (44 wel). De "Log"-characteristic
    (`pLog`) stond op **NOTIFY** (`FAR-500_ESP32C6.ino`) — fire-and-forget:
    de firmware riep `notify()` aan zonder de return-waarde te checken, geen
    enkele garantie dat de laptop een regel echt ontving. Bij een volle
    notificatiequeue of een korte RF-hobbel viel een regel stilletjes weg;
    als dat toevallig de `#MEAS,43,...`-markerregel was, bleef
    `importHistory()` (die alleen op zo'n markerregel een nieuw blok start)
    gewoon doorgaan met blok #42 voor alle volgende samples tot de volgende
    overlevende marker (#44) — exact het gemelde symptoom, zonder dat er
    ooit een foutmelding kwam.
  - **Gekozen aanpak (met de gebruiker afgestemd via `AskUserQuestion`,
    "grondiger"-optie)**: zowel de kans op een mislukte regel verkleinen als
    een eventuele restfout altijd zichtbaar maken i.p.v. laten samensmelten:
    (1) **`pLog` van NOTIFY naar INDICATE** — een indicatie krijgt een
    ATT-confirm van de laptop terug (gemeld via de nieuwe `LogCB::onStatus()`
    -callback, `code==0`=bevestigd); (2) nieuwe `sendLogLineReliable()`
    verstuurt elke DUMP-regel met tot **3 pogingen** (elk met een 500ms-
    wachttijd op de confirm) vóór hij een regel definitief opgeeft; (3) elke
    regel (incl. `#SIZE`/`#MEAS`-markers en `<<EOF>>`) krijgt nu een
    oplopend **volgnummer**-voorvoegsel (`"<seq>:<inhoud>"`, opgebouwd in
    `logDumpBle()`) zodat de laptop-UI (`onLog()` in `FAR-500.html`) een gat
    altijd kan detecteren — ook als alle 3 pogingen voor een regel toch nog
    mislukken. Bij een gedetecteerd gat breekt de import nu direct af met
    een duidelijke popup ("volgnummer X oversprongen") i.p.v. stilzwijgend
    door te gaan met een corrupt resultaat. Oudere firmware zonder
    volgnummers blijft ondersteund (UI valt dan terug op "geen controle",
    geen breaking change voor een niet-geflashed board).
  - De live-telemetrie-characteristic (`pData`) is bewust **NOT** aangepast
    — blijft NOTIFY, af en toe een gemist live-sample is acceptabel en de
    extra confirm-vertraging zou daar geen zin hebben.
  - **UI-stilte-timeout verruimd van 5s naar 10s** (`armDumpTimeout()`) omdat
    een reeks INDICATE-retries per regel iets langer kan duren dan de oude
    fire-and-forget NOTIFY-aanpak; dit was tevens het 2e gemelde probleem
    (transfer brak soms af na "minimaal 5 seconden").
  - **Getest**: `pio run` (compileert schoon, RAM 8.2%/Flash 63.0%, zelfde
    als de laatste bekend-goede build), `node --check`, en een gerichte
    jsdom-run (12 checks) die o.a. **exact het gemelde scenario**
    reproduceert (een kunstmatig weggelaten `#MEAS,43,...`-regel, dus een
    gat in de volgnummers) en bevestigt dat de import dan direct afbreekt
    met een duidelijke popup i.p.v. #42/#43 te laten samensmelten; een
    schone, gat-loze sequentie geeft geen valse afbreking en splitst #42/#43
    correct; en dat oudere (niet-geflashde) firmware zonder volgnummers nog
    steeds gewoon werkt (backwards-compatibele fallback). **Niet live op het
    device getest** (kon niet geflashed worden, board viel weg van COM10 —
    zie de waarschuwing hierboven) — dus ook niet bevestigd dat de INDICATE-
    aanpak de daadwerkelijke faalkans in het veld merkbaar verlaagt, alleen
    dat de detectie/foutmelding bij een gat correct werkt.
- **2026-08-21** (vervolg: OLED-teller-box verbreed + firmware geflashed +
  bevestigd werkend, op verzoek):
  - **Vraag beantwoord**: bij measNum=99 → volgende meting wordt 100, geen
    reset naar "01" (`measNum` is een `uint16_t`, 0-65535, telt gewoon door
    en wordt in Preferences/NVS bewaard over herstarts heen). Enige kleine
    aandachtspunt: het OLED-tellervakje was met 26px breed bedoeld voor 1-2
    cijfers.
  - **OLED-tellervakje verbreed 26px→32px** (`drawOled()`,
    `FAR-500_ESP32C6.ino`) zodat 3 cijfers (100-999) niet meer tegen de
    kaderrand komen.
  - **Firmware geflashed naar COM10** (board was eerst niet aangesloten/niet
    gedetecteerd — geen match op VID_303A/10C4/1A86, ook geen COM10 in
    `[System.IO.Ports.SerialPort]::getportnames()` — pas na opnieuw
    aansluiten gevonden). Dit bracht ook de eerder deze dag geschreven
    `#SIZE`-progressregel (DUMP-%-balk) en het 9e telemetrieveld (live
    opnamenummer) voor het eerst daadwerkelijk op het device.
  - **Upload-gotcha gevonden en opgelost**: de eerste 2 upload-pogingen
    hingen minutenlang zonder enige output (leek in eerste instantie op een
    vastgelopen reset/handshake, maar was dat niet). Root cause: esptool
    5.3.0's nieuwe voortgangsbalk gebruikt Unicode blok-tekens (█/░), en
    PlatformIO's Windows-console-echo-thread crasht daarop met een
    `UnicodeEncodeError` (cp1252 kan die tekens niet coderen) — de hoofd-
    `pio run`-thread blijft daarna voor altijd hangen wachten op die dode
    thread (deadlock), zonder foutmelding. Fix: `PYTHONIOENCODING=utf-8`
    zetten vóór het upload-commando. **Voor toekomstige uploads**: gebruik
    dus `export PYTHONIOENCODING=utf-8; export PLATFORMIO_CORE_DIR="C:\pio";
    & "$HOME\.platformio\penv\Scripts\pio.exe" run -d
    "C:\dev\FAR-500\FAR-500_ESP32C6" -t upload --upload-port COM10` (1 extra
    export t.o.v. het eerdere commando in dit Upload-log). Bij de 2 hang-
    pogingen werd alleen de bootloader (deels/geheel) herschreven op
    0x0-0x5fff, nooit de LittleFS-partitie (die wordt door `-t upload` sowieso
    nooit aangeraakt) — geen opgeslagen metingen zijn dus ooit in gevaar
    geweest, en de uiteindelijke succesvolle 3e poging herschreef
    bootloader/partitions/boot_app0/firmware toch nog eens allemaal
    consistent.
  - **Build/upload succesvol bevestigd**: RAM 8.2% (26940/327680 B), Flash
    63.0% (825188/1310720 B) -- exact gelijk aan de laatste bekend-goede
    build. Alle 4 delen (bootloader.bin@0x0, partitions.bin@0x8000,
    boot_app0.bin@0xe000, firmware.bin@0x10000) geschreven + hash-
    geverifieerd, hard reset via RTS-pin.
  - **Live bevestigd door gebruiker**: zowel het verbrede OLED-tellervakje
    als de %-balk bij "Importeer geschiedenis" werken nu goed.
- **2026-08-21** (vervolg: knop "Opslaan" + grafiek-opmaakfixes (tickmarks
  terug zonder "cm", écht root-cause-fix voor "handvathoogte"-overlap, 2x
  grotere rode afkeur-punten), op verzoek, alles in `FAR-500.html`,
  `drawAngleForceChart()` behalve de knop):
  - **Knop "Maak rapportage + Analyse" → "Opslaan"**, en dezelfde blauwe
    (`primary`/cyaan) kleur als "Start meting" (was grijs/neutraal).
  - **0/10/20-tickmarks weer met tekstlabel, boven én onder** (was: sinds
    2026-08-20 alleen "20cm" boven en "0cm" onder, de rest kaal) — nu weer
    alle 3 tickmarks aan beide kanten, maar zonder "cm"-eenheid (bv. "10"
    i.p.v. "10cm").
  - **"handvathoogte"-label overlapte nog steeds met "135 cm"**, ondanks 3
    eerdere fixpogingen (2026-08-19, 2x 2026-08-20) — **root cause nu pas
    gevonden**: `ctx.textAlign` stond op dat punt in de tekencode nog op
    `"center"` (geërfd van de hoek-as-loop erboven, nooit expliciet gezet
    voor de 135/170cm-labels), dus "135 cm" werd GECENTREERD op zijn
    tick-positie getekend i.p.v. links uitgelijnd ervanaf. Alle eerdere
    fixes gingen ervan uit dat "135 cm" bij `px135` bégint, terwijl de
    tekst daar juist het MIDDEN had — de linkerrand van "135 cm" lag dus
    steeds verder naar links dan aangenomen, precies genoeg om de kleine
    vaste marge (`AF*0.6`) op te eten. Fix: `textAlign` nu expliciet op
    `"center"` gezet vóór het tekenen van "135 cm"/"170 cm" (i.p.v. impliciet
    geërfd), de breedte van "135 cm" wordt gemeten en de linkerrand
    (`px135 - breedte/2`) is nu het uitgangspunt voor waar "handvathoogte"
    moet eindigen — wiskundig gegarandeerd geen overlap meer, voor elke
    positie van de 135cm-streep (ook als "handvathoogte" daardoor over de
    linkerrand van de grafiek/het canvas heen loopt, zoals al eerder
    afgesproken: geen overlap is de leidende eis).
  - **Rode (afkeur-)meetpunten 2x zo groot** (straal 3.4→6.8px) om beter op
    te vallen; groene/cyaan normale punten ongewijzigd (2.2px).
  - **Meta-vraag van de gebruiker beantwoord** (of ik feedback die tijdens
    het verwerken van een prompt gegeven wordt kan meenemen): nee, dat kan
    niet — ik verwerk één bericht per beurt en zie een tussentijds bericht
    pas ná het afronden van de lopende beurt, als een nieuw bericht. Dat is
    een technische beperking, geen kwestie van "niet goed omgaan met
    input" op zich. Voor déze specifieke "handvathoogte"-klacht was het
    echter geen kwestie van gemist commentaar: dezelfde bug is 3x eerder
    (ogenschijnlijk) "gefixt" zonder de werkelijke root cause (de geërfde
    `textAlign="center"`) te vinden — dat is nu wél gebeurd, zie boven.
  - **Getest**: `node --check`, en een uitgebreide jsdom-run (15 checks) via
    de volledige `buildAnalyseReportCanvas()`-pijplijn met een synthetische
    snelle-breakaway-meting (zowel omhoog als omlaag): bevestigt de nieuwe
    knoptekst/-kleur, dat "0"/"10"/"20" (zonder "cm") bij zowel de
    omhoog- als omlaag-tickmarks getekend worden, dat de rode punten nu
    straal 6.8 gebruiken (normale punten nog 2.2), en — met de exacte
    px-rekensom van de daadwerkelijke `ctx.fillText`-aanroepen — dat
    "handvathoogte" nooit meer over de (gecentreerd gemeten) linkerrand van
    "135 cm" heen valt, getest in zowel een normale geometrie als de
    historisch falende situatie (135cm-streep vlak bij de linkerrand van de
    grafiek). **Niet visueel gecontroleerd** in een echte browser/PDF-viewer
    — graag bij de volgende PDF-export bevestigen dat het er nu ook
    visueel goed uitziet.
- **2026-08-21** (vervolg: %-voortgangsbalk bij DUMP-transfers + opnamenummer
  in PDF-rapport en bestandsnamen, op verzoek, in `FAR-500.html` +
  `FAR-500_ESP32C6.ino`):
  - **Voortgangsbalk bij "Importeer geschiedenis" én "Device-log"** (zelfde
    onderliggende DUMP-overdracht): beide knoppen gaan nu op grijs zodra een
    transfer loopt (er kan er maar 1 gelijktijdig lopen) en tonen een
    %-balk. Voor een écht percentage (i.p.v. alleen "bezig...") stuurt de
    firmware nu de bestandsgrootte als allereerste regel (`#SIZE,<bytes>`,
    nieuw in `logDumpBle()`) -- **dit vereist een reflash**, zie hieronder.
    Met de nog niet-geflashte firmware (huidige situatie: device niet
    aangesloten deze sessie, COM10 niet gevonden) werkt de UI-kant al wel
    correct terug naar de oude situatie: knoppen grijs + "bezig..."-melding,
    gewoon zonder percentage totdat er geflashed is.
  - **Opnamenummer (device-#MEAS-teller) nu overal waar een meting
    getoond/geëxporteerd wordt**: rechtsboven in een kader ("#<nr>") in het
    PDF/XLSX-rapport, direct onder de "FAR-500 / bedienkracht-analyse"-kop;
    en in **alle** exportbestandsnamen (Meetgegevens XLSX/CSV via `fname()`,
    rapport-XLSX/PDF via `fnameSuffix()`) direct na de tijdstempel, vóór
    merk/type (bv. `20260821_140501_49_Falco_Premium_analyse.pdf`).
    Bronnen van het nummer: (a) **live meting** -- de telemetrie-regel kreeg
    een 9e veld (`measNum`, nieuw in de firmware-`loop()`) zodat de UI het
    nummer real-time volgt (`liveTracking`); (b) **geladen uit de
    device-geschiedenis-dropdown** (2026-08-21 eerder vandaag) -- number al
    bekend uit het `#MEAS`-blok, geen firmware-wijziging nodig; (c)
    **geladen oude meting via GitHub** -- alleen bekend als het bestand na
    deze wijziging geëxporteerd is (nieuwe metadata-rij "Meting nr" in
    `metaRows()`); oudere GitHub-exports missen dit veld, dan blijft het
    nummer onbekend en wordt het kader/bestandsnaam-segment gewoon
    weggelaten (geen "null"/leeg segment). Een verse "Start meting" (via UI
    of drukknop) reset het bijhouden altijd weer naar "volg het live
    device-nummer" (`liveTracking=true` in `resetAnalysis()`), zodat een
    eerder handmatig geladen historisch nummer niet blijft plakken.
  - **Firmware inmiddels geflashed en bevestigd werkend** (zie Upload-log
    voor het exacte commando/COM-poort/probleem-onderweg): live-opnamenummer-
    tracking (9e telemetrieveld) en de echte %-balk (`#SIZE`-regel) draaien
    nu op het device. Gebruiker heeft na deze reflash bevestigd dat zowel de
    verbrede OLED-teller-box (zie volgende sessie-notitie) als de %-balk bij
    "Importeer geschiedenis" goed werken.
  - **Getest**: `pio run` (compileert, zie boven), `node --check`, en een
    uitgebreide jsdom-run (31 checks) die dekt: beide DUMP-knoppen grijs +
    balk zichtbaar tijdens een transfer en weer terug na EOF/timeout; een
    `#SIZE`-regel geeft een echt oplopend percentage, het ontbreken ervan
    (oudere firmware-simulatie) laat de balk netjes op "bezig..." staan
    zonder te crashen; de `#SIZE`-regel wordt nergens als databestandsregel
    meegenomen; `curMeasNum`/`liveTracking` via alle 3 bronnen (live 9-velds
    telemetrie, history-dropdown, GitHub-metadata) inclusief dat live
    telemetrie een handmatig geladen nummer niet overschrijft totdat een
    nieuwe meting start; bestandsnamen met en zonder bekend nummer; en dat
    `buildAnalyseReportCanvas()` het kader+"#101" tekent zodra bekend en
    volledig weglaat zodra niet. **Niet visueel gecontroleerd** in een echte
    browser/PDF-viewer, en de firmware-kant (`#SIZE`/9e telemetrieveld) is
    niet op het echte device getest (geen reflash mogelijk deze sessie) --
    graag na de volgende reflash + veldmeting bevestigen dat de balk een
    kloppend percentage toont en dat het opnamenummer klopt met de OLED-
    teller.
- **2026-08-21** (bugfix combinatieknop XLSX+PDF+GitHub + nieuwe dropdown om
  een individuele device-geschiedenis-meting te laden, op verzoek, alles in
  `FAR-500.html`):
  - **Aanleiding**: bij metingen op 2026-08-20 in het veld (Standard mode) gaf
    "Maak rapportage + Analyse" wel een PDF maar geen XLSX, en kwam er niets
    op GitHub terecht. Twee losse, plausibele oorzaken geïdentificeerd (geen
    van beide met zekerheid reproduceerbaar zonder de exacte browser/locatie
    van gisteren, dus beide defensief gefixt):
    (1) **`download()` triggerde 2 automatische bestandsdownloads
    (XLSX daarna PDF) zonder pauze ertussen** — sommige browsers blokkeren
    een 2e automatische download die te snel na de 1e vanuit script komt.
    Nu wacht de combinatieknop 400ms tussen de XLSX- en de PDF-download.
    Ook revoked `download()` de blob-URL niet meer meteen na `a.click()`
    (nu na 3s) — te vroeg revoken kan op mobiele browsers een net-gestarte
    download laten mislukken.
    (2) **De "niet gedownload? tik op deze link"-fallbacklink (`#dlFallback`,
    nodig op Bluefy/iOS-webviews die `a.click()`-downloads negeren) werd door
    de PDF overschreven** — beide bestanden deelden 1 fallback-element, dus
    als de gebruiker op die link moest terugvallen was de XLSX-link al
    verdwenen tegen de tijd dat de PDF klaar was. `download()`/
    `showDownloadFallback()` ondersteunen nu `append` — de combinatieknop
    toont nu altijd beide bestandslinks naast elkaar.
    (3) **GitHub-upload had geen retry**: als de upload mislukt (bv. geen
    netwerk op de meetlocatie, zoals gisteren waarschijnlijk het geval was),
    werd dat alleen gemeld in de kleine `ghStatus`-hint en waren de lokale
    bestanden verder de enige uitkomst. Nieuwe knop **"Opnieuw uploaden naar
    GitHub"** (`btnGhRetry`, naast `ghStatus`) verschijnt nu zodra de upload
    binnen de combinatieknop mislukt, en bewaart het al-gebouwde PDF-blob
    (`lastReportUpload`) zodat je het later (met verbinding) alsnog kan
    uploaden zonder het rapport opnieuw te moeten genereren.
  - **Nieuwe dropdown "Geschiedenis" (advanced mode)**: de al bestaande
    "Geschiedenis — laatste metingen op device"-kaart (`#histWrap`) stond
    hardcoded op `display:none` in beide modi (zie sessienotitie 2026-08-19
    hieronder) — dat is nu verwijderd, de kaart is weer zichtbaar in Advanced
    mode (via de bestaande `adv-only`-klasse, blijft verborgen in Standard).
    Na "Importeer geschiedenis" is er nu, naast de bestaande tabel/Excel-
    export, ook een dropdown (`#histLoadSelect`, nieuwste meting boven) +
    knop **"Meting laden in analysescherm"** (`#btnHistLoad`) die een
    individuele meting (op `#MEAS`-nummer) rechtstreeks als `samples` in het
    analysescherm zet — zelfde soort samples-vervanging als "Oude meting
    laden (GitHub)" (`btnOldMeasLoad`), maar zonder meta (de device-
    geschiedenis bevat alleen t/deg/N, geen Naam/Notities/Handvat-posities),
    dus die velden blijven staan zoals al ingevuld. Zo kunnen de metingen die
    gisteren als drukknop-sessies op het device zijn gelogd (recorder-teller
    stond op 49) alsnog stuk voor stuk door de C1-C4-analyse gehaald worden.
  - **Getest**: `node --check`, en een uitgebreide jsdom-run (20 checks) die
    (a) bevestigt dat `#histWrap` niet meer geforceerd verborgen is; (b) een
    synthetisch device-log met 2 `#MEAS`-blokken door `importHistory()` haalt
    en controleert dat de dropdown correct gevuld is (nieuwste eerst) en dat
    "Meting laden" de juiste samples in `samples[]` zet; (c) dat `download()`
    met `append=true` beide bestandslinks in `#dlFallback` laat staan i.p.v.
    de eerste te overschrijven; (d) dat `uploadToGitHub()` nu een boolean
    teruggeeft en dat de retry-knop verschijnt/verdwijnt op het juiste moment;
    (e) de **volledige combinatieknop end-to-end** met een synthetische
    up-beweging + `btnOverridePositions` (geen BLE nodig): zowel bij een
    succesvolle als bij een mislukte GitHub-upload staan achteraf altijd
    beide bestandslinks (XLSX+PDF) klaar, en bij een mislukte upload
    verschijnt de retry-knop terwijl de lokale bestanden toch gewoon
    beschikbaar blijven. **Niet visueel gecontroleerd** in een echte browser
    (geen browser-tooling in dit environment) — met name de vermoede
    root cause (browser-blokkade van 2 snelle automatische downloads) is
    aannemelijk maar niet 1-op-1 reproduceerbaar geweest; graag bij de
    volgende veldmeting bevestigen dat nu zowel XLSX als PDF aankomen (evt.
    via de nieuwe fallback-links) en, als er toen geen netwerk was, de
    nieuwe "Opnieuw uploaden naar GitHub"-knop proberen.
- **2026-08-20** (vervolg: upload/rapport-blok verplaatst naar onderaan op
  smal scherm + 2-staps auto-scroll, op verzoek, in `FAR-500.html`):
  - **Upload-code + "Maak rapportage"-knop op een smal scherm (<800px) nu
    ook helemaal naar onderaan**, ná de "Kracht vs. hoek"-grafiek (was
    voorheen na de Verloop-grafiek, vóór "Kracht vs. hoek"). Upload-code-veld
    + knoppen + status-hints (`ghStatus`/`dlFallback`) zaten los in de DOM;
    samen gewrapt in een nieuwe `#reportSection`-container zodat ze in 1x
    verplaatst kunnen worden. `placeVerloopChart()` is hernoemd naar
    `placeCharts()` en verplaatst nu zowel de Verloop-grafiek (tussen
    Start/Stop en `#reportSection`) als `#reportSection` zelf (naar ná
    `#krachtHoekChartWrap`, de nieuwe id op de "Kracht vs. hoek"-chart-wrap)
    -- en zet op een breed scherm beide weer terug naar hun oorspronkelijke
    positie (`reportSectionHome`, éénmalig bij laden vastgelegd).
  - **Auto-scroll bij Start is nu 2 stappen**: eerst naar de Verloop-grafiek
    (ongewijzigd, direct), en na 2 seconden door naar de "Kracht vs.
    hoek"-grafiek (`scrollToChartsOnStart()`, vervangt de eerdere
    `scrollToVerloopChart()`, aangeroepen vanuit zowel `$("btnStart")
    .onclick` als het device-start-pad in `onData()`).
  - **Getest**: `node --check`, en een jsdom-run die bevestigt dat bij het
    versmallen van het scherm (gesimuleerde `matchMedia`-toggle) de Verloop-
    grafiek tussen Start/Stop en `#reportSection` terechtkomt, dat
    `#reportSection` daarna ná `#krachtHoekChartWrap` staat, en dat beide bij
    verbreden weer exact terug op hun oorspronkelijke plek (na
    `#startStopRow`, als eerste kind van `#chartsCol`) staan; en dat een
    gesimuleerde Start-klik `scrollIntoView` eerst op de Verloop-chart-wrap
    en ~2000ms later op de Kracht-vs-hoek-chart-wrap aanroept. **Niet
    visueel gecontroleerd** in een echte browser (geen browser-tooling in
    dit environment).
- **2026-08-20** (vervolg: grote UI-herindeling + gecombineerde rapportknop +
  bestandsnaam met notitie + auto-scroll + rapport-opmaak, op verzoek, alles
  in `FAR-500.html`; plan vooraf goedgekeurd via plan-mode):
  - **Linker kolom (bediening) herschikt**: Verbinden staat nu helemaal
    boven, direct daaronder de "Meting"-kaart (naam/notities) en de
    "Handvat-posities"-kaart. Daarna (ongewijzigde inhoud, alleen verplaatst)
    de Kracht/Hoek-uitlezing, sensor-oriëntatie, de banner, een kleinere
    knoppengroep (Tare/Wis device/Meetgegevens XLSX/C4-override/CSV/
    Device-log), "Oude meting laden", "Limieten & live", de meta-regel, dan
    een nieuwe **Start/Stop-rij** (2 kolommen, Start links/Stop rechts — was
    Stop-vóór-Start in 1 grote gemengde knoppengrid) en tot slot het
    upload/rapport-blok (`#reportBlock`).
  - **Grafiek schuift in op smal scherm (<800px)**: een `matchMedia`-listener
    (`placeVerloopChart()`, gedeclareerd direct na `tsC`/`xyC`) verplaatst de
    Verloop-`chart-wrap`-node (`#verloopChartWrap`) fysiek tussen de
    Start/Stop-rij en `#reportBlock` zodra het scherm smaller is dan 800px,
    en legt 'm terug als eerste kind van de grafieken-kolom (`#chartsCol`)
    zodra het scherm weer breder wordt. Een `<canvas>` behoudt zijn
    2D-context/pixelbuffer bij zo'n DOM-verplaatsing (bevestigd, geen
    her-render nodig). Bestaande CSS-breakpoint van 860px naar 800px
    gelijkgetrokken zodat kolomstapeling en deze reparent gelijktijdig
    triggeren. Telefoon-only sub-breakpoint (≤480px) toegevoegd die
    `.col`/`.card`/`.chart-wrap`-padding verder verkleint voor meer
    bruikbare breedte.
  - **Eén rapportknop**: "Maak rapportage + Analyse PDF" heet nu **"Maak
    rapportage + Analyse"** en slaat bij één klik zowel de XLSX
    (`buildReportXlsxBlob`) als de PDF (`buildAnalysisPdfBlob`) op — beide
    gebouwd uit hetzelfde canvas/asset-resultaat van 1x
    `buildAnalyseReportAssets()` (geen dubbele render). Upload naar GitHub
    blijft zoals voorheen aan de PDF gekoppeld. De losse "Rapportage +
    Analyse XLSX"-knop (adv-only, geen PDF/upload) blijft ongewijzigd bestaan
    voor wie dat losse pad nog wil.
  - **Bestandsnaam bevat nu ook de notitie**: `fnameSuffix()` (gebruikt door
    de rapport-exports) plakt de gesaniteerde "Notities"-tekst (max 60
    tekens) tussen naam en suffix, bv.
    `20260820_..._Falco_Premium_hoog_TestLading_28kg_analyse.pdf`. Bewust
    beperkt tot de rapport-bestanden (`fname()`, de ruwe Meetgegevens-XLSX/
    CSV, blijft ongewijzigd).
  - **Auto-scroll naar de Verloop-grafiek bij Start**: zowel bij
    `$("btnStart").onclick` als het device-geïnitieerde start-pad in
    `onData()` (drukknop op het device) scrollt de pagina nu naar
    `#verloopChartWrap` (`scrollIntoView({behavior:"smooth",block:"start"})`),
    zodat direct zichtbaar is dat er data binnenkomt.
  - **Drie kleine rapport/grafiek-opmaakfixes** (`drawAngleForceChart()`/
    `buildAnalyseReportCanvas()`): (1) de toelichting "groen: toegestaan |
    oranje: ... | rood: ..." staat nu op een eigen regel onder de titel
    "Kracht vs. hoek", kleiner en niet-bold (was op dezelfde regel, bold);
    (2) de 0/10/20cm-tickmarks tonen nu alleen nog een tekstlabel bij de
    uiterste punten — bovenaan (omhoog) alleen bij 20cm, onderaan (omlaag)
    alleen bij 0cm (10cm en de andere kant houden alleen de streep, geen
    tekst); (3) "hoek (°)" staat nu links uitgelijnd direct onder de
    "-50°"-as (was gecentreerd, ver naar onder met veel witruimte), met de
    "*"-voetnoot direct daaronder, ook links — `BOTAX` (en de bijpassende
    `CHART_H`-factor) teruggebracht van `AF*6.3` naar `AF*5.0` omdat er nu
    veel minder onderaan-ruimte nodig is.
  - **Rapport-kop herontworpen**: rechts uitgelijnd "FAR-500" in een dun
    lettergewicht (`"300 46px sans-serif"`) met daaronder klein
    "bedienkracht-analyse"; links uitgelijnd dikgedrukt de naam (Merk/Type),
    daaronder dikgedrukt de notitie gevolgd door (niet-dik) datum/tijd op
    dezelfde regel. Zelfde totale hoogte (205px) als de vorige titel/
    metadata-regel, dus geen wijziging nodig aan `TOP_BLOCK_H`.
  - **Getest**: `node --check`, en een uitgebreide jsdom-run die (a) de
    nieuwe DOM-volgorde bevestigt (Connect < Meting < Handvat; Start/Stop-rij
    < reportBlock; Start vóór Stop binnen die rij); (b) de `matchMedia`-
    reparent in beide richtingen (smal → chart tussen Start/Stop en
    reportBlock; breed → chart terug als eerste kind van de grafieken-
    kolom); (c) dat `fnameSuffix()` de notitie correct saniteert/opneemt; (d)
    dat de gecombineerde knop precies 1x download() voor de XLSX + 1x voor de
    PDF aanroept en 1x uploadToGitHub(); (e) dat `scrollIntoView` op
    `#verloopChartWrap` aangeroepen wordt bij Start; (f) met een dataset met
    een échte snelle breakaway: de titel/toelichting op gescheiden regels
    met het juiste lettertype, dat alleen "20cm" (boven) en "0cm" (onder) nog
    getekend worden, dat "hoek (°)" en de voetnoot links uitgelijnd op
    dezelfde x staan met de voetnoot direct onder de as-titel, en dat de
    nieuwe koptekst-elementen (FAR-500/bedienkracht-analyse rechts,
    naam/notitie+datum links) met de juiste uitlijning/lettergewicht
    getekend worden. **Niet visueel gecontroleerd** in een echte browser
    (geen browser-tooling in dit environment) — graag zelf even doorklikken:
    Connect→Meting→Handvat-volgorde, Start/Stop-layout, het smal-scherm-
    gedrag (venster smaller maken dan 800px) inclusief de auto-scroll bij
    Start, en een PDF/XLSX genereren via de nieuwe "Maak rapportage +
    Analyse"-knop.
- **2026-08-20** (vervolg: bugfix "handvathoogte" overlapte nog steeds met
  "135cm", op verzoek, in `FAR-500.html`, `drawAngleForceChart()`): de vorige
  fix (build 10:01) mat de tekstbreedte, maar klemde de linkerpositie ook
  tegen `x+4` (de linkerrand van de grafiek) als er weinig ruimte was -- en
  precies die klem duwde de tekst weer over "135 cm" heen zodra de
  135cm-streep dicht bij de linkerrand viel (exact het scenario dat de fix
  moest oplossen). De klem is verwijderd: "handvathoogte" staat nu altijd
  op basis van de gemeten tekstbreedte volledig links van "135 cm", ook als
  dat betekent dat de tekst over de linkerrand van de grafiek (of het
  canvas) heen loopt -- op verzoek is "geen overlap met 135cm" de leidende
  eis, niet "binnen de grafiekrand blijven". **Getest**: `node --check`, en
  een jsdom-run met een bewust geconstrueerde geometrie waarbij de
  135cm-streep vlak bij `ANGLE_MIN` (dus de linkerrand) valt -- bevestigt
  dat de oude klem hier daadwerkelijk overlap zou hebben gegeven en dat de
  tekst er nu, zonder klem, nooit meer overheen valt. **Niet visueel
  gecontroleerd** in een echte browser/PDF-viewer.
- **2026-08-20** (vervolg: oranje breakaway-kader richtingsgebonden vast +
  ticks alleen bij snelle breakaway, op verzoek, in `FAR-500.html`,
  `drawAngleForceChart()`):
  - **Oranje kader bij de omlaag-gaande beweging nooit meer aan de +
    kant**: voorheen volgde de kant van het kader het teken van de
    gemiddelde kracht in het 20cm-venster (`graceForceSign()`, nu
    verwijderd) — dat kon het kader naar de positieve kant spiegelen. Nu
    wordt het kader alleen getekend als de kracht ergens in de beweging
    ECHT negatief onder -50% van de bedienkracht komt (nieuwe
    `findSignedHalfCrossArc(move,-1)`), en dan altijd aan de negatieve kant.
    Komt de kracht nooit onder die -50% (bv. alleen positief), dan wordt er
    voor de omlaag-beweging helemaal geen kader getekend.
  - **Oranje kader bij de omhoog-gaande beweging altijd aan de + kant**
    (was al zo, nu ook expliciet afgedwongen via `findSignedHalfCrossArc
    (move,+1)` i.p.v. de teken-ongevoelige `findHalfCrossArc` — geen kader
    meer als er toevallig geen echte positieve 50%-overschrijding is, i.p.v.
    een kader op de verkeerde/+ kant forceren).
  - **Ticks (0/10/20cm) tonen we nu alleen bij een "snelle" breakaway**:
    nieuwe `hasQuickBreakaway(move)` zoekt eerst het punt waar de kracht 10%
    van de bedienkracht bereikt, en toont de ticks alleen als de kracht
    binnen de eerstvolgende 5 graden ook de 50% haalt. Bouwt de kracht
    trager op (bv. een geleidelijke, geen-echte-breakaway-meting), dan
    worden er voor die beweging geen ticks (en geen "* omhoog/omlaag"-
    onderschrift/voetnoot) getekend. De TICK-*positie* zelf (bij een
    geslaagde check) is ongewijzigd — nog steeds het bestaande, teken-
    ongevoelige 50%-kruispunt (`upCross`/`downCross`), op verzoek.
  - **Getest**: `node --check`, en 6 losse jsdom-scenario's via de volledige
    `buildAnalyseReportCanvas()`-pijplijn: (A) omlaag+echt negatief ≥50% →
    kader uitsluitend op de negatieve kant (bevestigd tegen een uit de
    ±140N-rasterlijnen afgeleide nullijn-y-positie); (B) omlaag+alleen
    positieve kracht → geen kader; (C) omhoog+positief ≥50% → kader
    uitsluitend op de positieve kant; (D) een langzame, lineaire
    krachtopbouw over de hele beweging → geen ticks/onderschrift/voetnoot;
    (E) een snelle opbouw (50% binnen 2° na het 10%-punt) → ticks en
    onderschrift wél getoond; (F) omhoog+alleen negatieve kracht → geen
    kader. Alle 6 scenario's gaven het verwachte resultaat. **Niet visueel
    gecontroleerd** in een echte browser/PDF-viewer.
- **2026-08-20** (vervolg: PDF-rapport nu altijd 1 pagina, op verzoek, in
  `FAR-500.html`): de tweede pagina (uitgebreide criteria-uitleg C1-C4) is
  volledig verwijderd. `buildAnalyseReportCanvas()` tekent de criteria-tekst
  niet meer (dode `criteriaTextLines()`/`wrapText()`-functies ook verwijderd,
  niets anders riep ze aan) en geeft geen `chartTop`-splitpunt meer terug;
  `buildAnalysisPdfBlob(jpegBytes, imgWpx, imgHpx)` is vereenvoudigd naar een
  vaste 1-pagina-opbouw (geen `splitPx`-parameter, geen pagina-clip-logica
  meer nodig). Titel/metadata/setup/Uitkomst-tabel/eindoordeel/grafiek staan
  allemaal op die ene pagina, breedte-vullend. Geldt voor zowel de PDF als
  de XLSX (delen dezelfde canvas-afbeelding) — de XLSX had het criteria-blok
  al nooit als apart "tabblad", dus die verandert verder niet. **Getest**:
  `node --check`, en een jsdom-run die bevestigt dat "Criteria" nergens meer
  getekend wordt en dat de gegenereerde PDF exact 1 `/Page`-object met
  `/Count 1` heeft. **Niet visueel gecontroleerd** in een echte PDF-viewer.
- **2026-08-20** (ruisfilter op hoek/positie-signaal + 3 opmaakfixes, alles
  in `FAR-500.html`):
  - **Ruis op het hoek/positie-signaal gefixt** (op verzoek, na de eerdere
    analyse van `20260731_153533_Falco_Premium`). Twee expliciete keuzes zijn
    vooraf met de gebruiker afgestemd via `AskUserQuestion` (verandert
    kernlogica van de C1-C4-engine, dus niet zomaar aangenomen):
    (1) **scope = overal** — dezelfde gladgestreken `A.arc`/`A.ht` wordt
    gebruikt voor zowel de bewegingsdetectie (`segmentMoves()`, ankers,
    envelope) als de getoonde afgelegde-afstand/handvathoogte-cijfers (i.p.v.
    een losse kopie alleen voor detectie); (2) **filtersterkte = hergebruik
    `smooth4()`** (4-punts voortschrijdend gemiddelde, dezelfde als al voor
    snelheid/versnelling gebruikt werd) i.p.v. een breder filter. Concreet:
    `analyze()` past nu `A.arc=smooth4(A.arc); A.ht=smooth4(A.ht);` toe
    direct na de ruwe hoek→positie/hoogte-omrekening, vóór de
    snelheid/versnelling-afleiding (die dus nu op een al gladgestreken
    positie werkt, en zelf ook weer smooth4() krijgt — een dubbele demping,
    bewust, zelfde patroon als eerder al voor snelheid/versnelling). Ruwe
    device-samples (t/deg/N) in CSV/XLSX-export blijven ongewijzigd — alleen
    de AFGELEIDE analysewaarden zijn gladgestreken.
  - **Getest**: een jsdom-run met een expliciet ingebouwde ruis-"blip" (korte
    hoek-nudge net over de REV_HYST-hysterese, gevolgd door de échte,
    geleidelijke krachtopbouw) en losse idle-periodes met kleine hoek/kracht-
    jitter: piek-|versnelling| tijdens idle nu 22,2 cm/s² tegenover 133,2
    cm/s² tijdens de werkelijke beweging (~6x marge, was voorheen in dezelfde
    orde van grootte) — het gemelde "onrealistische versnelling tijdens
    loze meting"-probleem is dus merkbaar verminderd. Bij een kunstmatig
    grote/aangehouden ruis-blip (bewust fors ingesteld om de grens te
    testen) kan `segmentMoves()` nog steeds oversegmenteren — dat is de
    geaccepteerde afweging van de gekozen 4-punts filtersterkte (breder
    filter was het alternatief, maar geeft meer vertraging op het echte
    bewegingsbegin); als dit met echte data nog voorkomt is REV_HYST
    verhogen de volgende stap, nog niet gedaan.
  - **"handvathoogte"-label stond nog niet altijd volledig links van de
    135cm-streep** (bug in de fix van hierboven/gisteren): de tekst werd
    met een vast pixel-offset rechts uitgelijnd tegen de 135cm-streep gezet,
    zonder de werkelijke tekstbreedte te kennen — bij een meting waarbij de
    135cm-hoogte dicht bij de linkerrand van de grafiek valt, liep de tekst
    zo van de grafiek af. Nu wordt de breedte met `ctx.measureText()` opgemeten
    en de starpositie geklemd op de linkerrand van de grafiek (`x+4`) als er
    niet genoeg ruimte is.
  - **Onderschriften bij de 0/10/20cm-tickmarks verkort** (op verzoek, te
    lang): "afgelegde afstand handvat start beweging omhoog →"/"...←omlaag"
    zijn nu "* omhoog →"/"* ← omlaag"; de weggehaalde tekst staat als
    voetnoot ("* afgelegde afstand handvat vanaf start beweging") helemaal
    linksonder in de grafiek (zowel live UI als PDF/XLSX, gedeelde
    tekencode in `drawAngleForceChart()`).
  - **Getest**: `node --check`, en een jsdom-run door de complete
    `btnOverridePositions→analyze()→buildAnalyseReportCanvas()`-pijplijn met
    synthetische ruisrijke samples: bevestigt de piek-versnelling-cijfers
    hierboven, dat de oude lange onderschriften nergens meer getekend worden
    en de verkorte "* omhoog/omlaag"-varianten + de voetnoot wél, en dat
    "handvathoogte" (breedte via de gestubde `measureText()`) altijd
    volledig vóór (links van) de 135cm-tekst eindigt. **Niet visueel
    gecontroleerd** in een echte browser/PDF-viewer, en ook niet tegen de
    exacte geometrie van een echte export (bv. Falco Premium) waarbij het
    handvathoogte-label eerder al fout stond — graag zelf even een PDF
    genereren met een meting waarbij 135cm dicht bij de linkerrand van de
    grafiek valt en de nieuwe versnellingscijfers op een echte, ruisige
    meting (bv. opnieuw `20260731_153533_Falco_Premium`) controleren.
- **2026-08-19** (vervolg 8: opmaakfixes grafiek/PDF + eindoordeel-kader
  verkleind, op verzoek, alles in `FAR-500.html`):
  - **Pijltjes weg bij de 0/10/20cm-tickmarks** ("↑"/"↓" achter "0cm" etc.) —
    stond te onrustig, `drawArcTicks()` tekent nu alleen nog "0cm"/"10cm"/"20cm"
    (zowel live UI als PDF/XLSX-rapport, gedeelde tekencode).
  - **Groene onderschriften "afgelegde afstand handvat..." staan nu gecentreerd
    recht boven/onder hun eigen 3 tickmarks** (op de x-positie van de
    "10cm"-tick, het midden van de 0-20cm-reeks) i.p.v. vast tegen de
    rechterrand van de grafiek — was voorheen op een vaste positie los van de
    ticks zelf.
  - **Label "handvathoogte" verplaatst naar links van de 135cm-streep**
    (`px135-Math.round(AF*0.6)`, rechts uitgelijnd) — stond eerst tegen de
    rechterrand en overlapte daar met het 170cm-label.
  - **PDF-paginabreuk lekte content door**: bij een breedte-gebonden schaal
    (dus met "slack" in de hoogte) tekende elke pagina de VOLLEDIGE
    rapportafbeelding, en zonder een clip-rechthoek liep het deel dat bij de
    ándere pagina hoort gewoon door tot aan de paginarand — vandaar dat
    "Criteria" en "hoek (°)" op pagina 1 én (een stukje) op pagina 2
    verschenen. Fix: `buildAnalysisPdfBlob()` tekent nu per pagina een
    expliciete PDF-clip (`re W n`) op precies het rijbereik (`rowStart`/
    `rowEnd`) dat die pagina hoort te tonen, vóór de `cm ... /Im1 Do`.
  - **Kader "EINDOORDEEL: PASS/FAIL" gehalveerd** (op verzoek, "iets sjieker"):
    afmetingen/font/tekstoffset allemaal x0.5 (700x53px i.p.v. 1400x105,
    font 33px i.p.v. 65px); de gereserveerde ruimte ervoor in
    `TABLE_BLOCK_H`/`buildAnalyseReportCanvas()` is meegekrompen (125→70).
  - Omdat de tickmark-onderschriften en de "hoek (°)"-as-titel nu allebei
    lager/hoger moeten staan om niet te overlappen is de gereserveerde
    hoogte onderaan de grafiek (`BOTAX` in `drawAngleForceChart()`, en de
    bijpassende `CHART_H`-formule in `buildAnalyseReportCanvas()`) vergroot
    van `AF*5` naar `AF*6.3`.
  - **Nog niet gedaan** (aparte, nog niet uitgevoerde vervolgstappen uit
    hetzelfde verzoek): de "50%-kruising lijkt op 90%"-bug (root cause:
    `segmentMoves()` werkt op het ongefilterde hoek/positie-signaal `A.arc`,
    waardoor bij ruis de grootste beweging soms al mid-ramp begint) en een
    concreet ruisfilter-advies voor de piek-versnelling aan begin/eind van
    een meting — beide vereisen een keuze die eerst met de gebruiker
    afgestemd moet worden (verandert kernlogica van de C1-C4-engine), zie ook
    de eerdere sessie-notitie over `AskUserQuestion` bij zo'n keuze.
  - **Getest**: `node --check`, en een jsdom-run die de complete
    `onOverridePositions→analyze()→buildAnalyseReportCanvas()→
    buildAnalysisPdfBlob()`-pijplijn met synthetische samples (idle→omhoog
    met geleidelijke krachtopbouw→idle→omlaag→idle) doorloopt en verifieert:
    geen "cm↑/↓" meer in de tick-labels, de onderschriften staan exact op de
    x-positie van hun "10cm"-tick, "handvathoogte" staat links van de
    135cm-labeltekst, het EINDOORDEEL-blok is 700x53px, en **beide**
    PDF-content-streams bevatten een `re W n`-clip. **Niet visueel
    gecontroleerd** in een echte browser/PDF-viewer — graag zelf even een PDF
    genereren en de nieuwe labelposities/paginasplitsing/kader-formaat
    bekijken.
- **2026-08-19** (vervolg 7: oranje-kader/tickmarks-nulpunt gelijk getrokken +
  PDF-paginavolgorde definitief, alles op verzoek, alles in `FAR-500.html`):
  - **Oranje kader "iets breder dan 0-20cm" opgehelderd**: geen bug in de
    ánder — de breedte was al exact 20cm cirkelbaanlengte (geverifieerd met
    een test: 130-131px versus theoretisch 130,3px). Het kwam door een
    verschillend nulpunt: het kader startte bij het 50%-kruispunt, de
    tickmarks bij de start van de beweging. Gebruiker koos: nulpunt van het
    kader is leidend, dus de 0/10/20cm-tickmarks (`drawArcTicks()`) gebruiken
    nu ook `upCross`/`downCross` (het 50%-kruispunt) i.p.v. `move.arcStart`.
    Bevestigd: linkerrand kader en "0cm↑"-tick vallen nu exact samen.
  - **Extra labels**: "handvathoogte" bij de 135/170cm-markers (zelfde
    lettertype), en groene onderschriften bij de tickmarks: "afgelegde
    afstand handvat start beweging omhoog →" (boven) / "...← omlaag" (onder).
  - **"omhoog"/"omlaag"-labels op de meetlijn**: omhoog nu bij -15° met tekst
    "omhoog →" (blijft boven de lijn), omlaag nu bij -30° met tekst
    "← omlaag" (nu altijd ONDER de lijn, niet meer conditioneel).
  - **PDF-paginavolgorde definitief**: de grafiek staat nu volledig op
    pagina 1 (direct na titel/metadata/setup/Uitkomst-tabel/eindoordeel) --
    niet meer tussen eindoordeel en criteria. De uitgebreide criteria-uitleg
    is een eigen blok dat altijd op pagina 2 komt, zonder dat daar nog een
    stukje grafiek zichtbaar is (`chartTop` is nu het einde van de grafiek
    i.p.v. het begin). Beide pagina's blijven de paginabreedte vullen
    (bevestigd: `tx≈24pt`op beide).
  - **Getest**: `node --check`, en jsdom-smoketests (breedte-/nulpunt-
    verificatie van het oranje kader t.o.v. de tickmarks met een gerichte
    px-berekening, aanwezigheid/tekst van de nieuwe labels, positie en
    boven/onder-plaatsing van de omhoog/omlaag-labels, en de volledige
    2-pagina-PDF-volgorde inclusief dat beide pagina's breedte-vullend
    blijven). **Niet visueel gecontroleerd** in een echte browser/PDF-viewer.
- **2026-08-19** (vervolg 6: download-proxy nu ook achter de upload-code, op
  verzoek): `GET /download?name=` in `far500-upload-worker/src/index.js`
  vereist sinds nu dezelfde `X-Upload-Secret` als de upload-kant (was bewust
  publiek sinds 2026-08-11, zie het "Deploy-log" hieronder voor de reden
  destijds) -- zonder de code kan je dus ook geen oude meting/PDF meer laden
  via de dropdowns, niet alleen niet uploaden. **Gedeployed** (`npx wrangler
  deploy`, versie-ID `33ff3966-7a5c-475d-9d26-b773dc1612d0`) en **live
  bevestigd met curl**: zonder code 401, met verkeerde code 401, met `tijn`
  200. `FAR-500.html` (`btnOldMeasLoad`/`btnOldPdfOpen`) stuurt de code nu
  mee als header en toont een duidelijke melding ("vul eerst de upload-code
  in" resp. "onjuiste upload-code") i.p.v. de aanroep te doen of een cryptische
  fout te tonen. `far500-upload-worker/README.md` bijgewerkt. Getest met
  jsdom (geen fetch zonder code, 401-melding bij verkeerde code, juiste
  header bij correcte code).
- **2026-08-19** (vervolg 5: 2 bugfixes + grafiek/PDF-verfijningen, op
  verzoek, alles in `FAR-500.html`):
  - **Bugfix "Meting"-kaart blijft knipperen na laden oude meting**: `.value=`
    zetten (bij het laden via "Oude meting laden") vuurt geen `input`-event
    af, dus `updateMetingBlink()` werd nooit opnieuw aangeroepen totdat de
    gebruiker zelf een letter wijzigde. Nu roept `btnOldMeasLoad` die functie
    direct aan na het zetten van naam/notities. Bevestigd met een test
    (knippert vóór laden, stopt direct na laden van een bestand met
    gevulde naam/notities).
  - **Vraag over 160 cm/s²-versnelling bij `20260731_153533_Falco_Premium`
    beantwoord/gecontroleerd**: dit IS al de gefilterde waarde (`smooth4()`
    wordt toegepast op zowel de snelheid als, daarna, nog eens op de daaruit
    afgeleide versnelling). Ruwe/ongefilterde piek was 602,7 cm/s², na 1x
    smoothing (alleen snelheid) 301,3 cm/s², na de huidige dubbele smoothing
    159,97 cm/s² -- dus een echte piek in de meting, geen rekenfout en geen
    ruwe/ongefilterde weergave.
  - **Root cause gevonden + gefixt voor "omhoog"/"omlaag"-labels die niet op
    de juiste plek stonden**: `upMove`/`downMove` pakten altijd de EERSTE
    gevonden beweging in `res.moves`, maar bij een echte (ruisige) meting
    kan segmentMoves() een klein, niet-representatief blipje vóór de
    eigenlijke sweep als aparte beweging herkennen. Nu wordt altijd de
    GROOTSTE beweging per richting gebruikt (grootste cirkelbaanlengte) --
    geldt voor de labels, het oranje breakaway-kader én de afstand-
    tickmarks. Bevestigd met een test die zo'n storend blipje bevat: labels
    landen nu exact op -35°/-15° van de echte sweep i.p.v. op het blipje.
  - **Krachtraster**: ±100N-lijn weer verwijderd (te druk samen met de
    nabije ±85N-lijn), ±85/±140/±210N blijven staan.
  - **PDF-indeling herzien** (`buildAnalyseReportCanvas()`): de uitgebreide
    criteria-uitleg staat nu op pagina 2 (bij de grafiek) i.p.v. pagina 1;
    de Uitkomst-tabel + eindoordeel vormen nu de onderste helft van pagina 1
    (titel/metadata/setup de bovenste helft). Metadata (naam/notities/datum)
    staat compacter op 1 regel i.p.v. 3 losse regels -- door die besparing
    komen beide helften van pagina 1 toevallig vrijwel exact op dezelfde
    hoogte uit (570px elk, 0px verschil, geen kunstmatige opvulling nodig).
  - **Getest**: `node --check`, en jsdom-smoketests per onderdeel (het
    knipper-bugfix-scenario met een echt via `buildMetingXlsx()` opgebouwd
    bestand, de exacte ruw/1x/2x-smoothing-vergelijking op de echte
    `20260731_153533_Falco_Premium.xlsx`, het "grootste beweging"-scenario
    met een kunstmatig storend blipje, de 50/50-paginaverdeling en dat
    "Uitkomst" vóór en "Criteria" ná het paginasplitspunt staan, en de
    volledige XLSX/PDF-exportpijplijn). **Niet visueel gecontroleerd** in
    een echte browser (geen browser-tooling in dit environment).
- **2026-08-19** (vervolg 4: browser-compat-suggesties zijn nu platform-
  bewuste links, op verzoek), in `FAR-500.html`: elke genoemde browser/app
  in de "geen Web Bluetooth"-melding is nu een link -- op Android een
  `intent://`-link die Chrome/Edge rechtstreeks opent (met de Play Store als
  terugval als de app niet geïnstalleerd is), op iOS de App Store-pagina van
  Bluefy (`id1492822055`, geverifieerd via websearch, niet gegokt), op
  desktop de officiële download-pagina's van chrome.com/microsoft.com/edge.
  Gedetecteerd via `navigator.userAgent` (Android/iOS/overig). Getest met 3
  gesimuleerde user-agents (Android/Firefox, iOS/Safari, Desktop/Firefox) --
  elk toont exact de juiste, platform-specifieke link.
- **2026-08-19** (vervolg 3: Meting-kaart knippert, upload-wachtwoord
  "tijn", verplichte velden vóór rapportage, grafiek-verfijningen, alles op
  verzoek, alles in `FAR-500.html`):
  - **"Meting"-kaart** (Naam/Notities) knippert nu ook (dikke oranje rand,
    zelfde `blinkOrangeCard`-mechaniek als Handvat-posities) zolang niet
    BEIDE velden gevuld zijn. Placeholders aangepast naar "bv. Falco /
    Premium" resp. "bv. hoog / AzorTerschelling 28kg". Handvat HOOG/LAAG
    hebben nu standaard 160cm/20cm ingevuld.
  - **Upload-wachtwoord naar GitHub is "tijn"** -- dit is ook **live gezet**
    op de Cloudflare Worker (`wrangler secret put UPLOAD_SECRET`, bevestigd
    met een test-upload + daarna dat test-asset weer verwijderd van de
    release); het invulveld heeft "tijn" als standaardwaarde.
  - **"Download CSV" nu ook adv-only** (was al zo voor "Meetgegevens
    XLSX") -- in Standard mode is er dus geen losse ruwe-data-download meer,
    alleen de PDF-rapportknop. De banner-tekst "voer L in (of leg posities
    vast) voor beoordeling" is vervangen door het neutrale "nog geen
    meting" (de knipperende Handvat-posities-velden maken dat al duidelijk).
  - **Verplichte velden vóór rapportage** (`reportPrereqsOk()`): Naam,
    Notities, Handvat HOOG/LAAG (cm) moeten gevuld zijn ÉN beide posities
    moeten daadwerkelijk vastgelegd zijn (`geo.angHigh`/`angLow`, niet
    alleen de hoogte ingevuld) -- anders popup "Niet alle gegevens zijn
    ingevuld." en geen rapport/upload. Geldt voor beide rapport-knoppen
    (XLSX + PDF). Upload-code + de rapport-knoppen (incl. "Upload naar
    GitHub") staan nu ook onderaan het knoppenblok, na alle invulvelden.
  - **Oranje breakaway-kader (omlaag) in 2 delen bevestigd**: de achtergrond
    wordt al per hoek-kolom op basis van de hoogte op dát punt berekend
    (F_LOW/F_HIGH), dus als het 20cm-venster de 135cm-grens kruist verspringt
    het kader vanzelf tussen de twee marges (210N resp. 127,5N) -- geen
    codewijziging nodig geweest, met een gerichte test bevestigd (2
    verschillende bandhoogtes bij een venster dat de grens kruist).
  - **Krachtraster** toegevoegd aan `drawAngleForceChart()`: dunne
    horizontale lijnen + labels bij ±85/±100/±140/±210N (live én PDF, gedeelde
    routine).
  - **Afstand-tickmarks (0/10/20cm) gesplitst**: omhoog-tickmarks nu
    bovenaan de grafiek, omlaag-tickmarks blijven vlak boven de onderste
    (hoek-)as. `TOPAX`-factor (in `drawAngleForceChart()`) van 2.3→3.0 om
    ruimte te maken voor de extra rij; `CHART_H` in
    `buildAnalyseReportCanvas()` daarop afgestemd.
  - "Omhoog"/"omlaag"-labels op de meetlijn nu bij -35° resp. -15° (was
    -30°/-15°), op verzoek zodat ze altijd op de bovenste resp. onderste
    curve vallen.
  - **Getest**: `node --check`, en jsdom-smoketests per onderdeel (default-
    waarden, knop-/veld-volgorde, popup + géén rapport/upload bij
    ontbrekende gegevens vs. wél rapport+upload bij complete gegevens, het
    2-delige oranje kader, en de boven/onder-tickplaatsing). Live bevestigd
    dat het nieuwe GitHub-wachtwoord werkt. **Niet visueel gecontroleerd**
    in een echte browser (geen browser-tooling in dit environment).
- **2026-08-19** (vervolg: L-werkelijk-tekenfix, PDF-opmaak 2-pagina's,
  C4-override-knop, en een forse Standard-mode-vereenvoudiging, alles op
  verzoek, alles in `FAR-500.html`):
  - **Bugfix**: `Lused()` gebruikte een handmatig ingevoerde "L werkelijk"
    (altijd positief, het is een meetlint-lengte) rechtstreeks, ook als de
    berekende L (uit de hoeken) negatief moet zijn voor deze hoek-conventie
    -- gaf bij beide Vconsist-metingen (L ingevoerd=+208, berekend=-199,03)
    een omgekeerde/onzinnige handvathoogte. Nu neemt `Lused()` het teken van
    de berekende L over en past dat toe op |Lman|.
  - **PDF-rapport nu 2 pagina's** (A4 portrait): pagina 1 = tekst/setup/
    criteria/uitkomst (2.5x grotere tekst dan het oorspronkelijke ontwerp,
    boven uitgelijnd), pagina 2 = de grafiek (30% hoger dan de vorige 1.5x-
    versie, vult de paginabreedte). Nodig omdat beide wensen samen niet meer
    op 1 pagina pasten zonder dat de grafiek de breedte niet meer zou
    vullen. `drawAngleForceChart()` kreeg een `axisFontPx`-parameter
    (default 15, voor de live UI) zodat het rapport (axisFontPx=38, geeft na
    de ~0.32x paginaschaal ~12pt) en de live grafiek elk hun eigen passende
    astekstgrootte hebben -- gedeelde code, anders zou 12pt-astekst in de
    live UI enorm ogen. "Omhoog"/"omlaag"-labels hangen nu vast aan het
    werkelijke punt op de meetlijn (rond -30°/-15°, binnen de betreffende
    beweging) i.p.v. op een vaste positie.
  - **C4 (snelheid/versnelling)**: default max. snelheid in de UI 40→60cm/s.
    Nieuwe knop "Overschrijven" (+ 2 velden) direct bij de Rapportage-
    knoppen om de limiet vlak vóór het genereren van een rapport aan te
    passen (schrijft door naar de bestaande vmax/amax-velden, geen apart
    bijgehouden status). Bevestigd (met een echte meting) dat de bestaande
    FAIL-banner al reactief de live ingestelde limiet + de 4-punts-smoothing
    gebruikt -- geen code-wijziging nodig, wel bleek de FAIL bij dat
    voorbeeld van versnelling te komen, niet snelheid.
  - **Standard mode fors vereenvoudigd** (`body.standard .adv-only`-regel
    hergebruikt, plus wat losse `display:none`/herstructurering):
    verborgen: sensor-oriëntatiekaart + "Reset bereik", "Tare 0°",
    "Meetgegevens XLSX" (CSV-knop blijft), samples/t/accu/status-regel, de
    snelheidslijn+legenda in de "Verloop"-grafiek, en (binnen Handvat-
    posities) het hoek-override-blok + "H/L berekend" + "H/L werkelijk".
    De banner toont in Standard mode nooit meer "TEST GEFAALD" op snelheid/
    versnelling (altijd "METING OK" zolang er wel meetdata is) -- ongewijzigd
    in Advanced. "Geschiedenis — laatste metingen" is nu altijd verborgen
    (in beide modi, `style="display:none"`, functies blijven intact).
    "Handvat-posities → geometrie" is niet langer adv-only (nu in Standard
    zichtbaar) omdat Standard-gebruikers de positie wél moeten kunnen
    vastleggen: de HOOG/LAAG-velden knipperen (dikke oranje rand, 1s aan/1s
    uit, CSS `@keyframes`) zolang die positie niet vastgelegd is
    (`updateCaptureBlink()`, aangeroepen vanuit `computeGeo()`), met een
    vaste toelichting erboven. Upload-code-veld + de (hernoemde) knop "Maak
    rapportage + Analyse PDF" zijn ook in Standard mode zichtbaar (XLSX-
    rapportknop blijft adv-only); die PDF-knop uploadt het rapport nu in
    **beide** modi altijd ook naar GitHub (naast de lokale download),
    hergebruikt de bestaande upload-code/relay.
  - **Getest**: `node --check`, en losse jsdom-smoketests per onderdeel
    (Vconsist-cijfers vóór/na de Lused()-fix, 2-pagina-PDF vult op beide
    pagina's de breedte + juiste astekstgrootte, Standard/Advanced-verschil
    in de banner met dezelfde overschrijding-data, capture-blink aan/uit bij
    vastleggen, en een gesimuleerde klik op de PDF-knop die bevestigt dat er
    precies 1 upload-aanroep gebeurt met de juiste bestandsnaam). **Niet
    visueel gecontroleerd** in een echte browser (geen browser-tooling in
    dit environment) -- graag zelf Standard/Advanced en het PDF-rapport
    even doorlopen.
- **2026-08-19** (grafiek-optimalisatie: oranje breakaway-kader eenzijdig +
  PDF portrait A4 + live grafiek = PDF-grafiek, op verzoek), alles in
  `FAR-500.html`, `drawAngleForceChart()`:
  - **Oranje breakaway-kader niet meer gespiegeld naar het verkeerde teken.**
    Voorheen werd het 150%-marge-kader symmetrisch rond 0N getekend (zowel
    boven als onder groen), wat overlapte/verwarrend was omdat een
    omhooggaande beweging altijd trekkracht (positief) geeft en een
    omlaaggaande beweging afhankelijk van leeg/beladen positief óf negatief.
    Nu: (1) het kader zit altijd op 0-20cm cirkelbaanlengte vanaf de
    **start** van de beweging (`move.arcStart`, dezelfde referentie als de
    afstand-tickmarks -- bewust niet het engine-anker); (2) bij de
    omhooggaande beweging (eerste "up" in `res.moves`) uitsluitend aan de
    positieve kant (+base..+150%·base); (3) bij de omlaaggaande beweging
    (eerste "down") aan de kant die het gemiddelde teken van de gemeten
    kracht in dat venster aangeeft (`graceForceSign()`, sommeert `force_N`
    over de samples binnen 20cm van `arcStart`). Geverifieerd met een
    synthetische meting (positieve kracht bij omhoog, negatieve bij omlaag):
    0 hoek-kolommen met oranje op beide kanten tegelijk (was de bug), 104
    kolommen boven / 104 onder, precies gescheiden per beweging.
  - **PDF naar A4 portrait** (was landscape): `PAGE_W=595, PAGE_H=842`. Omdat
    de breedte nu de beperkende dimensie is, vult de grafiek vanzelf de
    paginabreedte met de bestaande marge (`MARGIN=24`, ongewijzigde
    schaal-/centreerlogica in `buildAnalysisPdfBlob()`).
  - **Grafiek 1,5x hoger**: `CHART_H` 478→678px (plot-rechthoek 400→600px,
    TOPAX/BOTAX-marges ongewijzigd) voor preciezere krachtaflezing.
  - **Dikkere lijnen**: F=0N-nullijn 1→2.2px, meetdata-lijn 1.4→2.8px (2x,
    op verzoek).
  - **Labels "omhoog"/"omlaag"** toegevoegd op vaste posities (-30° resp.
    -15°) als oriëntatiehulp.
  - **Live "Kracht vs. hoek"-grafiek (`drawXY()`) hergebruikt nu
    `drawAngleForceChart()`** zodra L én H bekend zijn (`haveHL()`) -- zelfde
    envelope/assen/labels/lijndiktes als het PDF-rapport, incl. een expliciet
    witte achtergrond (die routine gebruikt vaste hex-kleuren i.p.v.
    thema-variabelen, dus zonder witte achtergrond zou het in dark mode
    onleesbaar zijn). Canvas-hoogte 300→420px voor de extra assen. Zonder
    L/H valt de live grafiek terug op de oude eenvoudige weergave (gewone
    lijn/punten, thema-kleuren, geen envelope).
  - **Snelheid/versnelling-smoothing** (`smooth4()`, vorige sessie later op
    deze dag toegevoegd) geldt al voor beide grafieken en is dit keer
    ongewijzigd bevestigd te werken.
  - **Getest**: `node --check`, en jsdom-smoketests die bevestigen: (1) het
    oranje kader nooit meer op beide kanten binnen dezelfde hoek-kolom; (2)
    de PDF-MediaBox exact 595x842pt is; (3) `lineWidth` 2.2 en 2.8 daad-
    werkelijk gebruikt worden; (4) `redraw()` zowel mét als zonder L/H
    foutloos doorloopt (live-grafiek fallback-pad). **Niet visueel
    gecontroleerd** in een echte browser/PDF-viewer (dit environment heeft
    geen browser-tooling) -- graag zelf even een meting met geometrie
    bekijken, zowel live als in het PDF-rapport.
- **2026-08-19** (bugfix handle_height_cm + snelheid/versnelling-smoothing,
  op verzoek na een gemelde fout), alles in `FAR-500.html`:
  - **Probleem**: in een echte meting (Falco Premium, hoek hoog ≈ -0.7°, hoek
    laag ≈ -47.9°) werd `handle_height_cm` bij -49,2° berekend als 305,6cm
    i.p.v. ~16cm, en daalde de hoogte juist ri.p.v. te stijgen naarmate de
    hoek richting 0° ging.
  - **Root cause**: `computeGeo()`/`Lused()` eisten `L>0`. Wiskundig is L de
    unieke oplossing van `yHigh=H-L·sin(angHigh)` / `yLow=H-L·sin(angLow)` —
    er is geen vrije tekenkeuze. Onder de hoek-conventie sinds de 2026-08-06
    teken-omdraai komt L bij een fysiek correcte meting regelmatig negatief
    uit (hoogte stijgt dan met de hoek i.p.v. te dalen, zoals hier). De
    `>0`-eis verwierp zo'n geldige geometrie als "ongeldig", of liet (via
    `btnOldMeasLoad`, dat L/H rechtstreeks uit de "L/H berekend"-metadata van
    een geladen oud bestand overnam i.p.v. herberekende) een **verouderde,
    inconsistente L** van een eerder geladen bestand staan zodra de hoeken
    daarna wél klopten — dat laatste is precies hoe het gemelde bestand
    (`20260819_131101_Falco_Premium.xlsx`) ontstond.
  - **Fix (3 plekken)**: `computeGeo()`/`Lused()` accepteren nu ook negatieve
    L (alleen 0/NaN/null is ongeldig); `btnOldMeasLoad` herberekent L/H nu
    altijd vers via `computeGeo()` i.p.v. de opgeslagen "L/H berekend" te
    vertrouwen.
  - **Snelheid/versnelling gladgestreken** (op verzoek, "grillig door ruis op
    de hoek"): nieuwe `smooth4()` — trailing 4-punts voortschrijdend
    gemiddelde, toegepast op `A.spd` ná `derivAt()` en op `A.acc` (die zelf
    weer van de gladgestreken snelheid afgeleid wordt, profiteert dus
    automatisch mee). Trailing i.p.v. gecentreerd zodat het ook tijdens een
    live meting werkt (geen toekomstige samples nodig).
  - **Bestaande .xlsx-bestanden nagerekend** (jsdom, de echte
    `parseFarMetingWorkbook()`/`computeGeo()`/`analyze()` uit dit bestand
    gebruikt, geen herimplementatie in Python): van de 9 bestanden onder de
    GitHub-`recordings`-release had **geen enkel** al een foute
    `handle_height_cm`-kolom — 4 hadden al correcte data (fysiek juiste,
    stijgende hoogte), 3 hadden geen hoek vastgelegd (geen kolom om fout te
    hebben), en 2 hadden wél een hoek maar géén berekende kolommen (de oude
    `>0`-eis verwierp die toen al). Bij diezelfde 6-met-hoek bestanden toont
    het tekstveld "L berekend" wel het verkeerde teken (H blijft toevallig
    vrijwel gelijk omdat sin(hoek hoog) ~0 is) — puur cosmetisch, wordt door
    de app nooit gebruikt (zie de `btnOldMeasLoad`-fix hierboven) dus
    bewust ongewijzigd gelaten op de GitHub-release (nog niet gevraagd om
    dat ook te corrigeren). De twee **lokale** (nog niet geüploade)
    `Falco_Premium`-bestanden van vandaag (11:41 en 13:11) waren wél echt
    fout door de hierboven beschreven `btnOldMeasLoad`-bug: een
    gecorrigeerde kopie van de 13:11-meting staat als
    `Downloads\20260819_131101_Falco_Premium_fixed.xlsx` (origineel
    ongewijzigd gelaten, stond open in Excel); de 11:41-meting is niet te
    herstellen (hoek hoog en hoek laag zijn daarin toevallig identiek
    vastgelegd, dus geometrisch onoplosbaar -- opnieuw meten nodig).
  - **Getest**: `node --check`, en een headless jsdom-run die (1) met de
    exacte Falco Premium-cijfers bevestigt dat `computeGeo()` nu L≈-191.84
    geeft (i.p.v. "ongeldig"), (2) dat `handle_height_cm` bij -49,2°/0° nu
    ~15,1cm/~160,3cm is en monotoon stijgt, (3) dat `smooth4()` het juiste
    4-punts voortschrijdend gemiddelde geeft, (4) dat het laden van een
    bewust "stale" (inconsistente) oud bestand nu L/H wél vers herberekent
    i.p.v. de oude waarde te laten staan. **Nog niet gevraagd/gedaan**: de
    kracht-vs-hoek-grafiek in het rapport optimaliseren (op verzoek eerst
    deze rekenfout oplossen) — dat staat nog open, zie eerdere sessie
    hierboven voor de huidige stand van die grafiek.
- **2026-08-19** (handmatige hoek-invoer + GitHub-links/PDF-dropdown +
  norm-achtergrond in de rapport-grafiek, op verzoek), alles in `FAR-500.html`:
  - **Probleem dat is opgelost**: een oude meting laden vanaf GitHub kan een
    handvathoogte (yHigh/yLow) hebben zonder de bijbehorende hoek (angHigh/
    angLow) — bv. exports van vóór deze functie, of een meting waarbij niet op
    "vastleggen" is geklikt. Zonder die hoeken kan L/H (en dus de cirkelbaan)
    niet berekend worden en blijven de "Rapportage + Analyse XLSX/PDF"-knoppen
    uitgeschakeld.
  - **Knop "Vastleggen" heet nu "Huidige positie vastleggen"** (`capHigh`/
    `capLow`), om te verduidelijken dat hij de live hoek van dat moment
    vastlegt. Ernaast twee nieuwe invulvelden ("Hoek HOOG/LAAG (deg, hand.)",
    defaults **+1.0** resp. **-46.0**, de op verzoek gegeven waarden) plus een
    knop **"Overschrijf posities"** (`btnOverridePositions`) die geo.angHigh/
    geo.angLow (en geo.yHigh/yLow uit de bestaande hoogtevelden) forceert en
    `computeGeo()` opnieuw draait — ook zonder live BLE-verbinding.
  - **"Oude meting laden (GitHub)"-kaart uitgebreid**: een link "Alle
    bestanden bekijken op GitHub →" naar
    `github.com/dynteq/far-500/releases/tag/recordings`, en een tweede
    dropdown die alleen de `.pdf`-assets uit diezelfde release toont (al
    eerder gegenereerde "Rapportage + Analyse PDF"-bestanden), met knop
    "PDF-rapport openen/downloaden" die 'm via de bestaande
    `GET /download?name=`-Worker-proxy (far500-upload-worker, zie
    Architectuur) ophaalt en downloadt. `refreshOldMeasurementList()` is
    hiervoor verbreed tot `refreshGhAssets()`: haalt de releaselijst 1x op en
    filtert 'm in twee dropdowns (`.xlsx` voor "laden in het analyse-scherm",
    `.pdf` voor rapporten) — de metingen-dropdown toonde voorheen ook al
    `_rapport.xlsx`/`_geschiedenis.xlsx`/`.pdf`-bestanden zonder filter (die
    gaven bij laden altijd al een duidelijke foutmelding via
    `parseFarMetingWorkbook()`, dus geen gedragswijziging, wel iets opgeruimder).
  - **Kracht-vs-hoek-grafiek in het rapport (XLSX + PDF) flink uitgebreid**
    (`drawAngleForceChart()`), op verzoek:
    - **Hoek-as (onderaan)**: nu bij elke 5° een dun streepje over de volle
      grafiekhoogte + label (was alleen de twee eindpunten).
    - **Handvathoogte-as (bovenaan)**: dikke streep + label bij **135 cm** en
      **170 cm** — de hoek die bij die hoogte hoort wordt berekend als de
      inverse van de bestaande hoogteformule (`Hused()-Lused()*sin(theta)`,
      dezelfde als in `analyze()`), dus consistent met de rest van de tool.
    - **Afgelegde afstand (onderaan, kort streepje)**: 0/10/20 cm-markeringen
      vanaf het startpunt van resp. de eerste "omhoog"- en de eerste
      "omlaag"-beweging uit `res.moves` (dezelfde beweging-segmentatie als de
      C1-C4-engine, `segmentMoves()`), elk vanaf hun eigen 0-punt — dus twee
      onafhankelijke reeksen ↑/↓.
    - **Achtergrondkleuren**: groen = toegestaan (±140N onder 135cm, ±85N
      tussen 135-170cm — `CRIT.F_LOW`/`F_HIGH`/`H_THRESH`), oranje = de
      bestaande 150%-breakaway-marge (`CRIT.GRACE_FACT`/`GRACE_ARC`) binnen de
      eerste 20cm van een beweging, rood = daarbuiten (incl. >170cm/<0cm
      handvathoogte, C3). De oranje "marge-vensters" zijn afgeleid uit
      `res.moves[].anchorArc` (dezelfde anker-logica als `runForceCheck()`/
      `buildEnvelope()`) omgerekend naar hoek via dezelfde `arc_cm`-referentie
      (`th0`) als `analyze()` gebruikt — dus wiskundig identiek aan wat de
      engine per sample als "in_grace" aanmerkt, geen aparte benadering.
      De meetpunten zelf werden al rood gekleurd bij een C1/C2/C3-
      overschrijding (`res.violation_indices`/`height_violation_indices`,
      ongewijzigd) — dat komt nu dus ook visueel overeen met de rode
      achtergrond.
    - Chart-blok is intern hoger gemaakt (`CHART_H` 400→478px) om ruimte te
      reserveren voor de extra assen; het eigenlijke plot-rechthoek is
      ongewijzigd 400px hoog.
  - **far500-force-check (Python/CLI) is bewust niet aangepast** — dit verzoek
    ging over de UI-eigen rapport-PDF/XLSX (canvas-gerasterd, zie eerdere
    sessie), niet over de losse CI-tool. Als de force-check-PDF dezelfde
    assen/achtergrond moet krijgen is dat een apart, nog niet gedaan stukje
    werk (zie de "twee implementaties, hou ze in sync"-notitie hierboven bij
    Architectuur).
  - **Getest**: `node --check` op het geëxtraheerde script (geen syntaxfouten),
    en een headless jsdom-run (canvas-2D-context en `canvas.toBlob()`
    gestubd, zelfde aanpak als eerdere sessies — dit environment heeft geen
    browser-tooling) die: (1) bevestigt dat de GitHub-link en beide dropdowns
    (met een gemockte `fetch()`-respons van 2 .xlsx- en 1 .pdf-asset) correct
    gevuld/gefilterd worden; (2) de knoppekst-wijziging en de default-waarden
    (+1.0/-46.0) van de nieuwe hoek-velden verifieert; (3) de
    "Overschrijf posities"-knop daadwerkelijk `geo.angHigh`/`angLow` zet en
    `computeGeo()` een geldige L/H teruggeeft; (4) een synthetische
    up-piek-down-meting (met bewust een C2- en C3-overschrijding erin)
    zonder exceptions door `buildAnalyseReportCanvas()` →
    `buildReportXlsxBlob()` → `buildAnalysisPdfBlob()` haalt, en dat de
    output een geldig zip/XLSX (`PK\x03\x04`-signature, ingelezen door
    Python's `zipfile`/`openpyxl`: juiste 2 tabbladen) resp. geldige PDF
    (`%PDF-`-header, 1 pagina A4-landscape, ingelezen door `pypdf`) is.
    **Niet visueel gecontroleerd** hoe de nieuwe assen/achtergrondkleuren er
    in een echte browser/PDF-viewer precies uitzien (positionering van
    labels/streepjes, leesbaarheid van de legenda-tekst in de titelregel) —
    graag met een echte meting (of een van de bestaande GitHub-recordings +
    de nieuwe hoek-override) een keer een PDF genereren en visueel nalopen.
- **2026-08-11** (rapport-grafiek + C4-normwijziging, op verzoek):
  - **Grafiek in het analyse-rapport (XLSX/PDF) vervangen.** De twee
    tijd-gebaseerde grafieken ("Bedienkracht vs. toegestane envelope" en
    "Handvathoogte vs. tijd") zijn vervangen door **één** grafiek die er
    identiek uitziet als de live "Kracht vs. hoek"-grafiek in de UI: vaste
    assen hoek **-50°(links)..+5°(rechts)**, kracht **-250N(onder)..+250N(boven)**
    — hergebruikt de al-bestaande `ANGLE_MIN/MAX`/`FORCE_MIN/MAX`/`clamp()` uit
    de GRAFIEKEN-sectie i.p.v. eigen constanten. Punten die C1/C2 (kracht >
    envelope) óf C3 (handvathoogte > H_MAX) overschrijden worden rood i.p.v.
    cyaan gemarkeerd (`drawAngleForceChart()`, vervangt `drawEnvelopeChart`/
    `drawHeightChart` die zijn verwijderd). `runForceCheck()` geeft nu ook
    `height_violation_indices` (C3 per sample) terug, naast de bestaande
    kracht-`violation_indices` (C1/C2).
  - **A6 — C4 (snelheid/versnelling) telt niet meer mee in het eindoordeel.**
    Op aangeven van de gebruiker: C4 is geen officiële eis, dus een
    C4-overschrijding mag geen FAIL veroorzaken. Doorgevoerd in **beide**
    implementaties voor consistentie (zie ook de "twee implementaties, hou ze
    in sync"-notitie verderop): far500-force-check kreeg een nieuwe
    `Criteria.C4_AFFECTS_OVERALL` (default `False`, dus ook automatisch een
    `--c4-affects-overall`-CLI-flag via het generieke `fields(Criteria)`-mechanisme
    in `cli.py`) en `Analysis.overall_pass` respecteert die vlag; de
    JS-poort in `FAR-500.html` kreeg dezelfde `CRIT.C4_AFFECTS_OVERALL`
    (default `false`). C4 wordt nog wel gerapporteerd (PASS/FAIL, gelabeld
    "C4 (info)") maar beïnvloedt de exit-code/eindoordeel niet meer zolang
    die vlag op de default staat. far500-force-check's README (Criteria-tabel)
    en de twee JS/Python-teksten voor de C4-regel zijn bijgewerkt om dit uit
    te leggen. Twee bestaande tests in `far500-force-check/tests/test_engine.py`
    (die een C4-only-overschrijding als `overall_pass is False` verwachtten)
    zijn aangepast + een nieuwe test toegevoegd die bevestigt dat
    `C4_AFFECTS_OVERALL=True` het oude gedrag terugzet — alle 30 tests slagen.
  - **Getest**: `pytest` (far500-force-check, 30/30), en voor de UI-kant een
    headless-jsdom-run die bevestigt dat een C4-only-overschrijding nu
    `overall_pass=true` geeft, dat een C3-overschrijding wél in
    `height_violation_indices` terechtkomt, en dat de XLSX/PDF nog steeds
    geldig zijn (`zipfile`/`pypdf`). **Niet visueel gecontroleerd** hoe de
    nieuwe grafiek er in een echte PDF-viewer uitziet.
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
- **2026-08-17**: GitHub-org hernoemd van `DynteqBV` naar `dynteq` (gebruiker had het
  bedrijfsaccount net omgedoopt). GitHub redirect de oude org-URL's automatisch,
  maar alle harde verwijzingen in de codebase zijn toch bijgewerkt naar `dynteq`:
  `README.md` (live-UI-link + releases-link), `far500-upload-worker/README.md`,
  `far500-upload-worker/src/index.js` (`OWNER`-constante) en `FAR-500.html`
  (`GH_LIST_URL`). De Cloudflare Worker-URL zelf
  (`far500-upload-worker.workers.dev`) is een los subdomein en hoefde niet aangepast.
  **Nog te doen (buiten deze sessie, geen repo-schrijftoegang hiervoor):** de
  Cloudflare Worker opnieuw deployen (`npx wrangler deploy` vanuit
  `far500-upload-worker/`) zodat de nieuwe `OWNER`-waarde ook live actief wordt —
  tot die deploy draait de Worker nog op de oude `DynteqBV`-waarde (werkt dankzij
  GitHub's redirect, maar beter alsnog deployen).
- Optimalisatie van UI
- Bouwen / valideren van hoek justering


## Belangrijke beslissingen
- Gebruik JWT authentication
- Geen Redux, alleen React Query

## Werkwijze
- Geef eerst een plan voordat je grote wijzigingen maakt.
- Pas bestaande architectuur niet aan zonder overleg.
- Houdt bij hoe (via welke COM-poort en software instellingen) je succesvol de firmware geupload hebt (in dit document CLAUDE.md)
- **Na elke `git push` van `FAR-500.html` naar `master`**: altijd controleren
  (bv. `curl -s https://dynteq.github.io/far-500/FAR-500.html | grep "build"`
  tegen het nieuwe buildstamp, zo nodig even pollen tot GitHub Pages bijgewerkt
  is) of de live pagina de nieuwe versie toont, en dat pas terugmelden aan de
  gebruiker -- niet alleen "gepusht" melden zonder te bevestigen dat 'm ook
  echt live staat.

## Naamgeving
- Project/device heet **FAR-500** (BLE naam, OLED-tekst, UI). Let op: "Sauter FH 500" in de architectuurbeschrijving is de naam van het externe krachtmeetinstrument (UART-bron) en is dus NIET hernoemd.
- PlatformIO-project: `FAR-500_ESP32C6/` (env `esp32-c6-devkitm-1`), sketch in `src/`.

## Upload-log (firmware -> ESP32-C6)
- **2026-08-24**: meerdere reflashes naar COM10 tijdens het uitzoeken van de
  BLE-DUMP-betrouwbaarheidssaga (zie Huidige status hierboven voor de volle
  toedracht) — telkens met het inmiddels standaard `PYTHONIOENCODING=utf-8`-
  commando uit de 2026-08-21-entry hieronder, board werd tussendoor een paar
  keer losgekoppeld/weer aangesloten op USB (elke keer `[System.IO.Ports.
  SerialPort]::getportnames()` gebruikt om te checken of COM10 er weer was).
  Volgorde: (1) INDICATE+retry geflashed -> bleek averechts te werken; (2)
  terug naar NOTIFY + niet-blokkerende dump-state-machine + OLED-scherm
  geflashed -> "instant timeout"-bug ontdekt tijdens testen; (3) millis()-
  underflow-fix + kortere OLED-foutmelding geflashed -> **live bevestigd
  werkend** door de gebruiker. Alle builds: RAM 8.2%, Flash ~63.0-63.1%
  (verwaarloosbare groei), alle 4 image-delen steeds hash-geverifieerd.
- **2026-08-21**: OLED-tellervakje (26→32px) + `#SIZE`-DUMP-progressregel +
  9e telemetrieveld (measNum) succesvol geupload naar COM10 (bash-tool, dit
  Claude Code-environment op Windows). Board was aanvankelijk niet
  aangesloten/gedetecteerd (geen COM10, geen VID_303A/10C4/1A86-match) --
  na opnieuw aansluiten wel gevonden.
  - **Nieuw probleem gevonden (Windows-console + esptool 5.x)**: `pio run -t
    upload` hing 2x minutenlang zonder enige output/foutmelding (leek op een
    vastgelopen reset-handshake, was het niet). Oorzaak: esptool 5.3.0's
    voortgangsbalk gebruikt Unicode blok-tekens (█/░); PlatformIO's
    Windows-console-echo-thread crasht daarop (`UnicodeEncodeError`, cp1252
    kan die tekens niet coderen) en de hoofd-`pio run`-thread blijft daarna
    voor altijd wachten op die dode thread (silent deadlock, geen
    stacktrace zichtbaar totdat je het proces zelf non-blocking uitleest).
  - **Fix/nieuw commando**: zet ook `PYTHONIOENCODING=utf-8`, vóór
    `PLATFORMIO_CORE_DIR`:
    `export PYTHONIOENCODING=utf-8; export PLATFORMIO_CORE_DIR="C:\pio"; &
    "$HOME\.platformio\penv\Scripts\pio.exe" run -d
    "C:\dev\FAR-500\FAR-500_ESP32C6" -t upload --upload-port COM10` — hiermee
    liep de upload de 3e keer wél gewoon door tot een normale afronding.
  - Build: RAM 8.2% (26940/327680 B), Flash 63.0% (825188/1310720 B) --
    ongewijzigd t.o.v. de laatste bekend-goede build (alleen kleine
    logica-/layout-wijzigingen, geen nieuwe dependencies). Alle 4 image-
    delen (bootloader/partitions/boot_app0/firmware) geschreven + hash-
    geverifieerd. `-t upload` raakt nooit de LittleFS-partitie aan, dus de 2
    hang-pogingen (die alleen de bootloader deels herschreven voor ze
    vastliepen) hebben nooit risico gevormd voor de opgeslagen metingen.
    Live bevestigd door de gebruiker: OLED-vakje en de DUMP-%-balk werken
    beide goed.
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
- **2026-08-19**: `UPLOAD_SECRET` op verzoek gewijzigd naar `tijn` (was het
  oorspronkelijke willekeurig gegenereerde wachtwoord) via `wrangler secret
  put UPLOAD_SECRET` (waarde weer non-interactief doorgepiped). Bewust een
  makkelijk te onthouden, dus ook makkelijker te raden wachtwoord — voor dit
  doel (laagdrempelig teamslotje op een release met alleen meetdata, geen
  echte beveiliging) een geaccepteerde afweging. Live getest met een
  curl-upload (`X-Upload-Secret: tijn`) — 200 OK, asset succesvol
  aangekomen; het test-asset is daarna weer verwijderd van de release. De UI
  (`FAR-500.html`, veld `ghSecret`) heeft `tijn` nu als standaardwaarde.