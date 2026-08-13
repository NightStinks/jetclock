#pragma once
#include "config.h"
#include <functional>

// Per-slot state update. `state` is the raw HA state string; `value` is the
// parsed numeric reading (from the slot's attribute, or the state itself) and
// `has_value` says whether it's valid. The UI interprets these per card type.
using SlotStateCallback  = std::function<void(int slot_idx, const char *state,
                                              float value, bool has_value)>;
using TempCallback       = std::function<void(float celsius)>;
using HumidityCallback   = std::function<void(float pct)>;
// Called with effective brightness 0–100 (0 = screen off) whenever the
// display_power or display_brightness HA entity changes.
using DisplayCallback    = std::function<void(int brightness_pct)>;

// Initialise the HA REST + WebSocket client.
void ha_client_init(const AppConfig &cfg,
                    SlotStateCallback on_slot_state,
                    TempCallback      on_temp,
                    HumidityCallback  on_humidity,
                    DisplayCallback   on_display = nullptr);

// Must be called every loop() — drives the WebSocket event loop.
void ha_client_tick();

// User activated a slot (toggle / action / lock / cover open-close). Picks the
// right service from the slot's card type + domain. Fire-and-forget via REST.
void ha_client_activate(int slot_idx);

// User set a slot's slider value (light brightness %, cover position, fan %…).
void ha_client_set_value(int slot_idx, int value);

// Fetch the current state of all configured slot entities + sensors (REST).
void ha_client_refresh_all();
