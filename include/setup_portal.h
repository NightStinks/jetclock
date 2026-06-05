#pragma once
#include "config.h"
#include <functional>

// Start the WiFi-only captive portal AP.
// Serves wifi.html — asks only for SSID + password.
// tick_fn is called every ~10 ms (use it to drive LVGL).
// Does not return: reboots once credentials are saved.
void setup_portal_run(AppConfig &cfg, std::function<void()> tick_fn = nullptr);
