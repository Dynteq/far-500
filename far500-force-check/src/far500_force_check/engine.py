"""Segmentatie, anker- en envelope-logica voor de C1-C4 bedienkracht-toetsing.

Zie far500-force-check/README.md ("Grace-venster" en "Anker bij niet-typische
bewegingen") voor de twee interpretatiekeuzes die hier geïmplementeerd zijn:

1. Het GRACE_ARC-venster (150% marge) geldt altijd voor de eerste GRACE_ARC cm
   van een beweging, ongeacht of het anker op het bewegingsbegin ligt of op
   een later gedetecteerd doorbraakpunt — het anker bepaalt alleen *waar* het
   venster begint, nooit *of* er een venster is.
2. De basislimiet voor de ankerdetectie (Stap 2) wordt bepaald door de
   handvathoogte bij het BEGIN van de beweging (net als de per-sample
   envelope in Stap 3), niet door de bewegingsrichting. Dit generaliseert het
   "up-beweging onder 135cm / down-beweging boven 135cm"-voorbeeld uit de
   spec naar alle bewegingen, en is de enige lezing die consistent is met de
   richting-onafhankelijke envelope-formule uit Stap 3.
"""

from __future__ import annotations

from typing import Optional

from .constants import Criteria
from .model import Analysis, Direction, Move, Recording, Sample, SampleResult

DEFAULT_VMAX_CM_S = 20.0
DEFAULT_AMAX_CM_S2 = 40.0


def segment_moves(samples: list[Sample], rev_hyst: float) -> list[tuple[Direction, int, int]]:
    """Splits de reeks in (richting, start_idx, eind_idx)-bewegingen.

    Richting wordt bepaald met hysterese: een omkering wordt pas bevestigd
    als arc_cm met minstens `rev_hyst` teruggeveerd is vanaf het lopende
    extremum, zodat ruis/kleine omkeringen niet als apart segment gelden.
    """
    n = len(samples)
    if n == 0:
        return []
    arc = [s.arc_cm for s in samples]

    pivots = [0]
    direction: Direction | None = None
    run_max_idx = 0
    run_min_idx = 0
    extreme_idx = 0

    for i in range(1, n):
        if direction is None:
            if arc[i] > arc[run_max_idx]:
                run_max_idx = i
            if arc[i] < arc[run_min_idx]:
                run_min_idx = i
            if arc[run_max_idx] - arc[pivots[-1]] >= rev_hyst:
                direction = "up"
                extreme_idx = run_max_idx
            elif arc[pivots[-1]] - arc[run_min_idx] >= rev_hyst:
                direction = "down"
                extreme_idx = run_min_idx
            continue

        if direction == "up":
            if arc[i] > arc[extreme_idx]:
                extreme_idx = i
            elif arc[extreme_idx] - arc[i] >= rev_hyst:
                pivots.append(extreme_idx)
                direction = "down"
                extreme_idx = i
        else:
            if arc[i] < arc[extreme_idx]:
                extreme_idx = i
            elif arc[i] - arc[extreme_idx] >= rev_hyst:
                pivots.append(extreme_idx)
                direction = "up"
                extreme_idx = i

    pivots.append(n - 1)

    moves: list[tuple[Direction, int, int]] = []
    for a, b in zip(pivots[:-1], pivots[1:]):
        if b <= a:
            continue
        d: Direction = "up" if arc[b] >= arc[a] else "down"
        moves.append((d, a, b))
    return moves


def _force_abs(sample: Sample, criteria: Criteria) -> float:
    return abs(sample.force_N) if criteria.USE_ABS_FORCE else sample.force_N


def find_anchor(
    samples: list[Sample], move: tuple[Direction, int, int], criteria: Criteria
) -> tuple[int, float, str]:
    """Bepaal het anker van een beweging (Stap 2).

    Basislimiet-voor-detectie = hoogte-gebaseerd op het bewegingsbegin (zie
    moduledocstring, punt 2). Binnen de eerste DETECT_ARC cm: als |F| de
    basislimiet overschrijdt, ligt het anker op het eerste punt waar de
    basislimiet bereikt wordt; anders ligt het anker op het bewegingsbegin.
    """
    _direction, start_idx, end_idx = move
    arc = [s.arc_cm for s in samples]
    arc_start = arc[start_idx]
    height_start = samples[start_idx].handle_height_cm
    base = criteria.F_LOW if height_start < criteria.H_THRESH else criteria.F_HIGH

    trigger_idx: Optional[int] = None
    for i in range(start_idx, end_idx + 1):
        if abs(arc[i] - arc_start) > criteria.DETECT_ARC:
            break
        if _force_abs(samples[i], criteria) > base:
            trigger_idx = i
            break

    if trigger_idx is None:
        return start_idx, arc_start, "start"

    for i in range(start_idx, trigger_idx + 1):
        if _force_abs(samples[i], criteria) >= base:
            return i, arc[i], "breakaway_detected"
    return trigger_idx, arc[trigger_idx], "breakaway_detected"  # onbereikbaar in de praktijk


def build_envelope(samples: list[Sample], moves: list[Move], criteria: Criteria) -> list[SampleResult]:
    """Bereken per sample de toegestane envelope en of de kracht daarbinnen valt (Stap 3)."""
    results: list[Optional[SampleResult]] = [None] * len(samples)
    for move_index, move in enumerate(moves):
        sign = 1.0 if move.direction == "up" else -1.0
        for i in range(move.start_idx, move.end_idx + 1):
            height = samples[i].handle_height_cm
            region = "laag" if height < criteria.H_THRESH else "hoog"
            base = criteria.F_LOW if region == "laag" else criteria.F_HIGH

            arc_i = samples[i].arc_cm
            progressed_past_anchor = (arc_i - move.anchor_arc) * sign >= 0
            dist = abs(arc_i - move.anchor_arc)
            in_grace = (
                progressed_past_anchor
                and dist <= criteria.GRACE_ARC
                and (criteria.GRACE_ABOVE or base == criteria.F_LOW)
            )
            envelope = criteria.GRACE_FACT * base if in_grace else base
            force_abs = _force_abs(samples[i], criteria)
            results[i] = SampleResult(
                region=region,
                base_N=base,
                envelope_N=envelope,
                in_grace=in_grace,
                force_abs_N=force_abs,
                force_ok=force_abs <= envelope,
                move_index=move_index,
            )
    return results  # type: ignore[return-value]


def analyze(recording: Recording, criteria: Criteria) -> Analysis:
    samples = recording.samples
    move_tuples = segment_moves(samples, criteria.REV_HYST)

    moves: list[Move] = []
    for d, start_idx, end_idx in move_tuples:
        anchor_idx, anchor_arc, reason = find_anchor(samples, (d, start_idx, end_idx), criteria)
        moves.append(
            Move(
                direction=d,
                start_idx=start_idx,
                end_idx=end_idx,
                arc_start=samples[start_idx].arc_cm,
                anchor_idx=anchor_idx,
                anchor_arc=anchor_arc,
                anchor_reason=reason,
            )
        )

    sample_results = build_envelope(samples, moves, criteria)
    violation_indices = [i for i, r in enumerate(sample_results) if not r.force_ok]

    low_forces = [r.force_abs_N for r in sample_results if r.region == "laag"]
    high_forces = [r.force_abs_N for r in sample_results if r.region == "hoog"]
    c1_ok = all(r.force_ok for r in sample_results if r.region == "laag")
    c2_ok = all(r.force_ok for r in sample_results if r.region == "hoog")

    max_height = max(s.handle_height_cm for s in samples) if samples else 0.0
    c3_ok = (not criteria.HEIGHT_MAX_IS_FAIL) or max_height <= criteria.H_MAX

    vmax = recording.meta.max_snelheid_cm_s if recording.meta.max_snelheid_cm_s else DEFAULT_VMAX_CM_S
    amax = (
        recording.meta.max_versnelling_cm_s2
        if recording.meta.max_versnelling_cm_s2
        else DEFAULT_AMAX_CM_S2
    )
    max_speed = max((abs(s.speed_cm_s) for s in samples), default=0.0)
    max_accel = max((abs(s.accel_cm_s2) for s in samples), default=0.0)
    c4_ok = max_speed <= vmax and max_accel <= amax

    return Analysis(
        recording=recording,
        criteria=criteria,
        moves=moves,
        sample_results=sample_results,
        c1_ok=c1_ok,
        c2_ok=c2_ok,
        c3_ok=c3_ok,
        c4_ok=c4_ok,
        peak_force_low_N=max(low_forces, default=0.0),
        peak_force_high_N=max(high_forces, default=0.0),
        max_height_cm=max_height,
        max_speed_cm_s=max_speed,
        max_accel_cm_s2=max_accel,
        violation_indices=violation_indices,
    )
