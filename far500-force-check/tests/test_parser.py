import math

import pytest

from far500_force_check.parser import DATA_HEADER, ParseError, load_recording


def _meta_line(*fields) -> str:
    return "# " + ",".join(str(f) for f in fields)


BASE_META_LINES = [
    _meta_line("FAR-500 meting"),
    _meta_line("Naam (Merk/Type)", "TestMerk TM1"),
    _meta_line("Notities (Positie/Belasting)", "test, met komma, erin"),
    _meta_line("Datum/tijd meting", "31-7-2026, 14:23:05"),
    _meta_line("Kalibratie", "3D-vector (2-punts justering 0°/45°)"),
    _meta_line("Handvat hoog (cm)", 168, "hoek hoog (deg)", 12.5),
    _meta_line("Handvat laag (cm)", 60, "hoek laag (deg)", -5),
    _meta_line("L ingevoerd (cm)", 50),
    _meta_line("L berekend (cm)", 48),
    _meta_line("H ingevoerd (cm)", ""),
    _meta_line("H berekend (cm)", 160),
    _meta_line("Limiet-controle", "aan"),
    _meta_line("Max snelheid (cm/s)", 20),
    _meta_line("Max versnelling (cm/s2)", 40),
    _meta_line("Markering 20cm omhoog (s)", 1.2),
    _meta_line("Markering 20cm omlaag (s)", 3.4),
    _meta_line("Markering 135cm omhoog (s)", 1.0),
    _meta_line("Markering 135cm omlaag (s)", 3.0),
    _meta_line("Resultaat", "GESLAAGD"),
]


def _write_csv(tmp_path, meta_lines, header, data_rows, name="meting.csv"):
    lines = list(meta_lines) + ["", ",".join(header)]
    lines += [",".join(str(v) for v in row) for row in data_rows]
    path = tmp_path / name
    # newline="" voorkomt dat write_text de \n's in de string nog eens vertaalt
    # naar os.linesep (op Windows anders \r\n -> \r\r\n).
    path.write_text("\r\n".join(lines), encoding="utf-8", newline="")
    return path


def test_datum_met_komma_wordt_correct_geparsed(tmp_path):
    path = _write_csv(
        tmp_path,
        BASE_META_LINES,
        DATA_HEADER,
        [[0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0]],
    )
    rec = load_recording(path)
    assert rec.meta.datum_tijd == "31-7-2026, 14:23:05"


def test_notitie_met_komma_wordt_correct_geparsed(tmp_path):
    path = _write_csv(
        tmp_path,
        BASE_META_LINES,
        DATA_HEADER,
        [[0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0]],
    )
    rec = load_recording(path)
    assert rec.meta.notities == "test, met komma, erin"


def test_arity4_handvat_regel(tmp_path):
    path = _write_csv(
        tmp_path,
        BASE_META_LINES,
        DATA_HEADER,
        [[0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0]],
    )
    rec = load_recording(path)
    assert rec.meta.handvat_hoog_cm == pytest.approx(168)
    assert rec.meta.hoek_hoog_deg == pytest.approx(12.5)
    assert rec.meta.handvat_laag_cm == pytest.approx(60)
    assert rec.meta.hoek_laag_deg == pytest.approx(-5)


def test_onbekend_label_gaat_naar_raw_extra(tmp_path):
    lines = list(BASE_META_LINES) + [_meta_line("Toekomstig veld", "foo", "bar")]
    path = _write_csv(tmp_path, lines, DATA_HEADER, [[0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0]])
    rec = load_recording(path)
    assert rec.meta.raw_extra["Toekomstig veld"] == ["foo,bar"]


def test_l_h_precedence_ingevoerd_boven_berekend(tmp_path):
    path = _write_csv(
        tmp_path,
        BASE_META_LINES,
        DATA_HEADER,
        [[0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0]],
    )
    rec = load_recording(path)
    # L ingevoerd=50 aanwezig -> heeft voorrang op L berekend=48
    assert rec.meta.l_used == pytest.approx(50)
    # H ingevoerd="" (leeg) -> valt terug op H berekend=160
    assert rec.meta.h_used == pytest.approx(160)


def test_ontbrekende_arc_en_height_worden_herberekend(tmp_path):
    meta_lines = [
        _meta_line("L ingevoerd (cm)", 100),
        _meta_line("H ingevoerd (cm)", 200),
    ]
    header = ["t_s", "angle_deg", "force_N"]
    rows = [
        [0.0, 0.0, 10.0],
        [0.5, 30.0, 10.0],
        [1.0, 60.0, 10.0],
    ]
    path = _write_csv(tmp_path, meta_lines, header, rows)
    rec = load_recording(path)

    L, H = 100.0, 200.0
    th0 = math.radians(0.0)
    expected_arc = [L * (th0 - math.radians(a)) for a in (0.0, 30.0, 60.0)]
    expected_height = [H - L * math.sin(math.radians(a)) for a in (0.0, 30.0, 60.0)]

    for s, arc, height in zip(rec.samples, expected_arc, expected_height):
        assert s.arc_cm == pytest.approx(arc)
        assert s.handle_height_cm == pytest.approx(height)

    assert "arc_cm" in rec.recomputed_columns
    assert "handle_height_cm" in rec.recomputed_columns
    assert "speed_cm_s/accel_cm_s2" in rec.recomputed_columns

    # zelfde windowed-derivative-algoritme als derivAt() in FAR-500.html (WIN=0.3s)
    expected_speed = [0.0, (expected_arc[1] - expected_arc[0]) / 0.5, (expected_arc[2] - expected_arc[1]) / 0.5]
    for s, v in zip(rec.samples, expected_speed):
        assert s.speed_cm_s == pytest.approx(v)


def test_ontbrekende_arc_zonder_l_geeft_duidelijke_fout(tmp_path):
    header = ["t_s", "angle_deg", "force_N"]
    rows = [[0.0, 0.0, 10.0], [0.5, 30.0, 10.0]]
    path = _write_csv(tmp_path, [], header, rows)  # geen L/H in metadata
    with pytest.raises(ParseError, match="arc_cm"):
        load_recording(path)


def test_ontbrekende_height_zonder_h_geeft_duidelijke_fout(tmp_path):
    meta_lines = [_meta_line("L ingevoerd (cm)", 100)]
    header = ["t_s", "angle_deg", "force_N", "arc_cm"]
    rows = [[0.0, 0.0, 10.0, 0.0], [0.5, 30.0, 10.0, -52.36]]
    path = _write_csv(tmp_path, meta_lines, header, rows)
    with pytest.raises(ParseError, match="handle_height_cm"):
        load_recording(path)


def test_onbekende_extra_kolom_wordt_genegeerd(tmp_path):
    header = list(DATA_HEADER) + ["extra_kolom"]
    rows = [[0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0, 999]]
    path = _write_csv(tmp_path, BASE_META_LINES, header, rows)
    rec = load_recording(path)
    assert len(rec.samples) == 1
    assert rec.samples[0].force_N == pytest.approx(5.0)


def test_xlsx_zelfde_structuur_als_csv(tmp_path):
    openpyxl = pytest.importorskip("openpyxl")

    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = "Meting"
    ws.append(["FAR-500 meting"])
    ws.append(["Naam (Merk/Type)", "TestMerk TM1"])
    ws.append(["Notities (Positie/Belasting)", "test, met komma, erin"])
    ws.append(["Handvat hoog (cm)", 168, "hoek hoog (deg)", 12.5])
    ws.append(["L ingevoerd (cm)", 50])
    ws.append(["H ingevoerd (cm)", 160])
    ws.append(["Resultaat", "GESLAAGD"])
    ws.append([])
    ws.append(list(DATA_HEADER))
    ws.append([0.0, 10.0, 5.0, 0.0, 0.0, 0.0, 140.0])
    ws.append([0.1, 12.0, 6.0, 1.5, 15.0, 10.0, 141.0])
    path = tmp_path / "meting.xlsx"
    wb.save(path)

    rec = load_recording(path)
    assert rec.source_format == "xlsx"
    assert rec.meta.naam == "TestMerk TM1"
    assert rec.meta.notities == "test, met komma, erin"
    assert rec.meta.handvat_hoog_cm == pytest.approx(168)
    assert rec.meta.hoek_hoog_deg == pytest.approx(12.5)
    assert rec.meta.l_used == pytest.approx(50)
    assert len(rec.samples) == 2
    assert rec.samples[1].force_N == pytest.approx(6.0)
