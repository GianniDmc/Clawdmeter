#include "../../hal/imu_hal.h"
#include "sim_platform.h"
#include <stdlib.h>

// Stand-in orientation: SIM_QUADRANT=<0..3> sets the start, the r key turns the
// "device" a quarter clockwise. The window itself never rotates — this only
// feeds the code that reacts to orientation (the Pomodoro).
static uint8_t quadrant = 0;

void imu_hal_init(void) {
    const char* v = getenv("SIM_QUADRANT");
    if (v) quadrant = (uint8_t)(atoi(v) & 3);
}
void    imu_hal_tick(void) {}
uint8_t imu_hal_rotation_quadrant(void) { return quadrant; }
void    sim_imu_rotate(void) { quadrant = (quadrant + 1) & 3; }
