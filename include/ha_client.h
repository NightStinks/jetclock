#pragma once
#include "config.h"
#include <functional>

// Callback types
using ButtonStateCallback = std::function<void(int button_idx, bool is_on)>;
using TempCallback        = std::function<void(float celsius)>;
using HumidityCallback    = std::function<void(float pct)>;

// Initialise the HA REST + WebSocket client.
// Connects to the HA WebSocket and subscribes to configured entity state changes.
void ha_client_init(const AppConfig &cfg,
                    ButtonStateCallback on_button_state,
                    TempCallback        on_temp,
                    HumidityCallback    on_humidity);

// Must be called every loop() — drives the WebSocket event loop.
void ha_client_tick();

// Toggle a configured button's entity (fire and forget via REST).
void ha_client_toggle(int button_idx);

// Fetch the current state of all button entities + sensors immediately (REST).
void ha_client_refresh_all();
