#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_BUTTONS 5
#define CONFIG_VERSION 1

struct ButtonConfig {
    char entity_id[64];
    char label[32];
    char domain[20];  // "switch", "light", "input_boolean", etc.
};

struct AppConfig {
    int version;

    // Network
    char wifi_ssid[64];
    char wifi_password[64];

    // Location
    float home_lat;
    float home_lon;
    int   radius_nm;       // ADS-B search radius in nautical miles
    int   screen_bearing;  // Compass direction the display faces, 0-359°

    // Home Assistant
    char ha_url[128];      // e.g. "http://192.168.1.100:8123"
    char ha_token[256];    // Long-lived access token

    // Optional HA sensor entities for status bar
    char temp_entity[64];
    char humidity_entity[64];

    // Smart home buttons
    ButtonConfig buttons[MAX_BUTTONS];
    int num_buttons;
};

// Zero-fill cfg and apply factory defaults.
void config_reset(AppConfig &cfg);

// Load from NVS. Returns false and fills defaults if nothing stored yet.
bool config_load(AppConfig &cfg);

// Persist to NVS.
void config_save(const AppConfig &cfg);

// Erase all stored config (factory reset).
void config_erase();

// Returns true if the device has been configured (has WiFi credentials).
bool config_is_configured(const AppConfig &cfg);

// JSON helpers — used by the setup web server.
void config_to_json(const AppConfig &cfg, JsonDocument &doc, bool mask_secrets = false);
bool config_from_json(const JsonDocument &doc, AppConfig &cfg);
