#include "config.h"
#include <Preferences.h>

static const char *NVS_NS = "jetclock";

void config_reset(AppConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.version    = CONFIG_VERSION;
    cfg.radius_nm  = 30;
}

bool config_load(AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);  // read-only

    if (!prefs.isKey("version")) {
        prefs.end();
        config_reset(cfg);
        return false;
    }

    cfg.version = prefs.getInt("version", CONFIG_VERSION);

    prefs.getString("wifi_ssid",     cfg.wifi_ssid,     sizeof(cfg.wifi_ssid));
    prefs.getString("wifi_pass",     cfg.wifi_password, sizeof(cfg.wifi_password));

    cfg.home_lat       = prefs.getFloat("home_lat",   0.0f);
    cfg.home_lon       = prefs.getFloat("home_lon",   0.0f);
    cfg.radius_nm      = prefs.getInt("radius_nm",    30);
    cfg.screen_bearing = prefs.getInt("screen_bear",  0);

    prefs.getString("ha_url",        cfg.ha_url,        sizeof(cfg.ha_url));
    prefs.getString("ha_token",      cfg.ha_token,      sizeof(cfg.ha_token));
    prefs.getString("temp_entity",   cfg.temp_entity,   sizeof(cfg.temp_entity));
    prefs.getString("hum_entity",    cfg.humidity_entity, sizeof(cfg.humidity_entity));

    cfg.num_slots = prefs.getInt("num_slots", 0);
    if (cfg.num_slots > MAX_SLOTS) cfg.num_slots = MAX_SLOTS;

    for (int i = 0; i < cfg.num_slots; i++) {
        char key[16];
        SlotConfig &s = cfg.slots[i];
        snprintf(key, sizeof(key), "s%d_type", i); s.type = (uint8_t)prefs.getInt(key, CARD_EMPTY);
        snprintf(key, sizeof(key), "s%d_eid",  i); prefs.getString(key, s.entity_id, sizeof(s.entity_id));
        snprintf(key, sizeof(key), "s%d_lbl",  i); prefs.getString(key, s.label,     sizeof(s.label));
        snprintf(key, sizeof(key), "s%d_dom",  i); prefs.getString(key, s.domain,    sizeof(s.domain));
        snprintf(key, sizeof(key), "s%d_svc",  i); prefs.getString(key, s.service,   sizeof(s.service));
        snprintf(key, sizeof(key), "s%d_attr", i); prefs.getString(key, s.attribute, sizeof(s.attribute));
        snprintf(key, sizeof(key), "s%d_unit", i); prefs.getString(key, s.unit,      sizeof(s.unit));
        snprintf(key, sizeof(key), "s%d_min",  i); s.min_val = (int16_t)prefs.getInt(key, 0);
        snprintf(key, sizeof(key), "s%d_max",  i); s.max_val = (int16_t)prefs.getInt(key, 100);
    }

    prefs.end();
    return true;
}

void config_save(const AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);  // read-write

    prefs.putInt("version",      cfg.version);
    prefs.putString("wifi_ssid", cfg.wifi_ssid);
    prefs.putString("wifi_pass", cfg.wifi_password);
    prefs.putFloat("home_lat",   cfg.home_lat);
    prefs.putFloat("home_lon",   cfg.home_lon);
    prefs.putInt("radius_nm",    cfg.radius_nm);
    prefs.putInt("screen_bear",  cfg.screen_bearing);
    prefs.putString("ha_url",    cfg.ha_url);
    prefs.putString("ha_token",  cfg.ha_token);
    prefs.putString("temp_entity",  cfg.temp_entity);
    prefs.putString("hum_entity",   cfg.humidity_entity);
    prefs.putInt("num_slots",  cfg.num_slots);

    for (int i = 0; i < cfg.num_slots; i++) {
        char key[16];
        const SlotConfig &s = cfg.slots[i];
        snprintf(key, sizeof(key), "s%d_type", i); prefs.putInt(key, s.type);
        snprintf(key, sizeof(key), "s%d_eid",  i); prefs.putString(key, s.entity_id);
        snprintf(key, sizeof(key), "s%d_lbl",  i); prefs.putString(key, s.label);
        snprintf(key, sizeof(key), "s%d_dom",  i); prefs.putString(key, s.domain);
        snprintf(key, sizeof(key), "s%d_svc",  i); prefs.putString(key, s.service);
        snprintf(key, sizeof(key), "s%d_attr", i); prefs.putString(key, s.attribute);
        snprintf(key, sizeof(key), "s%d_unit", i); prefs.putString(key, s.unit);
        snprintf(key, sizeof(key), "s%d_min",  i); prefs.putInt(key, s.min_val);
        snprintf(key, sizeof(key), "s%d_max",  i); prefs.putInt(key, s.max_val);
    }

    prefs.end();
}

void config_erase() {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.clear();
    prefs.end();
}

bool config_is_configured(const AppConfig &cfg) {
    return cfg.wifi_ssid[0] != '\0';
}

void config_to_json(const AppConfig &cfg, JsonDocument &doc, bool mask_secrets) {
    doc["version"]        = cfg.version;
    doc["wifi_ssid"]      = cfg.wifi_ssid;
    doc["wifi_password"]  = mask_secrets ? "********" : cfg.wifi_password;
    doc["home_lat"]       = cfg.home_lat;
    doc["home_lon"]       = cfg.home_lon;
    doc["radius_nm"]      = cfg.radius_nm;
    doc["screen_bearing"] = cfg.screen_bearing;
    doc["ha_url"]         = cfg.ha_url;
    doc["ha_token"]       = mask_secrets ? "********" : cfg.ha_token;
    doc["temp_entity"]    = cfg.temp_entity;
    doc["humidity_entity"] = cfg.humidity_entity;

    JsonArray slots = doc["slots"].to<JsonArray>();
    for (int i = 0; i < cfg.num_slots; i++) {
        const SlotConfig &s = cfg.slots[i];
        JsonObject o = slots.add<JsonObject>();
        o["type"]      = s.type;
        o["entity_id"] = s.entity_id;
        o["label"]     = s.label;
        o["domain"]    = s.domain;
        o["service"]   = s.service;
        o["attribute"] = s.attribute;
        o["unit"]      = s.unit;
        o["min"]       = s.min_val;
        o["max"]       = s.max_val;
    }
}

bool config_from_json(const JsonDocument &doc, AppConfig &cfg) {
    cfg.version = doc["version"] | CONFIG_VERSION;

    strlcpy(cfg.wifi_ssid,     doc["wifi_ssid"]     | "", sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_password, doc["wifi_password"] | "", sizeof(cfg.wifi_password));

    cfg.home_lat       = doc["home_lat"]       | 0.0f;
    cfg.home_lon       = doc["home_lon"]       | 0.0f;
    cfg.radius_nm      = doc["radius_nm"]      | 30;
    cfg.screen_bearing = doc["screen_bearing"] | 0;

    strlcpy(cfg.ha_url,          doc["ha_url"]          | "", sizeof(cfg.ha_url));
    strlcpy(cfg.ha_token,        doc["ha_token"]        | "", sizeof(cfg.ha_token));
    strlcpy(cfg.temp_entity,     doc["temp_entity"]     | "", sizeof(cfg.temp_entity));
    strlcpy(cfg.humidity_entity, doc["humidity_entity"] | "", sizeof(cfg.humidity_entity));

    JsonArrayConst slots = doc["slots"].as<JsonArrayConst>();
    cfg.num_slots = 0;
    for (JsonObjectConst o : slots) {
        if (cfg.num_slots >= MAX_SLOTS) break;
        SlotConfig &s = cfg.slots[cfg.num_slots++];
        memset(&s, 0, sizeof(s));
        s.type = (uint8_t)(o["type"] | (int)CARD_TOGGLE);
        strlcpy(s.entity_id, o["entity_id"] | "", sizeof(s.entity_id));
        strlcpy(s.label,     o["label"]     | "", sizeof(s.label));
        strlcpy(s.domain,    o["domain"]    | "", sizeof(s.domain));
        strlcpy(s.service,   o["service"]   | "", sizeof(s.service));
        strlcpy(s.attribute, o["attribute"] | "", sizeof(s.attribute));
        strlcpy(s.unit,      o["unit"]      | "", sizeof(s.unit));
        s.min_val = (int16_t)(o["min"] | 0);
        s.max_val = (int16_t)(o["max"] | 100);
        // Derive domain from entity_id if not supplied
        if (s.domain[0] == '\0' && s.entity_id[0]) {
            const char *dot = strchr(s.entity_id, '.');
            if (dot) { size_t n = dot - s.entity_id; if (n >= sizeof(s.domain)) n = sizeof(s.domain) - 1;
                       memcpy(s.domain, s.entity_id, n); s.domain[n] = '\0'; }
        }
    }

    return cfg.wifi_ssid[0] != '\0';
}
