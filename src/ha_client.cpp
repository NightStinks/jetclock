#include "ha_client.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static AppConfig       s_cfg;
static WebSocketsClient ws;
static int              ws_msg_id     = 1;
static bool             ws_authed     = false;

static SlotStateCallback s_on_slot;
static TempCallback      s_on_temp;
static HumidityCallback  s_on_humidity;

// Cached on/off-ness per slot, so we can synthesise toggles for domains
// (lock, cover) that have no native "toggle" service.
static bool s_slot_on[MAX_SLOTS] = {};

// ── Helpers ───────────────────────────────────────────────────────────────

static void ws_send_json(const JsonDocument &doc) {
    String payload;
    serializeJson(doc, payload);
    ws.sendTXT(payload);
}

static bool is_on_state(const char *state) {
    return strcmp(state, "on") == 0 || strcmp(state, "open") == 0
        || strcmp(state, "unlocked") == 0 || strcmp(state, "home") == 0
        || strcmp(state, "playing") == 0 || strcmp(state, "true") == 0;
}

// Split "domain.service" into parts. Returns false if no dot.
static bool split_service(const char *full, char *dom, size_t dn, char *svc, size_t sn) {
    const char *dot = strchr(full, '.');
    if (!dot) return false;
    size_t d = dot - full;
    if (d >= dn) d = dn - 1;
    memcpy(dom, full, d); dom[d] = '\0';
    strlcpy(svc, dot + 1, sn);
    return true;
}

// ── REST service call (optionally with one numeric data field) ─────────────

static void rest_call_service(const char *domain, const char *service,
                              const char *entity_id,
                              const char *data_key = nullptr, int data_val = 0) {
    if (s_cfg.ha_url[0] == '\0') return;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/services/%s/%s", s_cfg.ha_url, domain, service);

    JsonDocument body;
    if (entity_id && entity_id[0]) body["entity_id"] = entity_id;
    if (data_key) body[data_key] = data_val;
    String body_str;
    serializeJson(body, body_str);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + s_cfg.ha_token);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    http.POST(body_str);
    http.end();
}

// ── State extraction ───────────────────────────────────────────────────────

// Pull the numeric reading a slot cares about out of a state object.
static bool slot_value(const SlotConfig &s, const char *state,
                       JsonObjectConst attrs, float &out) {
    switch (s.type) {
    case CARD_SLIDER:
    case CARD_LIGHT:
        if (strcmp(s.domain, "light") == 0) {
            if (attrs["brightness"].is<float>()) { out = attrs["brightness"].as<float>() * 100.0f / 255.0f; return true; }
        } else if (strcmp(s.domain, "cover") == 0) {
            if (attrs["current_position"].is<int>()) { out = attrs["current_position"].as<int>(); return true; }
        } else if (strcmp(s.domain, "fan") == 0) {
            if (attrs["percentage"].is<int>()) { out = attrs["percentage"].as<int>(); return true; }
        }
        return false;
    case CARD_CLIMATE:
        if (attrs["temperature"].is<float>()) { out = attrs["temperature"].as<float>(); return true; }
        return false;
    case CARD_SENSOR:
        if (s.attribute[0]) {
            if (attrs[s.attribute].is<float>()) { out = attrs[s.attribute].as<float>(); return true; }
            return false;
        }
        if (state[0] && (isdigit((int)state[0]) || state[0] == '-' || state[0] == '.')) {
            out = atof(state); return true;
        }
        return false;
    default:
        return false;
    }
}

static void emit_slot(int i, const char *state, JsonObjectConst attrs) {
    const SlotConfig &s = s_cfg.slots[i];
    s_slot_on[i] = is_on_state(state);
    float v = 0; bool has = slot_value(s, state, attrs, v);
    if (s_on_slot) s_on_slot(i, state, v, has);
}

// ── WebSocket event handler ────────────────────────────────────────────────

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        ws_authed = false;
        break;
    case WStype_DISCONNECTED:
        ws_authed = false;
        break;
    case WStype_TEXT: {
        JsonDocument doc;
        if (deserializeJson(doc, (char *)payload)) break;
        const char *msg_type = doc["type"] | "";

        if (strcmp(msg_type, "auth_required") == 0) {
            JsonDocument auth;
            auth["type"] = "auth";
            auth["access_token"] = s_cfg.ha_token;
            ws_send_json(auth);
        } else if (strcmp(msg_type, "auth_ok") == 0) {
            ws_authed = true;
            JsonDocument sub;
            sub["id"]   = ws_msg_id++;
            sub["type"] = "subscribe_events";
            sub["event_type"] = "state_changed";
            ws_send_json(sub);
        } else if (strcmp(msg_type, "event") == 0) {
            const char *entity_id = doc["event"]["data"]["entity_id"] | "";
            JsonObjectConst ns = doc["event"]["data"]["new_state"];
            const char *new_state = ns["state"] | "";
            JsonObjectConst attrs = ns["attributes"];

            for (int i = 0; i < s_cfg.num_slots; i++) {
                if (strcmp(s_cfg.slots[i].entity_id, entity_id) == 0)
                    emit_slot(i, new_state, attrs);
            }
            if (s_cfg.temp_entity[0] && strcmp(entity_id, s_cfg.temp_entity) == 0 && s_on_temp)
                s_on_temp(atof(new_state));
            if (s_cfg.humidity_entity[0] && strcmp(entity_id, s_cfg.humidity_entity) == 0 && s_on_humidity)
                s_on_humidity(atof(new_state));
        }
        break;
    }
    default:
        break;
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

void ha_client_init(const AppConfig &cfg,
                    SlotStateCallback on_slot_state,
                    TempCallback on_temp,
                    HumidityCallback on_humidity) {
    s_cfg         = cfg;
    s_on_slot     = on_slot_state;
    s_on_temp     = on_temp;
    s_on_humidity = on_humidity;

    if (s_cfg.ha_url[0] == '\0') return;

    String url(s_cfg.ha_url);
    bool use_ssl = url.startsWith("https://");
    url.replace("https://", "");
    url.replace("http://", "");
    int colon = url.indexOf(':');
    String host = (colon >= 0) ? url.substring(0, colon) : url;
    int    port = (colon >= 0) ? url.substring(colon + 1).toInt() : (use_ssl ? 443 : 8123);

    if (use_ssl) ws.beginSSL(host.c_str(), port, "/api/websocket");
    else         ws.begin(host.c_str(), port, "/api/websocket");
    ws.onEvent(ws_event);
    ws.setReconnectInterval(5000);

    ha_client_refresh_all();
}

void ha_client_tick() { ws.loop(); }

void ha_client_activate(int i) {
    if (i < 0 || i >= s_cfg.num_slots) return;
    const SlotConfig &s = s_cfg.slots[i];
    const char *d = s.domain;

    switch (s.type) {
    case CARD_ACTION: {
        // Explicit "domain.service" if given, else sensible default per domain.
        char dom[24], svc[24];
        if (s.service[0] && split_service(s.service, dom, sizeof(dom), svc, sizeof(svc))) {
            rest_call_service(dom, svc, s.entity_id);
        } else if (strcmp(d, "scene") == 0)      rest_call_service("scene", "turn_on", s.entity_id);
        else if (strcmp(d, "script") == 0)       rest_call_service("script", "turn_on", s.entity_id);
        else if (strcmp(d, "automation") == 0)   rest_call_service("automation", "trigger", s.entity_id);
        else if (strcmp(d, "button") == 0)       rest_call_service("button", "press", s.entity_id);
        else if (strcmp(d, "input_button") == 0) rest_call_service("input_button", "press", s.entity_id);
        else                                     rest_call_service(d, "turn_on", s.entity_id);
        break;
    }
    case CARD_COVER:
        rest_call_service("cover", s_slot_on[i] ? "close_cover" : "open_cover", s.entity_id);
        break;
    case CARD_LOCK:
        rest_call_service("lock", s_slot_on[i] ? "lock" : "unlock", s.entity_id);
        break;
    case CARD_SELECT:
        rest_call_service(d[0] ? d : "select", "select_next", s.entity_id);
        break;
    case CARD_TOGGLE:
    case CARD_LIGHT:
    default:
        rest_call_service(d[0] ? d : "homeassistant", "toggle", s.entity_id);
        break;
    }
}

void ha_client_set_value(int i, int value) {
    if (i < 0 || i >= s_cfg.num_slots) return;
    const SlotConfig &s = s_cfg.slots[i];
    if (value < 0) value = 0; if (value > 100) value = 100;

    if (strcmp(s.domain, "light") == 0)
        rest_call_service("light", "turn_on", s.entity_id, "brightness_pct", value);
    else if (strcmp(s.domain, "cover") == 0)
        rest_call_service("cover", "set_cover_position", s.entity_id, "position", value);
    else if (strcmp(s.domain, "fan") == 0)
        rest_call_service("fan", "set_percentage", s.entity_id, "percentage", value);
}

void ha_client_refresh_all() {
    auto fetch = [](const char *entity_id, JsonDocument &doc) -> bool {
        if (!entity_id[0]) return false;
        char url[256];
        snprintf(url, sizeof(url), "%s/api/states/%s", s_cfg.ha_url, entity_id);
        HTTPClient http;
        http.begin(url);
        http.addHeader("Authorization", String("Bearer ") + s_cfg.ha_token);
        http.setTimeout(5000);
        bool ok = false;
        if (http.GET() == 200) ok = !deserializeJson(doc, http.getString());
        http.end();
        return ok;
    };

    for (int i = 0; i < s_cfg.num_slots; i++) {
        JsonDocument doc;
        if (fetch(s_cfg.slots[i].entity_id, doc))
            emit_slot(i, doc["state"] | "", doc["attributes"]);
    }
    if (s_cfg.temp_entity[0]) {
        JsonDocument doc;
        if (fetch(s_cfg.temp_entity, doc) && s_on_temp) s_on_temp(atof(doc["state"] | "0"));
    }
    if (s_cfg.humidity_entity[0]) {
        JsonDocument doc;
        if (fetch(s_cfg.humidity_entity, doc) && s_on_humidity) s_on_humidity(atof(doc["state"] | "0"));
    }
}
