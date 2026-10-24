#ifndef SIM_MISSION_H
#define SIM_MISSION_H

// The mission loop:
//
//   DROP       players ride pods down onto the drop zone (crushing whatever
//              is there); waves start once they have landed
//   OBJECTIVE  hold both zones: a zone fills while a living player stands
//              in it with no bugs inside, and never empties again
//   EXTRACT    call the Extraction calldown from inside the LZ
//   HOLD       survive while the waves come faster, until the dropship lands
//   BOARDING   living players who reach the LZ board; once nobody is left
//              alive on the ground, the mission is a success
//   SUCCESS / FAILED   the world stops; the N64 layer shows the results.
//              FAILED is the squad lost (no one alive, no reinforcements).

#include "sim.h"

void mission_init(sim_t *s);
void mission_update(sim_t *s);
bool mission_over(const sim_t *s);
// Ticks a zone takes to capture / the hold lasts (shorter in quick mode).
int  mission_capture_ticks(const sim_t *s);
int  mission_hold_ticks(const sim_t *s);
// Player `idx` stands in the LZ while extraction can be called.
bool mission_can_call_extraction(const sim_t *s, int idx);
// The Extraction calldown arrived: start the hold.
void mission_start_extraction(sim_t *s);
// Debug: capture every zone now.
void mission_complete_objective(sim_t *s);

#endif
