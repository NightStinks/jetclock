#pragma once
#include "config.h"

// Start the captive portal AP + web server.
// Blocks until the user saves a config, then returns the new config.
// Call ESP.restart() after this returns.
void setup_portal_run(AppConfig &cfg);
