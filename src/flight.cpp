#include "flight.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

static float s_home_lat   = 0.0f;
static float s_home_lon   = 0.0f;
static int   s_radius_nm  = 30;

void flight_set_home(float lat, float lon, int radius_nm) {
    s_home_lat  = lat;
    s_home_lon  = lon;
    s_radius_nm = radius_nm;
}

float haversine_km(float lat1, float lon1, float lat2, float lon2) {
    float dlat = (lat2 - lat1) * DEG_TO_RAD;
    float dlon = (lon2 - lon1) * DEG_TO_RAD;
    float a = sinf(dlat / 2) * sinf(dlat / 2)
            + cosf(lat1 * DEG_TO_RAD) * cosf(lat2 * DEG_TO_RAD)
            * sinf(dlon / 2) * sinf(dlon / 2);
    return 6371.0f * 2.0f * asinf(sqrtf(a));
}

float bearing_deg(float lat1, float lon1, float lat2, float lon2) {
    float dlon  = (lon2 - lon1) * DEG_TO_RAD;
    float lat1r = lat1 * DEG_TO_RAD;
    float lat2r = lat2 * DEG_TO_RAD;
    float y = sinf(dlon) * cosf(lat2r);
    float x = cosf(lat1r) * sinf(lat2r) - sinf(lat1r) * cosf(lat2r) * cosf(dlon);
    return fmodf((atan2f(y, x) / DEG_TO_RAD) + 360.0f, 360.0f);
}

bool flight_poll(NearestAircraft &out) {
    if (s_home_lat == 0.0f && s_home_lon == 0.0f) return false;

    char url[128];
    snprintf(url, sizeof(url),
        "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d",
        s_home_lat, s_home_lon, s_radius_nm);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "JetClock/1.0");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[flight] HTTP %d from adsb.lol\n", code);
        http.end();
        return false;
    }

    // Stream into JsonDocument — aircraft array can be large
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[flight] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
    if (ac.isNull() || ac.size() == 0) {
        return false;
    }

    float   best_dist = 1e9f;
    bool    found     = false;
    JsonObjectConst best_ac;

    for (JsonObjectConst aircraft : ac) {
        // Skip aircraft without position
        if (!aircraft["lat"].is<float>() || !aircraft["lon"].is<float>()) continue;

        float a_lat = aircraft["lat"].as<float>();
        float a_lon = aircraft["lon"].as<float>();
        float dist  = haversine_km(s_home_lat, s_home_lon, a_lat, a_lon);

        if (dist < best_dist) {
            best_dist = dist;
            best_ac   = aircraft;
            found     = true;
        }
    }

    if (!found) return false;

    out.valid       = true;
    out.distance_km = best_dist;

    // Callsign — trim trailing spaces
    const char *raw_cs = best_ac["flight"] | "";
    strlcpy(out.callsign, raw_cs, sizeof(out.callsign));
    int len = strlen(out.callsign);
    while (len > 0 && out.callsign[len - 1] == ' ') out.callsign[--len] = '\0';
    if (len == 0) strlcpy(out.callsign, best_ac["r"] | "?", sizeof(out.callsign));

    strlcpy(out.registration, best_ac["r"] | "", sizeof(out.registration));
    strlcpy(out.type,         best_ac["t"] | "", sizeof(out.type));

    out.lat          = best_ac["lat"]     | 0.0f;
    out.lon          = best_ac["lon"]     | 0.0f;
    out.altitude_ft  = best_ac["alt_baro"]| 0.0f;
    out.speed_kts    = best_ac["gs"]      | 0.0f;
    out.track_deg    = best_ac["track"]   | 0.0f;
    out.bearing_deg  = bearing_deg(s_home_lat, s_home_lon, out.lat, out.lon);

    return true;
}
