#pragma once
#include <Arduino.h>

struct RouteInfo {
    bool  valid;
    char  origin[64];       // e.g. "Manchester, GB"
    char  destination[64];  // e.g. "Hong Kong, HK"
    char  airline[64];      // e.g. "Cathay Pacific"
};

// Look up route info for a callsign via adsbdb.com.
// Returns false on error or if no route data is available (VFR/private).
bool enrichment_lookup(const char *callsign, RouteInfo &out);
