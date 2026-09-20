#include "sim_platform.h"
#include <string.h>
#include <SDL.h>
#include <Arduino.h>
#include <stdlib.h>
#include "../../settings.h"
#include "../../ui.h"
#include <lvgl.h>

static bool quit = false;

static bool     pwr_down = false;
static uint32_t pwr_down_ms = 0;
static bool     pwr_long_fired = false;
static bool     edge_pressed = false, edge_long = false, edge_released = false;

static int  battery = 87;
static bool charging = false;

static bool take(bool* f) { bool v = *f; *f = false; return v; }
bool sim_take_pwr_pressed(void)  { return take(&edge_pressed); }
bool sim_take_pwr_long(void)     { return take(&edge_long); }
bool sim_take_pwr_released(void) { return take(&edge_released); }
int  sim_battery_pct(void) { return battery; }
bool sim_charging(void)    { return charging; }
bool sim_should_quit(void) { return quit; }

// Matches the AXP2101 long-press threshold main.cpp's pair gesture expects.
#define PWR_LONG_MS 1500

void sim_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;
            switch (k) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_SPACE:  sim_playback_toggle(); break;
            case SDLK_LEFT:   sim_playback_step(-1); break;
            case SDLK_RIGHT:  sim_playback_step(+1); break;
            case SDLK_d:      sim_playback_toggle_link(); break;
            case SDLK_s:      sim_display_screenshot(NULL); break;
            case SDLK_c:      charging = !charging; break;
            case SDLK_r:      sim_imu_rotate(); break;
            case SDLK_o:      settings_open(); break;
            case SDLK_MINUS:  battery = battery < 5 ? 0 : battery - 5; break;
            case SDLK_EQUALS: battery = battery > 95 ? 100 : battery + 5; break;
            case SDLK_p:
                pwr_down = true;
                pwr_down_ms = millis();
                pwr_long_fired = false;
                break;
            default:
                if (k >= SDLK_1 && k <= SDLK_9) sim_playback_jump(k - SDLK_1);
                break;
            }
        }
        if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_p && pwr_down) {
            pwr_down = false;
            if (!pwr_long_fired) edge_pressed = true;
            edge_released = true;
        }
    }
    if (pwr_down && !pwr_long_fired && millis() - pwr_down_ms >= PWR_LONG_MS) {
        pwr_long_fired = true;
        edge_long = true;
    }

    // Headless: SIM_SETTINGS=1 opens the settings page once the UI is up, so
    // an autoshot can capture it.
    // SIM_SETTINGS=<tab> also picks the tab (0 Pomodoro, 1 Sound, 2 Buttons).
    static const char* settings_env = getenv("SIM_SETTINGS");
    static bool settings_armed = settings_env != nullptr;
    if (settings_armed && millis() >= 300) {
        settings_armed = false;
        settings_open();
        settings_show_tab(atoi(settings_env));
    }

    // Report LVGL's pool once the UI is built: running out of it crashes
    // inside the renderer, far from the widget that tipped it over.
    static bool mem_reported = false;
    if (!mem_reported && millis() >= 1000) {
        mem_reported = true;
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        printf("[sim] LVGL pool: %u%% used, %u bytes free, biggest free block %u\n",
               (unsigned)mon.used_pct, (unsigned)mon.free_size, (unsigned)mon.free_biggest_size);
    }

    // SIM_ROTATE_MS=<ms>[,<ms>...] turns the fake IMU a quarter clockwise at
    // each of those times, so an autoshot can reach the Pomodoro headlessly.
    static const char* rotate_env = getenv("SIM_ROTATE_MS");
    static const char* rotate_at = rotate_env;
    if (rotate_at && *rotate_at) {
        if (millis() >= (uint32_t)atol(rotate_at)) {
            sim_imu_rotate();
            const char* comma = strchr(rotate_at, ',');
            rotate_at = comma ? comma + 1 : nullptr;
        }
    }

    // SIM_NEXT_MS=<ms>[,<ms>...] taps "next screen" at each of those times, to
    // check headlessly which pages the rotation actually stops on.
    static const char* next_env = getenv("SIM_NEXT_MS");
    static const char* next_at = next_env;
    if (next_at && *next_at) {
        if (millis() >= (uint32_t)atol(next_at)) {
            ui_next_screen();
            printf("[sim] next screen -> %d\n", (int)ui_get_current_screen());
            const char* comma = strchr(next_at, ',');
            next_at = comma ? comma + 1 : nullptr;
        }
    }

    // SIM_SCREEN=<n> shows screen n (see screen_t) once the UI is up.
    static const char* screen_env = getenv("SIM_SCREEN");
    static bool screen_armed = screen_env != nullptr;
    if (screen_armed && millis() >= 1200) {
        screen_armed = false;
        ui_show_screen((screen_t)atoi(screen_env));
    }

    // Headless CI hook: SIM_AUTOSHOT_MS=<ms> → screenshot + exit.
    static long autoshot_ms = -2;
    if (autoshot_ms == -2) {
        const char* v = getenv("SIM_AUTOSHOT_MS");
        autoshot_ms = v ? atol(v) : -1;
    }
    if (autoshot_ms >= 0 && millis() >= (uint32_t)autoshot_ms) {
        const char* p = getenv("SIM_AUTOSHOT_PATH");
        sim_display_screenshot(p ? p : "sim-autoshot.bmp");
        quit = true;
        autoshot_ms = -1;
    }
}
