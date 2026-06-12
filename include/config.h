#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_SLOTS 5
#define MAX_BUTTONS MAX_SLOTS   // legacy alias
#define CONFIG_VERSION 2

// Card type for a side-panel slot. Append new types at the end — the numeric
// values are persisted in NVS and sent over JSON, so don't reorder.
enum CardType : uint8_t {
    CARD_EMPTY = 0,
    CARD_TOGGLE,    // on/off: switch, light, input_boolean, fan
    CARD_SLIDER,    // 0–100 drag: light brightness, fan %, cover position
    CARD_SENSOR,    // read-only value + unit
    CARD_ACTION,    // momentary: scene, script, automation, button press
    CARD_COVER,     // open / stop / close (+ position)
    CARD_LIGHT,     // on/off + brightness (+ colour temp)
    CARD_CLIMATE,   // setpoint up/down + state
    CARD_LOCK,      // lock / unlock
    CARD_SELECT,    // option select (select / input_select)
    CARD_TYPE_COUNT
};

// One configurable card. Generic fields cover every card type; each type
// interprets the ones it needs (e.g. SLIDER uses min/max/attribute/service,
// SENSOR uses attribute/unit, ACTION uses service).
struct SlotConfig {
    uint8_t type;          // CardType
    char    entity_id[64];
    char    label[24];
    char    domain[16];    // derived from entity_id ("light", "cover", …)
    char    service[40];   // explicit "domain.service" (ACTION / overrides)
    char    attribute[24]; // state attribute to read (brightness, current_position…)
    char    unit[10];      // SENSOR display unit suffix
    int16_t min_val;       // SLIDER range min (default 0)
    int16_t max_val;       // SLIDER range max (default 100)
};

// Legacy name kept so older call sites still compile during migration.
typedef SlotConfig ButtonConfig;

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

    // Side-panel cards
    SlotConfig slots[MAX_SLOTS];
    int num_slots;
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
