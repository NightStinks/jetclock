#include "flight.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

static float s_home_lat   = 0.0f;
static float s_home_lon   = 0.0f;
static int   s_radius_nm  = 30;
static int   s_last_http_code = 0;

int flight_last_http_code() { return s_last_http_code; }

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

FlightResult flight_poll(NearestAircraft &out) {
    if (s_home_lat == 0.0f && s_home_lon == 0.0f) return FLIGHT_NO_COORDS;

    char url[128];
    snprintf(url, sizeof(url),
        "https://api.airplanes.live/v2/point/%.4f/%.4f/%d",
        s_home_lat, s_home_lon, s_radius_nm);
    Serial.printf("[flight] GET %s\n", url);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "JetClock/1.0");

    int code = http.GET();
    s_last_http_code = code;
    if (code != 200) {
        Serial.printf("[flight] HTTP %d from airplanes.live\n", code);
        http.end();
        return FLIGHT_NET_ERROR;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[flight] JSON error: %s\n", err.c_str());
        return FLIGHT_JSON_ERROR;
    }

    JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
    if (ac.isNull() || ac.size() == 0) {
        Serial.println("[flight] Empty aircraft array");
        return FLIGHT_NO_AC;
    }
    Serial.printf("[flight] %u aircraft in range\n", (unsigned)ac.size());

    float   best_dist = 1e9f;
    bool    found     = false;
    JsonObjectConst best_ac;

    for (JsonObjectConst aircraft : ac) {
        // Skip aircraft without position
        if (aircraft["lat"].isNull() || aircraft["lon"].isNull()) continue;

        // Skip private / GA — commercial callsigns are 2–3 letters + digits (e.g. EZY1234)
        const char *cs = aircraft["flight"] | "";
        int alpha = 0;
        while (cs[alpha] && isalpha((unsigned char)cs[alpha])) alpha++;
        if (alpha < 2 || alpha > 3 || !isdigit((unsigned char)cs[alpha])) continue;

        float a_lat = aircraft["lat"].as<float>();
        float a_lon = aircraft["lon"].as<float>();
        float dist  = haversine_km(s_home_lat, s_home_lon, a_lat, a_lon);

        if (dist < best_dist) {
            best_dist = dist;
            best_ac   = aircraft;
            found     = true;
        }
    }

    if (!found) {
        Serial.println("[flight] No commercial flights passed filter");
        return FLIGHT_NO_MATCH;
    }

    out.valid       = true;
    out.distance_km = best_dist;

    const char *raw_cs = best_ac["flight"] | "";
    strlcpy(out.callsign, raw_cs, sizeof(out.callsign));
    int len = strlen(out.callsign);
    while (len > 0 && out.callsign[len - 1] == ' ') out.callsign[--len] = '\0';
    if (len == 0) strlcpy(out.callsign, best_ac["r"] | "?", sizeof(out.callsign));

    strlcpy(out.registration, best_ac["r"] | "", sizeof(out.registration));
    strlcpy(out.type,         best_ac["t"] | "", sizeof(out.type));

    out.lat          = best_ac["lat"]      | 0.0f;
    out.lon          = best_ac["lon"]      | 0.0f;
    out.altitude_ft  = best_ac["alt_baro"] | 0.0f;
    out.speed_kts    = best_ac["gs"]       | 0.0f;
    out.track_deg    = best_ac["track"]    | 0.0f;
    out.bearing_deg  = bearing_deg(s_home_lat, s_home_lon, out.lat, out.lon);

    Serial.printf("[flight] Found %s at %.1f km\n", out.callsign, out.distance_km);
    return FLIGHT_OK;
}
