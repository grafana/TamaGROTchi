#pragma once
#include "pet_state.h"

// =============================================================================
// Evolution — checks age thresholds and triggers stage transitions
// =============================================================================

// Called each game tick. Sets p->evolveReady = true when threshold crossed.
// Also updates p->quality at each transition based on care mistakes.
void evolution_check(PetState* p);

// Execute the actual stage transition (called after evolution animation completes)
void evolution_advance(PetState* p);
