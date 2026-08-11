# far500-force-check

Bedienkracht-normtoetsing en XLSX/PDF-rapportgenerator voor FAR-500-metingen.
Los subproject naast de firmware/UI (zie `far500-upload-worker/` voor het
precedent) — geen dependency op FAR-500.html of de firmware, alleen op het
exportformaat dat ze produceren.

De laptop-UI (`FAR-500.html`) toetst zelf alleen snelheid/versnelling. Dit
subproject toetst de **bedienkracht** (C1-C4, zie hieronder) op een export en
maakt daar een net opgemaakt rapport van.

## Installatie

```
cd far500-force-check
python -m venv .venv
.venv/Scripts/activate        # Windows; op macOS/Linux: source .venv/bin/activate
pip install -e ".[dev]"
```

Enige runtime-dependency: `openpyxl`. Voor de PDF-stap lokaal is daarnaast
[LibreOffice](https://www.libreoffice.org/) nodig (command `soffice` op PATH)
— zie "PDF lokaal genereren" hieronder.

## Gebruik

```
python -m far500_force_check <input.csv|xlsx> -o <rapport.xlsx>
```

Opties:
- `-o/--output <pad>` — pad voor het volledige rapport (2 tabbladen). Default:
  `<input>_rapport.xlsx`.
- `--print-only <pad>` — schrijf daarnaast een klein bronbestand met **alleen**
  `setup_analyse` (voor PDF-conversie, zie CI).
- `--pdf` — render dat print-only-bestand ook direct naar PDF via `soffice`
  (als LibreOffice op PATH staat).
- Elke constante uit §Criteria is te overschrijven, bv. `--h-thresh 130`,
  `--use-abs-force false`.

Exit-code: `0` bij een PASS-eindoordeel, `1` bij FAIL, `2` bij een fout tijdens
het inlezen (bv. ontbrekende verplichte kolom). Een FAIL is een geldige
meetuitkomst, geen tool-fout.

## Criteria (C1-C4)

| # | Omschrijving | Constante(n) |
|---|---|---|
| C1 | Onder `H_THRESH` (135 cm) handvathoogte: kracht ≤ `F_LOW` (140 N). Bij het begin van een beweging mag de kracht binnen de eerste `GRACE_ARC` cm (20 cm) vanaf het anker oplopen tot `GRACE_FACT`×`F_LOW` (210 N). | `H_THRESH`, `F_LOW`, `GRACE_ARC`, `GRACE_FACT` |
| C2 | Boven `H_THRESH`: kracht ≤ `F_HIGH` (85 N), met dezelfde breakaway-marge tot `GRACE_FACT`×`F_HIGH` (127,5 N) — **A1**: dit geldt ook boven 135 cm (`GRACE_ABOVE=True`). | `F_HIGH`, `GRACE_ABOVE` |
| C3 | Handvathoogte ≤ `H_MAX` (170 cm) — **A3**: overschrijding is een harde FAIL (`HEIGHT_MAX_IS_FAIL=True`). | `H_MAX` |
| C4 | Snelheid ≤ *Max snelheid* en versnelling ≤ *Max versnelling* uit de metadata van de export (anders default 20 cm/s / 40 cm/s²) — **A6**: dit is geen officiële eis, C4 wordt nog wel gerapporteerd (PASS/FAIL) maar telt standaard niet mee in `overall_pass`/de exit-code (`C4_AFFECTS_OVERALL=False`; zet op `True` om C4 weer als harde FAIL-reden te laten meetellen). | `C4_AFFECTS_OVERALL` |

Kracht wordt getoetst als `|force_N|` (druk én trek) — **A2**
(`USE_ABS_FORCE=True`).

### Anker en het breakaway-venster (A4/A5)

Voor elke beweging (up/down, gesegmenteerd met `REV_HYST`-hysterese op
`arc_cm`) wordt eerst een **anker** bepaald: binnen de eerste `DETECT_ARC`
cm (10 cm) van de beweging, als `|F|` de basislimiet (F_LOW/F_HIGH, bepaald
door de handvathoogte bij het *begin* van de beweging) overschrijdt, ligt
het anker op het punt waar die limiet voor het eerst bereikt wordt; anders
ligt het anker op het bewegingsbegin.

**Belangrijk (A4), expliciet gekozen bij het bouwen van deze tool:** het
`GRACE_ARC`-venster (150% marge) geldt **altijd** voor de eerste 20 cm
vanaf het anker — ook als er géén vroege overschrijding was. Het anker
bepaalt alléén *waar* dat venster begint, nooit *of* er een venster is.
Dit is de enige lezing die consistent is met de envelope-formule (die geen
uitzondering kent op basis van de ankerreden) en is bevestigd met de
opdrachtgever.

Binnen een beweging kan de handvathoogte door `H_THRESH` heen kruisen
(bv. een opgaande slag die van laag naar hoog gaat): de basislimiet
(`base_i`) wisselt dan per sample met de hoogte, maar het grace-venster
blijft actief zolang de sample binnen `GRACE_ARC` van het (ene) anker van
die beweging valt — dus een hoge-regio-sample vlak na het anker kan nog
`GRACE_FACT`×`F_HIGH` (127,5 N) toegestaan krijgen.

Een tweede, minder centrale interpretatiekeuze: de basislimiet voor de
ankerdetectie wordt bepaald door de handvathoogte bij het *begin* van de
beweging (net als `base_i` in de envelope-formule zelf), **niet** door de
bewegingsrichting. Dit generaliseert het voorbeeld uit de oorspronkelijke
spec ("up-beweging onder 135cm / down-beweging boven 135cm") naar alle
bewegingen, en is nodig omdat de envelope-formule zelf ook richtingsonafhankelijk
is. Voor de gebruikelijke volledige op-neer-slag (omhoog vanaf laag, omlaag
vanaf hoog) maakt dit geen verschil met een letterlijke lezing.

## Rapport-opbouw

- **`setup_analyse`** — titelblok, setup-samenvatting (L/H ingevoerd,
  berekend én gebruikt; handvatposities; snelheid/versnelling-limieten),
  de volledig uitgeschreven criteria/constanten, een PASS/FAIL-uitkomsttabel
  per criterium met onderbouwing, een groot gekleurd eindoordeel, en een
  grafiek: gestapeld groen (toegestane envelope) / rood (verboden gebied) met
  de krachtlijn erover en rode markers op overschrijdingen. Optioneel een 2e
  grafiek (hoogte vs tijd, met 135/170cm-referentielijnen) als
  `handle_height_cm` beschikbaar is. Landscape A4, past op 1 pagina breed.
- **`data`** — de ruwe meetdata plus afgeleide kolommen `region`, `limit_N`
  (de per-sample envelope), `in_grace`, `force_ok`. Vetgedrukte header,
  autofilter, freeze op rij 1.

**Niet geïmplementeerd** (bewust, als "nice to have" laten vallen): verticale
referentielijnen op de bestaande 20cm/135cm-markeringen uit de metadata in de
kracht-grafiek. De enige manier om dat in een gestapelde AreaChart te tonen is
een fragiele workaround (een extra, grotendeels-`None`-serie met een piek op
precies dat ene sample) die het risico op een kapotte grafiek verhoogt zonder
duidelijke meerwaarde t.o.v. de tijd-as die de kracht/envelope-grafiek al
heeft.

## Testen

```
pytest
```

Alle tests draaien op **synthetische fixtures** (`tests/fixtures/synth.py` +
`tests/fixtures/sample_normal.csv`) — er is geen echte FAR-500-device-export
beschikbaar geweest tijdens het bouwen van deze tool. `tests/test_parser.py`
dekt o.a. de komma-in-metadata-edge-cases (Datum/tijd, Notities) en de
L/H-ingevoerd-vs-berekend-voorrangsregel; `tests/test_engine.py` dekt de
anker-/envelope-/segmentatielogica (breakaway binnen/na 10cm, grace-venster,
regio-overgang, hoogte->FAIL, snelheid/versnelling->FAIL, ruis vs. echte
omkering); `tests/test_xlsx_writer.py` is een structurele rookproef op het
rapport (tabbladen, header, autofilter, grafiek-aanwezigheid, round-trip).

**Vóór productiegebruik**: valideer dit 1x tegen een echte FAR-500.html-export
(CSV én XLSX) zodra die beschikbaar is — met name de XLSX-inleesroute is
alleen tegen een zelf-nagebouwde fixture getest, niet tegen de daadwerkelijke
(non-standaard-minimale) zip-writer uit FAR-500.html.

## PDF lokaal genereren

Vereist LibreOffice (`soffice` op PATH):

```
python -m far500_force_check meting.csv --print-only setup_analyse.xlsx --pdf
```

Zonder `--pdf` (of als `soffice` ontbreekt) wordt alleen het XLSX-bronbestand
geschreven; de tool waarschuwt dan en slaat de PDF-stap over in plaats van te
falen.

## CI / releases

`.github/workflows/force-check.yml` (repo-root) draait `pytest`, genereert een
demo-rapport op `tests/fixtures/sample_normal.csv`, rendert dat naar PDF via
LibreOffice, en publiceert XLSX+PDF als build-artefact. Bij een gepubliceerde
GitHub Release worden ze ook als release-asset geüpload.

**Tag-afspraak:** de hoofdrepo gebruikt al de release-tag `recordings` voor
geüploade metingen (`far500-upload-worker/`). Gebruik voor releases van dit
subproject een eigen prefix, bv. `force-check-v1.0.0`, om verwarring met die
meting-opslag te voorkomen.
