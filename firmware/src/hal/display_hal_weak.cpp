#include "display_hal.h"

// Boards that never rotate, or always do, keep this no-op.

__attribute__((weak)) void display_hal_follow_orientation(bool follow) {
    (void)follow;
}
