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

enum FlightResult : uint8_t {
    FLIGHT_OK,
    FLIGHT_NO_COORDS,   // lat/lon both zero — not configured
    FLIGHT_NET_ERROR,   // HTTP != 200 or connection failed
    FLIGHT_JSON_ERROR,  // JSON parse error
    FLIGHT_NO_AC,       // API returned empty aircraft array
    FLIGHT_NO_MATCH,    // No commercial flights passed filter
};

// Poll airplanes.live for the nearest aircraft.
// Returns FLIGHT_OK and populates `out` on success, otherwise an error code.
FlightResult flight_poll(NearestAircraft &out);

// HTTP status code (or negative HTTPClient error) from the most recent poll.
// Only meaningful after a FLIGHT_NET_ERROR result.
int flight_last_http_code();

// Haversine distance in km between two lat/lon points.
float haversine_km(float lat1, float lon1, float lat2, float lon2);

// Bearing from point 1 to point 2, in degrees (0=N, 90=E, …).
float bearing_deg(float lat1, float lon1, float lat2, float lon2);
