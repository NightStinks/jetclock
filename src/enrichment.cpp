#include "enrichment.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// The compiled LVGL Montserrat fonts only include ASCII (0x20-0x7E) plus a
// handful of symbol glyphs — no Latin-1 accented letters. City names like
// "Düsseldorf" render as boxes. Fold accented Latin-1 letters (UTF-8 2-byte
// sequences starting 0xC3) down to their closest ASCII letter in place.
static void strip_latin1_accents(char *s) {
    static const char map[64] = {
        'A','A','A','A','A','A','A','C','E','E','E','E','I','I','I','I',
        'D','N','O','O','O','O','O','x','O','U','U','U','U','Y','T','s',
        'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
        'd','n','o','o','o','o','o','/','o','u','u','u','u','y','t','y',
    };
    char *w = s;
    for (char *r = s; *r; ) {
        unsigned char c = (unsigned char)*r;
        if (c == 0xC3 && (unsigned char)r[1] >= 0x80 && (unsigned char)r[1] <= 0xBF) {
            *w++ = map[(unsigned char)r[1] - 0x80];
            r += 2;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

bool enrichment_lookup(const char *callsign, RouteInfo &out) {
    out = {};
    if (!callsign || callsign[0] == '\0') return false;

    char url[128];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
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

    const char *orig_iata = route["origin"]["iata_code"]        | "";
    const char *orig_city = route["origin"]["municipality"]     | "";
    const char *orig_name = route["origin"]["name"]             | "";
    const char *dest_iata = route["destination"]["iata_code"]   | "";
    const char *dest_city = route["destination"]["municipality"] | "";
    const char *dest_name = route["destination"]["name"]        | "";
    const char *airline   = route["airline"]["name"]            | "";

    // Prefer IATA + city (e.g. "LHR · London"), fall back to full name.
    if (orig_iata[0]) {
        if (orig_city[0]) snprintf(out.origin, sizeof(out.origin), "%s / %s", orig_iata, orig_city);
        else              strlcpy(out.origin, orig_iata, sizeof(out.origin));
    } else if (orig_name[0]) {
        strlcpy(out.origin, orig_name, sizeof(out.origin));
    }
    if (dest_iata[0]) {
        if (dest_city[0]) snprintf(out.destination, sizeof(out.destination), "%s / %s", dest_iata, dest_city);
        else              strlcpy(out.destination, dest_iata, sizeof(out.destination));
    } else if (dest_name[0]) {
        strlcpy(out.destination, dest_name, sizeof(out.destination));
    }
    strlcpy(out.airline, airline, sizeof(out.airline));

    strip_latin1_accents(out.origin);
    strip_latin1_accents(out.destination);
    strip_latin1_accents(out.airline);

    out.valid = (out.origin[0] != '\0' || out.airline[0] != '\0');
    return out.valid;
}
