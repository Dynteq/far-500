"""Lees een FAR-500-meetexport (CSV of XLSX) in tot een Recording.

Spiegelt de export-code in FAR-500.html:
- metaRows() (regel 544-568) voor de metadata-kop
- DATA_HEADER/dataRow() (regel 569-572) voor de datarijen
- Lused()/Hused() (regel 249-250) voor de ingevoerd-vs-berekend-voorrangsregel
- arc_cm/handle_height_cm-formules (regel 505-506) voor herberekening

Bestandsopbouw (één werkblad, geen apart 'data'-tabblad):
  "# <label>,<waarde>"     (of 4 velden voor de Handvat-hoog/laag-regels)
  ...
  ""                       (lege regel)
  t_s,angle_deg,force_N,arc_cm,speed_cm_s,accel_cm_s2,handle_height_cm
  <datarij>
  ...

CSV-specifiek risico: de meta-regels worden zonder quoting geschreven
(`"# "+r.join(",")`). "Datum/tijd meting" (nl-NL, bv. "31-7-2026, 14:23:05")
en "Notities (Positie/Belasting)" (vrije tekst) kunnen dus zelf een komma
bevatten. Omdat het label nooit een komma bevat, is `split(",", 1)`
voldoende voor alle 2-veld-regels — alleen de twee 4-veld-regels
(Handvat hoog/laag) hebben een aparte, volledige split.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Optional

from .model import Meta, Recording, Sample

DATA_HEADER = [
    "t_s",
    "angle_deg",
    "force_N",
    "arc_cm",
    "speed_cm_s",
    "accel_cm_s2",
    "handle_height_cm",
]
REQUIRED_DATA_COLUMNS = ("t_s", "angle_deg", "force_N")
DERIV_WIN_S = 0.3  # spiegelt WIN in FAR-500.html (derivAt)


class ParseError(Exception):
    pass


_STRING = "string"
_NUMERIC = "numeric"
_BOOL_AAN_UIT = "bool_aan_uit"

TITLE_LABELS = {"FAR-500 meting"}
ARITY4_LABELS = {"Handvat hoog (cm)", "Handvat laag (cm)"}

# label -> (dataclass-veldnamen, soort). Arity4-labels hebben 2 veldnamen
# (waarde 1 en waarde 2); alle andere hebben er 1.
META_FIELD_SPEC: dict[str, tuple[tuple[str, ...], str]] = {
    "Naam (Merk/Type)": (("naam",), _STRING),
    "Notities (Positie/Belasting)": (("notities",), _STRING),
    "Datum/tijd meting": (("datum_tijd",), _STRING),
    "Kalibratie": (("kalibratie",), _STRING),
    "Handvat hoog (cm)": (("handvat_hoog_cm", "hoek_hoog_deg"), _NUMERIC),
    "Handvat laag (cm)": (("handvat_laag_cm", "hoek_laag_deg"), _NUMERIC),
    "L ingevoerd (cm)": (("l_ingevoerd_cm",), _NUMERIC),
    "L berekend (cm)": (("l_berekend_cm",), _NUMERIC),
    "H ingevoerd (cm)": (("h_ingevoerd_cm",), _NUMERIC),
    "H berekend (cm)": (("h_berekend_cm",), _NUMERIC),
    "Limiet-controle": (("limiet_controle",), _BOOL_AAN_UIT),
    "Max snelheid (cm/s)": (("max_snelheid_cm_s",), _NUMERIC),
    "Max versnelling (cm/s2)": (("max_versnelling_cm_s2",), _NUMERIC),
    "Markering 20cm omhoog (s)": (("markering_20cm_omhoog_s",), _NUMERIC),
    "Markering 20cm omlaag (s)": (("markering_20cm_omlaag_s",), _NUMERIC),
    "Markering 135cm omhoog (s)": (("markering_135cm_omhoog_s",), _NUMERIC),
    "Markering 135cm omlaag (s)": (("markering_135cm_omlaag_s",), _NUMERIC),
    "Resultaat": (("resultaat",), _STRING),
}


def load_recording(path: str | Path) -> Recording:
    path = Path(path)
    suffix = path.suffix.lower()
    if suffix == ".csv":
        recording = _parse_csv_text(path.read_text(encoding="utf-8-sig"))
    elif suffix in (".xlsx", ".xlsm"):
        recording = _parse_xlsx(path)
    else:
        raise ParseError(f"Onbekend bestandsformaat: '{suffix}' (verwacht .csv of .xlsx)")
    _recompute_missing_columns(recording)
    return recording


# ---------------------------------------------------------------- meta ----

def _cast_value(raw: Any, kind: str) -> Any:
    if isinstance(raw, str):
        raw = raw.strip()
        if raw == "":
            raw = None
    if raw is None:
        return "" if kind == _STRING else None
    if kind == _STRING:
        return str(raw)
    if kind == _BOOL_AAN_UIT:
        s = str(raw).strip().lower()
        if s == "aan":
            return True
        if s == "uit":
            return False
        return None
    try:
        return float(raw)
    except (TypeError, ValueError):
        return None


def _apply_meta_cells(cells: list, meta: Meta) -> None:
    if not cells:
        return
    label = str(cells[0]).strip()
    if label in TITLE_LABELS or label not in META_FIELD_SPEC:
        if label and label not in TITLE_LABELS:
            meta.raw_extra[label] = cells[1:]
        return
    field_names, kind = META_FIELD_SPEC[label]
    if label in ARITY4_LABELS:
        raw_values = [cells[1] if len(cells) > 1 else None, cells[3] if len(cells) > 3 else None]
    else:
        raw_values = [cells[1] if len(cells) > 1 else None]
    for name, raw in zip(field_names, raw_values):
        setattr(meta, name, _cast_value(raw, kind))


def _parse_meta_csv(meta_lines: list[str]) -> Meta:
    meta = Meta()
    for line in meta_lines:
        content = line[2:] if line.startswith("# ") else line
        if "," not in content:
            continue  # titelregel, geen key/value
        tentative_label = content.split(",", 1)[0].strip()
        cells = content.split(",") if tentative_label in ARITY4_LABELS else content.split(",", 1)
        _apply_meta_cells(cells, meta)
    return meta


def _strip_trailing_none(row: list) -> list:
    row = list(row)
    while row and row[-1] is None:
        row.pop()
    return row


def _parse_meta_xlsx(meta_rows: list[tuple]) -> Meta:
    meta = Meta()
    for row in meta_rows:
        cells = _strip_trailing_none(row)
        _apply_meta_cells(cells, meta)
    return meta


def _is_blank_row(row) -> bool:
    return all(c is None or (isinstance(c, str) and c.strip() == "") for c in row)


# ---------------------------------------------------------------- data ----

def _sample_from_values(values: dict[str, Optional[float]]) -> Sample:
    for name in REQUIRED_DATA_COLUMNS:
        if values.get(name) is None:
            raise ParseError(f"Ontbrekende waarde voor verplichte kolom '{name}' in een datarij.")
    return Sample(
        t_s=values["t_s"],
        angle_deg=values["angle_deg"],
        force_N=values["force_N"],
        arc_cm=values.get("arc_cm"),
        speed_cm_s=values.get("speed_cm_s"),
        accel_cm_s2=values.get("accel_cm_s2"),
        handle_height_cm=values.get("handle_height_cm"),
    )


def _parse_csv_text(text: str) -> Recording:
    lines = text.splitlines()
    idx = 0
    meta_lines: list[str] = []
    while idx < len(lines) and lines[idx].startswith("# "):
        meta_lines.append(lines[idx])
        idx += 1
    meta = _parse_meta_csv(meta_lines)

    while idx < len(lines) and lines[idx].strip() == "":
        idx += 1
    if idx >= len(lines):
        raise ParseError("Geen dataheader gevonden in CSV-bestand.")

    header = [h.strip() for h in lines[idx].split(",")]
    idx += 1
    col_index = {name: i for i, name in enumerate(header)}
    for required in REQUIRED_DATA_COLUMNS:
        if required not in col_index:
            raise ParseError(f"Verplichte kolom '{required}' ontbreekt in de dataheader.")

    samples: list[Sample] = []
    for line in lines[idx:]:
        if line.strip() == "":
            continue
        cells = line.split(",")
        values: dict[str, Optional[float]] = {}
        for name in DATA_HEADER:
            i = col_index.get(name)
            if i is None or i >= len(cells):
                values[name] = None
                continue
            raw = cells[i].strip()
            values[name] = float(raw) if raw != "" else None
        samples.append(_sample_from_values(values))

    return Recording(meta=meta, samples=samples, source_format="csv")


def _parse_xlsx(path: Path) -> Recording:
    import openpyxl

    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    ws = wb.worksheets[0]
    rows = list(ws.iter_rows(values_only=True))

    idx = 0
    meta_rows: list[tuple] = []
    while idx < len(rows) and not _is_blank_row(rows[idx]):
        meta_rows.append(rows[idx])
        idx += 1
    meta = _parse_meta_xlsx(meta_rows)

    while idx < len(rows) and _is_blank_row(rows[idx]):
        idx += 1
    if idx >= len(rows):
        raise ParseError("Geen dataheader gevonden in XLSX-bestand.")

    header = [str(h).strip() for h in _strip_trailing_none(list(rows[idx]))]
    idx += 1
    col_index = {name: i for i, name in enumerate(header)}
    for required in REQUIRED_DATA_COLUMNS:
        if required not in col_index:
            raise ParseError(f"Verplichte kolom '{required}' ontbreekt in de dataheader.")

    samples: list[Sample] = []
    for row in rows[idx:]:
        if _is_blank_row(row):
            continue
        values: dict[str, Optional[float]] = {}
        for name in DATA_HEADER:
            i = col_index.get(name)
            if i is None or i >= len(row):
                values[name] = None
                continue
            v = row[i]
            if isinstance(v, str):
                v = v.strip()
                v = None if v == "" else float(v)
            values[name] = float(v) if v is not None else None
        samples.append(_sample_from_values(values))

    return Recording(meta=meta, samples=samples, source_format="xlsx")


# ---------------------------------------------------------- herberekening ----

def _recompute_missing_columns(recording: Recording) -> None:
    meta = recording.meta
    samples = recording.samples
    if not samples:
        return
    L = meta.l_used
    H = meta.h_used

    if any(s.arc_cm is None for s in samples):
        if L is None:
            raise ParseError(
                "arc_cm ontbreekt in de export en kan niet herberekend worden: "
                "'L ingevoerd (cm)'/'L berekend (cm)' zijn beide onbekend."
            )
        th0 = math.radians(samples[0].angle_deg)
        for s in samples:
            if s.arc_cm is None:
                s.arc_cm = L * (th0 - math.radians(s.angle_deg))
        recording.recomputed_columns.append("arc_cm")

    if any(s.handle_height_cm is None for s in samples):
        if L is None or H is None:
            raise ParseError(
                "handle_height_cm ontbreekt in de export en kan niet herberekend worden: "
                "'L' en/of 'H' (ingevoerd of berekend) zijn onbekend."
            )
        for s in samples:
            if s.handle_height_cm is None:
                s.handle_height_cm = H - L * math.sin(math.radians(s.angle_deg))
        recording.recomputed_columns.append("handle_height_cm")

    if any(s.speed_cm_s is None or s.accel_cm_s2 is None for s in samples):
        _recompute_derivatives(samples)
        recording.recomputed_columns.append("speed_cm_s/accel_cm_s2")


def _deriv_at(i: int, arr: list[float], ts: list[float], win: float) -> float:
    j = i - 1
    while j > 0 and ts[i] - ts[j] < win:
        j -= 1
    if j < 0 or ts[i] == ts[j]:
        return 0.0
    return (arr[i] - arr[j]) / (ts[i] - ts[j])


def _recompute_derivatives(samples: list[Sample], win: float = DERIV_WIN_S) -> None:
    ts = [s.t_s for s in samples]
    arc = [s.arc_cm for s in samples]
    speed = [_deriv_at(i, arc, ts, win) for i in range(len(samples))]
    for s, v in zip(samples, speed):
        if s.speed_cm_s is None:
            s.speed_cm_s = v
    speed_full = [s.speed_cm_s for s in samples]
    accel = [_deriv_at(i, speed_full, ts, win) for i in range(len(samples))]
    for s, v in zip(samples, accel):
        if s.accel_cm_s2 is None:
            s.accel_cm_s2 = v
