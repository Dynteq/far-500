"""Datamodel voor een ingelezen meting en de kracht-normtoetsing erop."""

from dataclasses import dataclass, field
from typing import Literal, Optional

from .constants import Criteria


@dataclass
class Meta:
    """Spiegelt de velden uit metaRows() in FAR-500.html (regel 544-568)."""

    naam: str = ""
    notities: str = ""
    datum_tijd: str = ""
    kalibratie: str = ""
    handvat_hoog_cm: Optional[float] = None
    hoek_hoog_deg: Optional[float] = None
    handvat_laag_cm: Optional[float] = None
    hoek_laag_deg: Optional[float] = None
    l_ingevoerd_cm: Optional[float] = None
    l_berekend_cm: Optional[float] = None
    h_ingevoerd_cm: Optional[float] = None
    h_berekend_cm: Optional[float] = None
    limiet_controle: Optional[bool] = None
    max_snelheid_cm_s: Optional[float] = None
    max_versnelling_cm_s2: Optional[float] = None
    markering_20cm_omhoog_s: Optional[float] = None
    markering_20cm_omlaag_s: Optional[float] = None
    markering_135cm_omhoog_s: Optional[float] = None
    markering_135cm_omlaag_s: Optional[float] = None
    resultaat: str = ""
    raw_extra: dict = field(default_factory=dict)

    @property
    def l_used(self) -> Optional[float]:
        """Spiegelt Lused() (FAR-500.html regel 249): ingevoerd > 0 heeft voorrang."""
        if self.l_ingevoerd_cm is not None and self.l_ingevoerd_cm > 0:
            return self.l_ingevoerd_cm
        if self.l_berekend_cm is not None and self.l_berekend_cm > 0:
            return self.l_berekend_cm
        return None

    @property
    def h_used(self) -> Optional[float]:
        """Spiegelt Hused() (FAR-500.html regel 250)."""
        if self.h_ingevoerd_cm is not None:
            return self.h_ingevoerd_cm
        if self.h_berekend_cm is not None:
            return self.h_berekend_cm
        return None


@dataclass
class Sample:
    t_s: float
    angle_deg: float
    force_N: float
    arc_cm: Optional[float] = None
    speed_cm_s: Optional[float] = None
    accel_cm_s2: Optional[float] = None
    handle_height_cm: Optional[float] = None


@dataclass
class Recording:
    meta: Meta
    samples: list[Sample]
    source_format: Literal["csv", "xlsx"]
    recomputed_columns: list[str] = field(default_factory=list)


Direction = Literal["up", "down"]
AnchorReason = Literal["start", "breakaway_detected"]


@dataclass
class Move:
    direction: Direction
    start_idx: int
    end_idx: int  # inclusief
    arc_start: float
    anchor_idx: int
    anchor_arc: float
    anchor_reason: AnchorReason


@dataclass
class SampleResult:
    region: Literal["laag", "hoog"]
    base_N: float
    envelope_N: float
    in_grace: bool
    force_abs_N: float
    force_ok: bool
    move_index: Optional[int]


@dataclass
class Analysis:
    recording: Recording
    criteria: Criteria
    moves: list[Move] = field(default_factory=list)
    sample_results: list[SampleResult] = field(default_factory=list)

    c1_ok: bool = True
    c2_ok: bool = True
    c3_ok: bool = True
    c4_ok: bool = True

    peak_force_low_N: float = 0.0
    peak_force_high_N: float = 0.0
    max_height_cm: float = 0.0
    max_speed_cm_s: float = 0.0
    max_accel_cm_s2: float = 0.0

    violation_indices: list[int] = field(default_factory=list)

    @property
    def overall_pass(self) -> bool:
        return self.c1_ok and self.c2_ok and self.c3_ok and self.c4_ok
