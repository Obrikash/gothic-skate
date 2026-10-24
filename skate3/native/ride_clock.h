/* Fixed-rate input clock, independent of Windows/Gothic. Owned maintenance
 * pauses simulation time; unrelated host stalls still trigger the guard. */
#pragma once
#include <stdint.h>
typedef struct RideClock { uint32_t last, remainder; } RideClock;
static inline void ride_clock_reset(RideClock* clock, uint32_t now) {
    clock->last = now; clock->remainder = 0;
}
/* Returns 0..8 steps, -1 for an unrelated >200 ms stall, -2 for queue overflow. */
static inline int ride_clock_tick(RideClock* clock, uint32_t now, uint32_t maintenance_ms) {
    uint32_t elapsed = now - clock->last;
    clock->last = now;
    if (maintenance_ms > elapsed) return -1;
    elapsed -= maintenance_ms;
    if (elapsed > 200) return -1;
    clock->remainder += elapsed * 60;
    unsigned count = clock->remainder / 1000;
    clock->remainder %= 1000;
    return count > 8 ? -2 : (int)count;
}
