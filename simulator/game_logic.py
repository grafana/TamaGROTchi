"""Game logic — mirrors firmware game_engine.cpp / evolution.cpp / actions.cpp exactly.

All numeric constants are taken verbatim from src/config.h and the firmware source.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING, Callable

from pet_state import (
    EvoQuality,
    LifeStage,
    PetState,
    PetStatus,
    life_stage_name,
    quality_name,
)

if TYPE_CHECKING:
    from sim import TamagrotchiInstance

# ---------------------------------------------------------------------------
# Constants — mirrors config.h / game_engine.cpp
# ---------------------------------------------------------------------------

GAME_TICK_S = 1  # seconds per game tick

# Stat decay (per tick while awake and IDLE/ALERT)
HUNGER_DECAY    = 1
HAPPINESS_DECAY = 1

# Alert thresholds
ALERT_HUNGER_THRESH = 20
ALERT_HAPPY_THRESH  = 20
ALERT_TIMEOUT_S     = 120   # seconds before ignored alert → care mistake

# Sickness
SICK_HUNGER_THRESH = 15
SICK_DELAY_S       = 60     # sustained low hunger before sick onset
SICK_HEALTH_DECAY  = 2      # extra health drain per tick while sick

# Evolution age thresholds (seconds) — demo-speed
EVO_THRESHOLDS = [
    30,    # EGG → BABY
    120,   # BABY → CHILD
    600,   # CHILD → TEEN
    1800,  # TEEN → ADULT
    5400,  # ADULT → SENIOR
    10800, # SENIOR → DEAD (natural)
]

# Quality thresholds (care mistakes at time of evolution)
EVO_QUALITY_EXCELLENT = 1   # 0–1 mistakes
EVO_QUALITY_GOOD      = 3   # 2–3 mistakes

# Animation timeouts
DIZZY_DURATION_S         = 2.0
EATING_PLAYING_DURATION_S = 1.5

# Health recovery threshold
HEALTH_RECOVERY_HUNGER_MIN    = 50
HEALTH_RECOVERY_HAPPINESS_MIN = 50


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _clamp(v: int, lo: int = 0, hi: int = 100) -> int:
    return max(lo, min(hi, v))


def _compute_quality(care_mistakes: int) -> EvoQuality:
    if care_mistakes <= EVO_QUALITY_EXCELLENT:
        return EvoQuality.EXCELLENT
    if care_mistakes <= EVO_QUALITY_GOOD:
        return EvoQuality.GOOD
    return EvoQuality.TIRED


def _is_interactive(p: PetState) -> bool:
    return p.status not in (PetStatus.DEAD, PetStatus.EVOLVING)


# ---------------------------------------------------------------------------
# Game tick — called once per second
# ---------------------------------------------------------------------------

def tick(p: PetState, sim: "TamagrotchiInstance") -> None:
    """Advance the game state by one second. Mirrors game_engine_update()."""
    if p.status == PetStatus.DEAD:
        return

    now = time.monotonic()
    p.age_s += 1

    # --- Clear animation timeouts ---
    if p.status == PetStatus.DIZZY:
        if now - p.status_start >= DIZZY_DURATION_S:
            p.status = PetStatus.IDLE
    elif p.status in (PetStatus.EATING, PetStatus.PLAYING):
        if now - p.status_start >= EATING_PLAYING_DURATION_S:
            p.status = PetStatus.IDLE

    # --- Stat decay ---
    skip_decay = p.status in (
        PetStatus.EATING, PetStatus.PLAYING, PetStatus.SICK,
        PetStatus.EVOLVING, PetStatus.DIZZY,
    )
    if not skip_decay:
        if p.is_sleeping:
            # Halved decay while sleeping — apply every other tick
            if p.age_s % 2 == 0:
                p.hunger    = _clamp(p.hunger    - HUNGER_DECAY)
                p.happiness = _clamp(p.happiness - HAPPINESS_DECAY)
        else:
            p.hunger    = _clamp(p.hunger    - HUNGER_DECAY)
            p.happiness = _clamp(p.happiness - HAPPINESS_DECAY)

    # --- Health recovery ---
    if p.status != PetStatus.SICK:
        if p.is_sleeping:
            p.health = _clamp(p.health + 1)
        elif (p.hunger    >= HEALTH_RECOVERY_HUNGER_MIN and
              p.happiness >= HEALTH_RECOVERY_HAPPINESS_MIN and
              p.health    < 100):
            p.health = _clamp(p.health + 1)

    # --- Alert checks (hunger / happiness critically low) ---
    if not p.is_sleeping:
        _check_alert(p, "hunger",    p.hunger,    ALERT_HUNGER_THRESH, sim, now)
        _check_alert(p, "happiness", p.happiness, ALERT_HAPPY_THRESH,  sim, now)

    # --- Sickness onset ---
    if p.status not in (PetStatus.SICK, PetStatus.DEAD):
        if p.hunger < SICK_HUNGER_THRESH:
            if p.low_hunger_start == 0.0:
                p.low_hunger_start = now
            elif now - p.low_hunger_start >= SICK_DELAY_S:
                _go_sick(p, sim)
        else:
            p.low_hunger_start = 0.0

    # --- Sick health drain ---
    if p.status == PetStatus.SICK:
        p.health = _clamp(p.health - SICK_HEALTH_DECAY)

    # --- Death from health depletion ---
    if p.health <= 0 and p.status != PetStatus.DEAD:
        _die(p, sim, cause="health_depleted")
        return

    # --- Evolution threshold check ---
    evolution_check(p)

    # --- Auto-advance evolution (no button press needed in sim) ---
    if p.evolve_ready:
        evolution_advance(p, sim)


def _check_alert(
    p: PetState,
    vital: str,
    value: int,
    thresh: int,
    sim: "TamagrotchiInstance",
    now: float,
) -> None:
    sent_attr   = f"{vital}_alert_sent"
    already_sent = getattr(p, sent_attr)

    if value < thresh:
        if not already_sent:
            setattr(p, sent_attr, True)
            if not p.alert_active:
                p.alert_active = True
                p.alert_start  = now
            p.status = PetStatus.ALERT
            sim.log(13, f"{vital}_alert",
                    f"{vital}={value} | care_mistakes={p.care_mistakes}")
    else:
        if already_sent:
            setattr(p, sent_attr, False)

    # Timeout: ignored alert → care mistake
    if p.alert_active and (now - p.alert_start) >= ALERT_TIMEOUT_S:
        p.care_mistakes += 1
        p.alert_active = False
        p.hunger_alert_sent    = False
        p.happiness_alert_sent = False
        if p.status == PetStatus.ALERT:
            p.status = PetStatus.IDLE
        sim.log(13, "alert_missed",
                f"vital={vital} | care_mistakes={p.care_mistakes} | value={value}")


def _go_sick(p: PetState, sim: "TamagrotchiInstance") -> None:
    p.status         = PetStatus.SICK
    p.sick_since     = time.monotonic()
    p.low_hunger_start = 0.0
    sim.log(13, "sick",
            f"health={p.health} | hunger_was={p.hunger} | happiness_was={p.happiness}")
    sim.trace_standalone("pet.sick",
                         f"health={p.health} | hunger_was={p.hunger}", 500)


def _die(p: PetState, sim: "TamagrotchiInstance", cause: str) -> None:
    p.status = PetStatus.DEAD
    p.stage  = LifeStage.DEAD
    sim.log(17, "death",
            f"age_s={p.age_s} | care_mistakes={p.care_mistakes} | cause={cause}")
    sim.trace_standalone("pet.died",
                         f"age_s={p.age_s} | care_mistakes={p.care_mistakes} | cause={cause}",
                         1000)


# ---------------------------------------------------------------------------
# Evolution
# ---------------------------------------------------------------------------

def evolution_check(p: PetState) -> None:
    if p.evolve_ready:
        return
    if p.status in (PetStatus.DEAD, PetStatus.EVOLVING):
        return
    stage_idx = int(p.stage)
    if stage_idx >= 5:
        return
    if p.age_s >= EVO_THRESHOLDS[stage_idx]:
        p.evolve_ready = True


def evolution_advance(p: PetState, sim: "TamagrotchiInstance") -> None:
    if not p.evolve_ready:
        return
    p.evolve_ready = False

    prev      = p.stage
    next_idx  = int(p.stage) + 1

    if next_idx > int(LifeStage.SENIOR):
        # Natural death — old age
        p.status = PetStatus.DEAD
        p.stage  = LifeStage.DEAD
        msg = f"age_s={p.age_s} | care_mistakes={p.care_mistakes} | cause=old_age"
        sim.log(17, "death", msg)
        sim.active_trace_child("pet.died", msg, 1000)
        return

    p.stage   = LifeStage(next_idx)
    p.quality = _compute_quality(p.care_mistakes)
    p.status  = PetStatus.IDLE

    msg = (f"from={life_stage_name(prev)} | to={life_stage_name(p.stage)} | "
           f"age_s={p.age_s} | care_mistakes={p.care_mistakes} | "
           f"quality={quality_name(p.quality)}")
    sim.log(9, "evolved", msg)
    sim.active_trace_child("pet.evolved", msg, 2000)


# ---------------------------------------------------------------------------
# Actions — mirrors actions.cpp
# ---------------------------------------------------------------------------

def action_feed(p: PetState, food: str, sim: "TamagrotchiInstance") -> bool:
    """food: 'microchip' or 'sin_wave'"""
    if not _is_interactive(p):
        return False

    hun_before = p.hunger
    hap_before = p.happiness

    if food == "microchip":
        p.hunger    = _clamp(p.hunger    + 20)
        p.happiness = _clamp(p.happiness +  5)
    else:  # sin_wave
        p.hunger    = _clamp(p.hunger    + 10)
        p.happiness = _clamp(p.happiness + 15)

    p.status             = PetStatus.EATING
    p.status_start       = time.monotonic()
    p.hunger_alert_sent  = False

    msg = (f"food={food} | hunger={hun_before}->{p.hunger} | "
           f"happiness={hap_before}->{p.happiness}")
    sim.log(9, "fed", msg)
    sim.active_trace_child("pet.fed", msg, 500)
    return True


def action_play(p: PetState, sim: "TamagrotchiInstance") -> bool:
    if not _is_interactive(p):
        return False

    before      = p.happiness
    p.happiness = _clamp(p.happiness + 20)
    p.status    = PetStatus.PLAYING
    p.status_start = time.monotonic()
    p.happiness_alert_sent = False

    msg = f"happiness_before={before} | happiness_after={p.happiness}"
    sim.log(9, "played", msg)
    sim.active_trace_child("pet.played", msg, 500)
    return True


def action_medicine(p: PetState, sim: "TamagrotchiInstance") -> bool:
    if not _is_interactive(p):
        return False

    before    = p.health
    was_sick  = (p.status == PetStatus.SICK)
    p.health  = _clamp(p.health + 30)
    if was_sick:
        p.status = PetStatus.IDLE

    msg = (f"health_before={before} | health_after={p.health} | "
           f"sick_cleared={'true' if was_sick else 'false'}")
    sim.log(9, "medicine", msg)
    sim.active_trace_child("pet.medicine", msg, 500)

    if was_sick:
        rec_msg = f"health={p.health}"
        sim.log(9, "recovered", rec_msg)
        sim.active_trace_child("pet.recovered", rec_msg, 500)

    return True


def action_discipline(p: PetState, sim: "TamagrotchiInstance") -> bool:
    if not _is_interactive(p):
        return False

    before     = p.discipline
    p.discipline = _clamp(p.discipline + 5)

    msg = f"discipline_before={before} | discipline_after={p.discipline}"
    sim.log(9, "discipline", msg)
    sim.active_trace_child("pet.discipline", msg, 500)
    return True


def action_shake(p: PetState, mag: float, sim: "TamagrotchiInstance") -> None:
    """Simulate IMU shake. mag > 3.0 → dizzy (hard), else gentle → play."""
    p.last_accel_mag = mag
    if mag > 3.0:
        # Hard shake → dizzy
        body = f"level=hard | accel_mag={mag:.2f}"
        sim.begin_trace("sensor.shake", body, 2000)
        p.happiness  = _clamp(p.happiness - 5)
        p.status      = PetStatus.DIZZY
        p.status_start = time.monotonic()
        dizzy_msg = f"accel_mag={mag:.2f} | happiness_lost=5"
        sim.log(9, "shake_dizzy", dizzy_msg)
        sim.active_trace_child("pet.shake_dizzy", dizzy_msg, 2000)
        sim.end_trace()
    else:
        # Gentle shake → play
        body = f"level=gentle | accel_mag={mag:.2f}"
        sim.begin_trace("sensor.shake", body, 1000)
        play_msg = f"accel_mag={mag:.2f} | action=play"
        sim.log(9, "shake_play", play_msg)
        sim.active_trace_child("pet.shake_play", play_msg, 0)
        action_play(p, sim)
        sim.end_trace()
