"""Synthetische testdata voor de engine-tests.

Bouwt Recording/Sample-objecten rechtstreeks (buiten de parser om), zodat elk
scenario zich puur op de segmentatie-/anker-/envelope-logica in engine.py
richt. angle_deg wordt niet door de engine gebruikt en staat op 0.0.
"""

from __future__ import annotations

from far500_force_check.model import Meta, Recording, Sample


def build_stroke(t0, dt, arc_start, arc_end, height_fn, force_fn, step=1.0):
    """Genereer samples voor één monotone beweging.

    force_fn(dist) krijgt de afgelegde afstand vanaf arc_start (altijd >= 0).
    height_fn(arc) geeft de handvathoogte bij een gegeven arc-positie.
    """
    n = max(2, round(abs(arc_end - arc_start) / step) + 1)
    samples = []
    for k in range(n):
        frac = k / (n - 1)
        arc = arc_start + (arc_end - arc_start) * frac
        dist = abs(arc - arc_start)
        samples.append(
            Sample(
                t_s=round(t0 + k * dt, 4),
                angle_deg=0.0,
                force_N=force_fn(dist),
                arc_cm=arc,
                speed_cm_s=0.0,
                accel_cm_s2=0.0,
                handle_height_cm=height_fn(arc),
            )
        )
    return samples


def chain(*stroke_lists):
    out: list[Sample] = []
    for lst in stroke_lists:
        out.extend(lst)
    return out


def make_recording(samples, **meta_kwargs) -> Recording:
    return Recording(meta=Meta(**meta_kwargs), samples=samples, source_format="csv")


def identity_height(arc: float) -> float:
    """Eenvoudigste height_fn: handvathoogte == arc-positie (cm)."""
    return arc
