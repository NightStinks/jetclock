#include "ha_client.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static AppConfig       s_cfg;
static WebSocketsClient ws;
static int              ws_msg_id       = 1;
static bool             ws_authed       = false;
static bool             ws_subscribed   = false;

static ButtonStateCallback s_on_button;
static TempCallback        s_on_temp;
static HumidityCallback    s_on_humidity;

// ── Helpers ───────────────────────────────────────────────────────────────

static void ws_send_json(const JsonDocument &doc) {
    String payload;
    serializeJson(doc, payload);
    ws.sendTXT(payload);
}

static int button_index_for_entity(const char *entity_id) {
    for (int i = 0; i < s_cfg.num_buttons; i++) {
        if (strcmp(s_cfg.buttons[i].entity_id, entity_id) == 0) return i;
    }
    return -1;
}

static bool is_on_state(const char *state) {
    return strcmp(state, "on") == 0
        || strcmp(state, "true") == 0
        || strcmp(state, "home") == 0;
}

// ── WebSocket event handler ───────────────────────────────────────────────

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        Serial.println("[ha_ws] Connected");
        ws_authed    = false;
        ws_subscribed = false;
        break;

    case WStype_DISCONNECTED:
        Serial.println("[ha_ws] Disconnected — will reconnect");
        ws_authed    = false;
        ws_subscribed = false;
        break;

    case WStype_TEXT: {
        JsonDocument doc;
        if (deserializeJson(doc, (char *)payload)) break;

        const char *msg_type = doc["type"] | "";

        if (strcmp(msg_type, "auth_required") == 0) {
            JsonDocument auth;
            auth["type"]         = "auth";
            auth["access_token"] = s_cfg.ha_token;
            ws_send_json(auth);

        } else if (strcmp(msg_type, "auth_ok") == 0) {
            ws_authed = true;
            Serial.println("[ha_ws] Authenticated");

            // Subscribe to state changes for all button entities + sensors
            JsonDocument sub;
            sub["id"]   = ws_msg_id++;
            sub["type"] = "subscribe_events";
            sub["event_type"] = "state_changed";
            ws_send_json(sub);
            ws_subscribed = true;

        } else if (strcmp(msg_type, "auth_invalid") == 0) {
            Serial.println("[ha_ws] Auth failed — check HA token");

        } else if (strcmp(msg_type, "event") == 0) {
            const char *entity_id = doc["event"]["data"]["entity_id"] | "";
            const char *new_state = doc["event"]["data"]["new_state"]["state"] | "";

            int btn_idx = button_index_for_entity(entity_id);
            if (btn_idx >= 0 && s_on_button) {
                s_on_button(btn_idx, is_on_state(new_state));
            }

            if (s_cfg.temp_entity[0] && strcmp(entity_id, s_cfg.temp_entity) == 0) {
                if (s_on_temp) s_on_temp(atof(new_state));
            }
            if (s_cfg.humidity_entity[0] && strcmp(entity_id, s_cfg.humidity_entity) == 0) {
                if (s_on_humidity) s_on_humidity(atof(new_state));
            }
        }
        break;
    }
    default:
        break;
    }
}

// ── REST helpers ──────────────────────────────────────────────────────────

static void rest_get_state(const char *entity_id, const char *&state_out, String &buf) {
    char url[256];
    snprintf(url, sizeof(url), "%s/api/states/%s", s_cfg.ha_url, entity_id);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + s_cfg.ha_token);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    int code = http.GET();
    if (code == 200) {
        buf = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, buf)) {
            state_out = doc["state"] | "";
        }
    }
    http.end();
}

static void rest_post_service(const char *domain, const char *service, const char *entity_id) {
    char url[256];
    snprintf(url, sizeof(url), "%s/api/services/%s/%s", s_cfg.ha_url, domain, service);

    JsonDocument body;
    body["entity_id"] = entity_id;
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

// ── Public API ─────────────────────────────────────────────────────────────

void ha_client_init(const AppConfig &cfg,
                    ButtonStateCallback on_button_state,
                    TempCallback on_temp,
                    HumidityCallback on_humidity) {
    s_cfg       = cfg;
    s_on_button = on_button_state;
    s_on_temp   = on_temp;
    s_on_humidity = on_humidity;

    if (s_cfg.ha_url[0] == '\0') return;

    // Parse host and port from ha_url (e.g. "http://192.168.1.100:8123")
    String url(s_cfg.ha_url);
    bool use_ssl = url.startsWith("https://");
    url.replace("https://", "");
    url.replace("http://",  "");

    int colon = url.indexOf(':');
    String host = (colon >= 0) ? url.substring(0, colon) : url;
    int    port = (colon >= 0) ? url.substring(colon + 1).toInt() : (use_ssl ? 443 : 8123);

    if (use_ssl) {
        ws.beginSSL(host.c_str(), port, "/api/websocket");
    } else {
        ws.begin(host.c_str(), port, "/api/websocket");
    }
    ws.onEvent(ws_event);
    ws.setReconnectInterval(5000);

    Serial.printf("[ha_ws] Connecting to %s:%d\n", host.c_str(), port);

    ha_client_refresh_all();
}

void ha_client_tick() {
    ws.loop();
}

void ha_client_toggle(int button_idx) {
    if (button_idx < 0 || button_idx >= s_cfg.num_buttons) return;
    const ButtonConfig &b = s_cfg.buttons[button_idx];
    rest_post_service(b.domain, "toggle", b.entity_id);
}

void ha_client_refresh_all() {
    for (int i = 0; i < s_cfg.num_buttons; i++) {
        const char *state = nullptr;
        String buf;
        rest_get_state(s_cfg.buttons[i].entity_id, state, buf);
        // Re-parse — state pointer invalidates with buf
        JsonDocument doc;
        if (!deserializeJson(doc, buf)) {
            if (s_on_button) s_on_button(i, is_on_state(doc["state"] | ""));
        }
    }

    if (s_cfg.temp_entity[0]) {
        String buf;
        const char *state = nullptr;
        rest_get_state(s_cfg.temp_entity, state, buf);
        JsonDocument doc;
        if (!deserializeJson(doc, buf) && s_on_temp) {
            s_on_temp(atof(doc["state"] | "0"));
        }
    }

    if (s_cfg.humidity_entity[0]) {
        String buf;
        const char *state = nullptr;
        rest_get_state(s_cfg.humidity_entity, state, buf);
        JsonDocument doc;
        if (!deserializeJson(doc, buf) && s_on_humidity) {
            s_on_humidity(atof(doc["state"] | "0"));
        }
    }
}
