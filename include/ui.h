#pragma once
#include <lvgl.h>
#include "config.h"

// Build the full LVGL widget tree (page_main + page_radar).
// Call once after display_init().
void ui_init(const AppConfig &cfg);

// ── Live data updates (called from flight/ha polling) ─────────────────────

void ui_set_time(const char *hhmm, const char *date);
void ui_set_temp(float celsius);
void ui_set_humidity(float pct);

// Flight data — all in one call to avoid partial updates
struct FlightData {
    const char *callsign;   // "UAL26" or "NO FLIGHT"
    const char *airline;    // "United Airlines" or ""
    const char *origin;     // "Manchester, GB" or "---"
    const char *dest;       // "Denver, US" or "---"
    float       distance_km;
    float       bearing_deg;
    float       track_deg;
};
void ui_set_flight(const FlightData &f, int screen_bearing_deg);

// Button state — index 0..4
void ui_set_button_state(int idx, bool on);

// Apply a theme by name ("Warm Brown", "Midnight", "Forest", "iOS")
void ui_apply_theme(const char *theme_name);

// Register the function to call when a button is tapped.
// Must be set before ui_init() creates buttons, or buttons won't be clickable.
void ui_set_toggle_callback(void (*cb)(int button_idx));
