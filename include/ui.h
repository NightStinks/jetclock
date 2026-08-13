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
void ui_set_wifi(int rssi_dbm);  // -30 (great) … -90 (terrible); 0 = unknown

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

// Update a slot card from HA state. `state` is the raw HA state string;
// `value`/`has_value` carry the numeric reading (brightness %, position, sensor
// value). Each card type uses what it needs.
void ui_set_slot_state(int idx, const char *state, float value, bool has_value);

// Apply a theme by name ("Warm Brown", "Midnight", "Forest", "iOS")
void ui_apply_theme(const char *theme_name);

// Register callbacks for slot interactions. Set before ui_init().
//  - activate: a toggle/action/cover/lock card was tapped.
//  - set_value: a slider card was dragged to `value` (0–100).
void ui_set_slot_callbacks(void (*activate)(int idx), void (*set_value)(int idx, int value));
