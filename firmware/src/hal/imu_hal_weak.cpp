#include "imu_hal.h"

// Default for the optional part of the IMU HAL: a board that tracks
// orientation overrides it (see boards/*/imu.cpp).

__attribute__((weak)) bool imu_hal_orientation_known(void) {
    return true;
}
