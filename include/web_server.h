#pragma once
#include "config.h"

// Start the persistent config web server + mDNS.
// Call once after WiFi is connected. Accessible at http://jetclock.local
// ESPAsyncWebServer is interrupt-driven — no tick function needed.
void web_server_init(AppConfig &cfg);
