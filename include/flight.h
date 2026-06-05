#pragma once
#include <Arduino.h>

struct NearestAircraft {
    bool    valid;
    char    callsign[16];   // trimmed, e.g. "UAL26"
    char    registration[12];
    char    type[8];
    float   lat, lon;
    float   distance_km;
    float   bearing_deg;    // from home to aircraft
    float   track_deg;      // aircraft heading
    float   altitude_ft;
    float   speed_kts;
};

// Configure the home location and search radius before polling.
void flight_set_home(float lat, float lon, int radius_nm);

// Poll adsb.lol for the nearest aircraft.
// Returns true and populates `out` if a result was found.
// Returns false on network error or no aircraft in range.
bool flight_poll(NearestAircraft &out);

// Haversine distance in km between two lat/lon points.
float haversine_km(float lat1, float lon1, float lat2, float lon2);

// Bearing from point 1 to point 2, in degrees (0=N, 90=E, …).
float bearing_deg(float lat1, float lon1, float lat2, float lon2);
