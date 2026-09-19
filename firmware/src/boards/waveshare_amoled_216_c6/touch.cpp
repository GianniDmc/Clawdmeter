#include "../../hal/touch_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>

static TouchDrvCST92xx touch;

static volatile bool     touch_data_ready = false;
static volatile bool     touch_pressed = false;
static volatile uint16_t touch_x = 0;
static volatile uint16_t touch_y = 0;

// display.cpp: the quadrant whose MADCTL is on the panel right now.
uint8_t display_applied_quadrant(void);

// The swap/mirror set in touch_hal_init() calibrates the controller for the
// base MADCTL (q3, upright). The other three quadrants re-address the panel,
// so the same finger lands on a different LVGL pixel: undo that here. Derived
// from the MADCTL bits (q2 = MY, q0 = MX, q1 = MX|MY|MV), each a proper
// rotation of the base frame.
static void map_to_rotation(int16_t bx, int16_t by, uint16_t* x, uint16_t* y) {
    const int16_t W = LCD_WIDTH - 1, H = LCD_HEIGHT - 1;
    switch (display_applied_quadrant()) {
    case 0:  *x = W - by; *y = bx;     break;
    case 1:  *x = W - bx; *y = H - by; break;
    case 2:  *x = by;     *y = H - bx; break;
    default: *x = bx;     *y = by;     break;
    }
}

static void IRAM_ATTR touch_isr(void) {
    touch_data_ready = true;
}

void touch_hal_init(void) {
    touch.setPins(TP_RST, TP_INT);
    if (!touch.begin(Wire, CST9220_ADDR, IIC_SDA, IIC_SCL)) {
        Serial.println("Touch init failed");
        return;
    }
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    // C6 2.16 panel mapping (verified empirically): the CST9217's raw
    // axes are swapped relative to the raster AND X is mirrored, to match
    // the MADCTL 0x30 (MV+ML) transpose written in display.cpp.
    touch.setSwapXY(true);
    touch.setMirrorXY(true, false);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(TP_INT, touch_isr, FALLING);
    Serial.println("Touch init OK");
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (touch_data_ready) {
        touch_data_ready = false;
        int16_t tx[5], ty[5];
        uint8_t n = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
        if (n > 0) {
            uint16_t mx, my;
            map_to_rotation(tx[0], ty[0], &mx, &my);
            touch_pressed = true;
            touch_x = mx;
            touch_y = my;
        } else {
            touch_pressed = false;
        }
    }
    *x = touch_x;
    *y = touch_y;
    *pressed = touch_pressed;
}
