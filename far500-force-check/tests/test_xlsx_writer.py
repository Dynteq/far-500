import pytest

openpyxl = pytest.importorskip("openpyxl")

from far500_force_check.constants import Criteria
from far500_force_check.engine import analyze
from far500_force_check.xlsx_writer import build_workbook

from fixtures.synth import build_stroke, identity_height, make_recording


def _sample_analysis():
    def force_fn(dist):
        if dist <= 10:
            return 100 + 4.5 * dist
        return 80.0

    samples = build_stroke(0.0, 0.05, 0, 130, identity_height, force_fn, step=1.0)
    rec = make_recording(samples, naam="TestMerk TM1", notities="unit test")
    return analyze(rec, Criteria())


def test_workbook_heeft_2_tabbladen_met_juiste_namen():
    wb = build_workbook(_sample_analysis())
    assert wb.sheetnames == ["setup_analyse", "data"]


def test_data_tab_header_is_vet_en_autofilter_aan():
    wb = build_workbook(_sample_analysis())
    ws = wb["data"]
    assert ws["A1"].value == "t_s"
    assert ws["A1"].font.bold is True
    assert ws.auto_filter.ref is not None
    assert ws.freeze_panes == "A2"


def test_data_tab_bevat_afgeleide_kolommen_en_juiste_rijaantal():
    analysis = _sample_analysis()
    wb = build_workbook(analysis)
    ws = wb["data"]
    header = [c.value for c in next(ws.iter_rows(min_row=1, max_row=1))]
    for col in ("region", "limit_N", "in_grace", "force_ok"):
        assert col in header
    assert ws.max_row == 1 + len(analysis.recording.samples)


def test_setup_analyse_heeft_grafiek_en_eindoordeel():
    analysis = _sample_analysis()
    wb = build_workbook(analysis)
    ws = wb["setup_analyse"]
    assert len(ws._charts) >= 1

    verdict_found = any(
        cell.value and "EINDOORDEEL" in str(cell.value)
        for row in ws.iter_rows()
        for cell in row
    )
    assert verdict_found


def test_workbook_round_trip_via_load_workbook(tmp_path):
    analysis = _sample_analysis()
    wb = build_workbook(analysis)
    path = tmp_path / "rapport.xlsx"
    wb.save(path)

    wb2 = openpyxl.load_workbook(path)
    assert wb2.sheetnames == ["setup_analyse", "data"]
    assert len(wb2["setup_analyse"]._charts) >= 1


def test_include_data_tab_false_geeft_alleen_setup_analyse():
    analysis = _sample_analysis()
    wb = build_workbook(analysis, include_data_tab=False)
    assert wb.sheetnames == ["setup_analyse"]
