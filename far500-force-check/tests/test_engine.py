import pytest

from far500_force_check.constants import Criteria
from far500_force_check.engine import analyze, segment_moves

from fixtures.synth import build_stroke, chain, identity_height, make_recording

C = Criteria()  # defaults: H_THRESH=135, H_MAX=170, F_LOW=140, F_HIGH=85, GRACE_FACT=1.5, GRACE_ARC=20, DETECT_ARC=10


# ---- DoD (a)/basis: normale slag met vroege breakaway, blijft binnen envelope (PASS) ----

def test_up_move_binnen_envelope_pass():
    def force_fn(dist):
        if dist <= 10:
            return 100 + 4.5 * dist  # 100 -> 145, kruist 140 rond dist~8.9
        if dist <= 30:
            return max(80.0, 145 - (dist - 10) * 3.5)
        return 80.0

    samples = build_stroke(0.0, 0.05, 0, 130, identity_height, force_fn, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c1_ok is True
    assert analysis.overall_pass is True
    assert analysis.moves[0].anchor_reason == "breakaway_detected"
    assert analysis.moves[0].anchor_arc == pytest.approx(9.0)  # eerste sample (stap 1cm) met |F|>=140


def test_down_move_boven135_binnen_envelope_pass():
    def force_fn(dist):
        if dist <= 10:
            return 50 + 4.0 * dist  # 50 -> 90, kruist 85 rond dist~8.75
        if dist <= 30:
            return max(30.0, 90 - (dist - 10) * 3.0)
        return 30.0

    samples = build_stroke(0.0, 0.05, 165, 140, identity_height, force_fn, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c2_ok is True
    assert analysis.overall_pass is True
    assert analysis.moves[0].anchor_reason == "breakaway_detected"


# ---- DoD (b): geen overschrijding binnen 10cm -> anker = start, mét grace-venster vanaf start ----

def test_geen_overschrijding_anker_is_start_met_grace_vanaf_start():
    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, lambda dist: 130.0, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    move = analysis.moves[0]
    assert move.anchor_reason == "start"
    assert move.anchor_arc == pytest.approx(0.0)

    # binnen 20cm van het anker (=start): marge actief -> envelope 210N (ook al kwam de kracht
    # nooit boven de basislimiet 140N, het venster geldt sowieso vanaf het bewegingsbegin)
    within = [r for i, r in enumerate(analysis.sample_results) if samples[i].arc_cm <= 20]
    beyond = [r for i, r in enumerate(analysis.sample_results) if samples[i].arc_cm > 20]
    assert all(r.in_grace for r in within)
    assert all(r.envelope_N == pytest.approx(210.0) for r in within)
    assert all(not r.in_grace for r in beyond)
    assert all(r.envelope_N == pytest.approx(140.0) for r in beyond)
    assert analysis.overall_pass is True


# ---- DoD (a) precies: anker exact op het omslagpunt (niet op bewegingsstart) ----

def test_anker_exact_op_omslagpunt():
    def force_fn(dist):
        return 100 + 6.0 * dist if dist <= 10 else 160.0

    samples = build_stroke(0.0, 0.05, 0, 30, identity_height, force_fn, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    move = analysis.moves[0]
    assert move.anchor_reason == "breakaway_detected"
    # bij stap=1cm is dist=7 de eerste sample met force>=140 (100+6*7=142)
    assert move.anchor_arc == pytest.approx(7.0)
    assert move.anchor_arc != pytest.approx(0.0)  # niet op bewegingsstart


# ---- Grace-overschrijding -> FAIL ----

def test_grace_overschrijding_binnen_venster_fail():
    def force_fn(dist):
        return 100.0 if dist < 1 else 220.0  # direct ver over de 150%-marge (210N)

    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, force_fn, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c1_ok is False
    assert analysis.overall_pass is False
    violated = [samples[i].arc_cm for i in analysis.violation_indices]
    assert any(a <= 21 for a in violated)  # overschrijding zit binnen het (dan al gesloten) grace-venster


def test_kracht_zakt_pas_na_grace_venster_terug_fail():
    def force_fn(dist):
        if dist < 1:
            return 100.0
        if dist <= 21:
            return 200.0  # binnen grace (<=20cm vanaf anker op dist=1) en onder 210N: OK
        return 145.0  # venster gesloten (dist>21) -> basislimiet 140N geldt weer -> FAIL

    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, force_fn, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c1_ok is False
    assert analysis.overall_pass is False
    # de overschrijding hoort bij een sample ná het venster, niet erbinnen
    violated = [samples[i].arc_cm for i in analysis.violation_indices]
    assert all(a > 21 for a in violated)


# ---- DoD (c): overgang door 135cm binnen het grace-venster ----

def test_beweging_kruist_135cm_binnen_grace_venster():
    # korte piek boven 140N bij dist=1 triggert het anker (arc=121, laag-gebied, F_LOW);
    # de beweging eindigt op arc=140, dus het hele vervolg blijft binnen het 20cm-venster
    # vanaf het anker (arc<=141) en kruist ondertussen H_THRESH=135.
    def force_fn(dist):
        if dist < 1:
            return 100.0
        if dist < 3:
            return 145.0
        return 120.0  # altijd < 127.5 (=1.5*85) en < 210

    samples = build_stroke(0.0, 0.05, 120, 140, identity_height, force_fn, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    move = analysis.moves[0]
    # samples net voorbij 135cm, nog binnen het venster: envelope moet 1.5*F_HIGH=127.5 zijn
    # (base_i wisselt naar F_HIGH zodra hoogte>=135, maar in_grace blijft actief tot dist=20 vanaf anker)
    crossing = [
        r
        for i, r in enumerate(analysis.sample_results)
        if samples[i].handle_height_cm >= C.H_THRESH and abs(samples[i].arc_cm - move.anchor_arc) <= C.GRACE_ARC
    ]
    assert crossing, "test-opzet moet minstens 1 sample binnen het venster >135cm hebben"
    assert all(r.in_grace for r in crossing)
    assert all(r.envelope_N == pytest.approx(1.5 * C.F_HIGH) for r in crossing)
    assert analysis.overall_pass is True


# ---- DoD (d): hoogte > 170cm -> FAIL, onafhankelijk van kracht ----

def test_hoogte_boven_170_fail_onafhankelijk_van_kracht():
    samples = build_stroke(0.0, 0.05, 0, 175, identity_height, lambda dist: 50.0, step=1.0)
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c1_ok is True
    assert analysis.c2_ok is True
    assert analysis.c3_ok is False
    assert analysis.overall_pass is False
    assert analysis.max_height_cm == pytest.approx(175.0)


# ---- DoD (e): snelheid/versnelling boven limiet -> C4 FAIL, maar A6: C4 is geen
# officiële eis, dus overall_pass blijft standaard ongevoelig voor C4 ----

def test_snelheid_boven_limiet_geeft_c4_fail_maar_geen_overall_fail():
    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, lambda dist: 50.0, step=1.0)
    samples[5].speed_cm_s = 25.0  # > default vmax 20 cm/s
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c1_ok is True and analysis.c2_ok is True and analysis.c3_ok is True
    assert analysis.c4_ok is False
    assert analysis.overall_pass is True  # A6: C4 telt niet mee, tenzij C4_AFFECTS_OVERALL


def test_versnelling_boven_limiet_geeft_c4_fail_maar_geen_overall_fail():
    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, lambda dist: 50.0, step=1.0)
    samples[5].accel_cm_s2 = 999.0  # > default amax 40 cm/s2
    rec = make_recording(samples)
    analysis = analyze(rec, C)

    assert analysis.c4_ok is False
    assert analysis.overall_pass is True


def test_c4_affects_overall_kan_expliciet_aangezet_worden():
    from dataclasses import replace

    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, lambda dist: 50.0, step=1.0)
    samples[5].speed_cm_s = 25.0  # > default vmax 20 cm/s
    rec = make_recording(samples)
    strict = replace(C, C4_AFFECTS_OVERALL=True)
    analysis = analyze(rec, strict)

    assert analysis.c4_ok is False
    assert analysis.overall_pass is False


def test_limiet_uit_metadata_wordt_gebruikt_ipv_default():
    samples = build_stroke(0.0, 0.05, 0, 40, identity_height, lambda dist: 50.0, step=1.0)
    samples[5].speed_cm_s = 25.0  # zou met de default (20) FAIL geven
    rec = make_recording(samples, max_snelheid_cm_s=30.0)  # ruimere limiet uit de metadata
    analysis = analyze(rec, C)
    assert analysis.c4_ok is True


# ---- meerdere op-neer-slagen + ruis rond omkeerpunten ----

def test_meerdere_slagen_ruis_onder_hysterese_geeft_geen_extra_segmenten():
    strokes = chain(
        build_stroke(0.0, 0.05, 0, 50, identity_height, lambda d: 50.0, step=1.0),
        build_stroke(3.0, 0.05, 50, 0, identity_height, lambda d: 50.0, step=1.0),
        build_stroke(6.0, 0.05, 0, 50, identity_height, lambda d: 50.0, step=1.0),
    )
    # ruis van 1cm (< REV_HYST=2.0) rond de omkeerpunten mag geen extra segment opleveren
    strokes[50].arc_cm += 1.0
    strokes[100].arc_cm -= 1.0

    moves = segment_moves(strokes, C.REV_HYST)
    assert [m[0] for m in moves] == ["up", "down", "up"]


def test_echte_omkering_boven_hysterese_geeft_wel_extra_segment():
    strokes = chain(
        build_stroke(0.0, 0.05, 0, 50, identity_height, lambda d: 50.0, step=1.0),
        build_stroke(3.0, 0.05, 50, 0, identity_height, lambda d: 50.0, step=1.0),
        build_stroke(6.0, 0.05, 0, 50, identity_height, lambda d: 50.0, step=1.0),
    )
    # een echte tussentijdse omkering van 3cm (> REV_HYST=2.0) middenin de eerste slag
    strokes[20].arc_cm += 3.0
    strokes[21].arc_cm -= 3.0

    moves = segment_moves(strokes, C.REV_HYST)
    assert len(moves) > 3
