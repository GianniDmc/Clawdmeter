#pragma once
#include <stdint.h>

// Optional accelerometer-driven orientation tracker. Returns 0..3 (quarter
// turns CW from default mounting). Boards without an IMU — or boards with
// rotation intentionally disabled, like AMOLED-1.8 fixed at 0° — return 0
// from imu_hal_rotation_quadrant() and no-op on init/tick.

void    imu_hal_init(void);
void    imu_hal_tick(void);
uint8_t imu_hal_rotation_quadrant(void);

// False until a stable reading has committed: the quadrant reads 0 (a side)
// before that, and anything drawn from it — the Pomodoro's edge marks — would
// point the wrong way for the first second. Boards without an IMU say true,
// since nothing orientation-driven runs on them.
bool    imu_hal_orientation_known(void);
