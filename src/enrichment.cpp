#include "enrichment.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>

bool enrichment_lookup(const char *callsign, RouteInfo &out) {
    out = {};
    if (!callsign || callsign[0] == '\0') return false;

    char url[128];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    http.addHeader("User-Agent", "JetClock/1.0");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[enrich] HTTP %d for %s\n", code, callsign);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[enrich] JSON error: %s\n", err.c_str());
        return false;
    }

    // adsbdb returns "flightroute": false for VFR/private aircraft
    JsonVariantConst route = doc["response"]["flightroute"];
    if (route.isNull() || route.is<bool>()) return false;

    const char *orig_name    = route["origin"]["name"]            | "";
    const char *orig_country = route["origin"]["country_iso_name"]| "";
    const char *dest_name    = route["destination"]["name"]            | "";
    const char *dest_country = route["destination"]["country_iso_name"]| "";
    const char *airline      = route["airline"]["name"]            | "";

    if (orig_name[0] != '\0') {
        snprintf(out.origin, sizeof(out.origin), "%s, %s", orig_name, orig_country);
    }
    if (dest_name[0] != '\0') {
        snprintf(out.destination, sizeof(out.destination), "%s, %s", dest_name, dest_country);
    }
    strlcpy(out.airline, airline, sizeof(out.airline));

    out.valid = (out.origin[0] != '\0' || out.airline[0] != '\0');
    return out.valid;
}
