#pragma once
#include "config.h"
#include <functional>

// Start the captive portal AP + web server.
// tick_fn is called every ~10 ms while waiting — use it to drive LVGL.
// Does not return: reboots the device once config is saved.
void setup_portal_run(AppConfig &cfg, std::function<void()> tick_fn = nullptr);
