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
- **2026-08-19**: `UPLOAD_SECRET` op verzoek gewijzigd naar `tijn` (was het
  oorspronkelijke willekeurig gegenereerde wachtwoord) via `wrangler secret
  put UPLOAD_SECRET` (waarde weer non-interactief doorgepiped). Bewust een
  makkelijk te onthouden, dus ook makkelijker te raden wachtwoord — voor dit
  doel (laagdrempelig teamslotje op een release met alleen meetdata, geen
  echte beveiliging) een geaccepteerde afweging. Live getest met een
  curl-upload (`X-Upload-Secret: tijn`) — 200 OK, asset succesvol
  aangekomen; het test-asset is daarna weer verwijderd van de release. De UI
  (`FAR-500.html`, veld `ghSecret`) heeft `tijn` nu als standaardwaarde.