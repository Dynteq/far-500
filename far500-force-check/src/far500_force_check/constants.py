"""Instelbare constanten voor de bedienkracht-toetsing (C1-C4).

Zie far500-force-check/README.md voor de normatieve tekst van elk criterium
en de achtergrond van de aannames A1-A5.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Criteria:
    H_THRESH: float = 135.0
    """cm — grens laag/hoog handvathoogte."""

    H_MAX: float = 170.0
    """cm — maximale bedienhoogte (C3)."""

    F_LOW: float = 140.0
    """N — basislimiet onder H_THRESH (C1)."""

    F_HIGH: float = 85.0
    """N — basislimiet boven H_THRESH (C2)."""

    GRACE_FACT: float = 1.50
    """Breakaway-factor: toegestane marge = GRACE_FACT * basislimiet."""

    GRACE_ARC: float = 20.0
    """cm — lengte van het breakaway-venster vanaf het anker."""

    DETECT_ARC: float = 10.0
    """cm — venster aan het begin van een beweging waarin een vroege
    overschrijding van de basislimiet het anker verlegt."""

    REV_HYST: float = 2.0
    """cm — hysterese voor richtingsdetectie op arc_cm (ruisonderdrukking)."""

    USE_ABS_FORCE: bool = True
    """A2 — toets |force_N| (druk én trek) i.p.v. het rauwe teken."""

    GRACE_ABOVE: bool = True
    """A1 — de breakaway-marge geldt ook boven H_THRESH (tot GRACE_FACT*F_HIGH)."""

    HEIGHT_MAX_IS_FAIL: bool = True
    """A3 — handvathoogte > H_MAX is een testfout (C3 FAIL)."""
