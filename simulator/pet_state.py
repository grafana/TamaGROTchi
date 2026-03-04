"""PetState dataclass and enums — mirrors src/game/pet_state.h exactly."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import IntEnum


class LifeStage(IntEnum):
    EGG    = 0
    BABY   = 1
    CHILD  = 2
    TEEN   = 3
    ADULT  = 4
    SENIOR = 5
    DEAD   = 6


class PetStatus(IntEnum):
    IDLE     = 0
    EATING   = 1
    PLAYING  = 2
    SLEEPING = 3
    SICK     = 4
    EVOLVING = 5
    DEAD     = 6
    ALERT    = 7
    DIZZY    = 8


class EvoQuality(IntEnum):
    EXCELLENT = 0
    GOOD      = 1
    TIRED     = 2


STAGE_NAMES   = ["egg", "baby", "child", "teen", "adult", "senior", "dead"]
QUALITY_NAMES = ["excellent", "good", "tired"]


def life_stage_name(stage: LifeStage) -> str:
    return STAGE_NAMES[int(stage)]


def quality_name(q: EvoQuality) -> str:
    return QUALITY_NAMES[int(q)]


@dataclass
class PetState:
    hunger:    int = 60
    happiness: int = 60
    health:    int = 80

    stage:   LifeStage  = LifeStage.EGG
    status:  PetStatus  = PetStatus.IDLE
    quality: EvoQuality = EvoQuality.EXCELLENT

    age_s:          int = 0
    care_mistakes:  int = 0
    discipline:     int = 50
    evolve_ready:   bool = False

    is_sleeping: bool = False
    sleep_hour:  int  = 22
    wake_hour:   int  = 8

    # monotonic timestamps (seconds)
    status_start: float = field(default_factory=time.monotonic)
    last_tick:    float = field(default_factory=time.monotonic)

    alert_active:         bool  = False
    alert_start:          float = 0.0
    hunger_alert_sent:    bool  = False
    happiness_alert_sent: bool  = False

    sick_since:        float = 0.0
    low_hunger_start:  float = 0.0

    # last accel magnitude (for telemetry)
    last_accel_mag: float = 0.0
