#include "config.h"
#include <Preferences.h>

static const char *NVS_NS = "jetclock";

bool config_load(AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);  // read-only

    if (!prefs.isKey("version")) {
        prefs.end();
        cfg = kDefaultConfig;
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

    cfg.num_buttons = prefs.getInt("num_buttons", 0);
    if (cfg.num_buttons > MAX_BUTTONS) cfg.num_buttons = MAX_BUTTONS;

    for (int i = 0; i < cfg.num_buttons; i++) {
        char key[20];
        snprintf(key, sizeof(key), "btn%d_eid", i);
        prefs.getString(key, cfg.buttons[i].entity_id, sizeof(cfg.buttons[i].entity_id));
        snprintf(key, sizeof(key), "btn%d_lbl", i);
        prefs.getString(key, cfg.buttons[i].label, sizeof(cfg.buttons[i].label));
        snprintf(key, sizeof(key), "btn%d_dom", i);
        prefs.getString(key, cfg.buttons[i].domain, sizeof(cfg.buttons[i].domain));
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
    prefs.putInt("num_buttons",  cfg.num_buttons);

    for (int i = 0; i < cfg.num_buttons; i++) {
        char key[20];
        snprintf(key, sizeof(key), "btn%d_eid", i);
        prefs.putString(key, cfg.buttons[i].entity_id);
        snprintf(key, sizeof(key), "btn%d_lbl", i);
        prefs.putString(key, cfg.buttons[i].label);
        snprintf(key, sizeof(key), "btn%d_dom", i);
        prefs.putString(key, cfg.buttons[i].domain);
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

    JsonArray btns = doc["buttons"].to<JsonArray>();
    for (int i = 0; i < cfg.num_buttons; i++) {
        JsonObject b = btns.add<JsonObject>();
        b["entity_id"] = cfg.buttons[i].entity_id;
        b["label"]     = cfg.buttons[i].label;
        b["domain"]    = cfg.buttons[i].domain;
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

    JsonArrayConst btns = doc["buttons"].as<JsonArrayConst>();
    cfg.num_buttons = 0;
    for (JsonObjectConst b : btns) {
        if (cfg.num_buttons >= MAX_BUTTONS) break;
        int i = cfg.num_buttons++;
        strlcpy(cfg.buttons[i].entity_id, b["entity_id"] | "", sizeof(cfg.buttons[i].entity_id));
        strlcpy(cfg.buttons[i].label,     b["label"]     | "", sizeof(cfg.buttons[i].label));
        strlcpy(cfg.buttons[i].domain,    b["domain"]    | "", sizeof(cfg.buttons[i].domain));
    }

    return cfg.wifi_ssid[0] != '\0';
}
