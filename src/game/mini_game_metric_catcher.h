#pragma once
#include "../game/pet_state.h"
#include "../input/buttons.h"

// =============================================================================
// Metric Catcher — falling-blob catch mini-game
//
// Green blobs (normal metrics) → catch for +10 score
// Red blobs   (anomalous)     → dodge; hit = lose a life
// 3 lives, 30-second timer.  A = left, C = right, B_LONG = quit early.
// =============================================================================

void metric_catcher_start(PetState* pet);
void metric_catcher_stop();
bool metric_catcher_is_running();
void metric_catcher_handle_input(ButtonEvent evt);
