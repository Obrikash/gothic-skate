#include "ride_clock.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "clock check failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    RideClock clock;
    ride_clock_reset(&clock, 1000);
    CHECK(ride_clock_tick(&clock, 1281, 265) == 0); /* exact reported capture */
    CHECK(ride_clock_tick(&clock, 1298, 0) == 1);
    CHECK(ride_clock_tick(&clock, 1600, 0) == -1); /* real stall still guarded */
    ride_clock_reset(&clock, 0);
    CHECK(ride_clock_tick(&clock, 150, 0) == -2); /* protocol has only 8 slots */
    unsigned steps = 0, now = 0;
    ride_clock_reset(&clock, now);
    for (unsigned frame = 0; frame < 1000; ++frame) {
        unsigned maintenance = frame % 7 == 0 ? 266 : 0;
        now += 1 + maintenance;
        int count = ride_clock_tick(&clock, now, maintenance);
        CHECK(count >= 0 && count <= 1); steps += (unsigned)count;
    }
    CHECK(steps == 60 && clock.remainder == 0);
    ride_clock_reset(&clock, UINT32_MAX - 10);
    CHECK(ride_clock_tick(&clock, 6, 0) == 1); /* GetTickCount wrap */
    ride_clock_reset(&clock, 10);
    CHECK(ride_clock_tick(&clock, 20, 11) == -1);
    puts("PASS ride clock: reported 265 ms recapture, repeated recaptures, 60 Hz, real stalls, queue bound, timer wrap");
    return 0;
}
