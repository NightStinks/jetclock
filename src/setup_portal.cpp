#include "setup_portal.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char *AP_SSID = "JetClock-Setup";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);
static const uint8_t DNS_PORT = 53;

static DNSServer      dns;
static AsyncWebServer server(80);
static bool           config_saved = false;

// ── Captive portal detection ──────────────────────────────────────────────────
// iOS, Android and Windows all probe specific URLs. Respond with a redirect
// so the OS shows the "join network" prompt automatically.

static void redirect_to_portal(AsyncWebServerRequest *req) {
    req->redirect("http://192.168.4.1/");
}

static bool is_captive_probe(AsyncWebServerRequest *req) {
    const String path = req->url();
    return path == "/generate_204"
        || path == "/gen_204"
        || path == "/hotspot-detect.html"
        || path == "/library/test/success.html"
        || path == "/connecttest.txt"
        || path == "/redirect"
        || path == "/ncsi.txt"
        || path == "/success.txt"
        || path == "/canonical.html";
}

// ── Route handlers ────────────────────────────────────────────────────────────

static void handle_get_config(AsyncWebServerRequest *req, AppConfig &cfg) {
    JsonDocument doc;
    config_to_json(cfg, doc, true);  // mask secrets

    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
}

static void handle_post_config(AsyncWebServerRequest *req,
                                uint8_t *data, size_t len,
                                size_t index, size_t total,
                                AppConfig &cfg) {
    // Accumulate body (ESPAsyncWebServer delivers body in chunks)
    static String body_buf;
    if (index == 0) body_buf = "";
    body_buf += String((char *)data, len);

    if (index + len < total) return;  // wait for last chunk

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body_buf);
    if (err) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    AppConfig new_cfg;

    // Preserve existing secrets if the placeholder was sent back
    new_cfg = cfg;
    const char *new_pass = doc["wifi_password"] | "";
    if (strcmp(new_pass, "********") != 0) {
        strlcpy(new_cfg.wifi_password, new_pass, sizeof(new_cfg.wifi_password));
    }
    const char *new_tok = doc["ha_token"] | "";
    if (strcmp(new_tok, "********") != 0) {
        strlcpy(new_cfg.ha_token, new_tok, sizeof(new_cfg.ha_token));
    }

    // Overlay everything else
    config_from_json(doc, new_cfg);

    // Restore secrets if they were masked
    if (strcmp(new_pass, "********") == 0) {
        strlcpy(new_cfg.wifi_password, cfg.wifi_password, sizeof(new_cfg.wifi_password));
    }
    if (strcmp(new_tok, "********") == 0) {
        strlcpy(new_cfg.ha_token, cfg.ha_token, sizeof(new_cfg.ha_token));
    }

    config_save(new_cfg);
    cfg = new_cfg;
    config_saved = true;

    req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
}

static void handle_factory_reset(AsyncWebServerRequest *req, AppConfig &cfg) {
    config_erase();
    cfg = kDefaultConfig;
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

// ── Portal entry point ────────────────────────────────────────────────────────

void setup_portal_run(AppConfig &cfg) {
    Serial.println("[portal] Starting setup AP: " + String(AP_SSID));

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(AP_SSID);

    dns.start(DNS_PORT, "*", AP_IP);  // wildcard DNS → portal IP

    if (!LittleFS.begin(true)) {
        Serial.println("[portal] LittleFS mount failed");
    }

    // Serve the setup UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/index.html", "text/html");
    });

    // Config endpoints
    server.on("/config", HTTP_GET, [&cfg](AsyncWebServerRequest *req) {
        handle_get_config(req, cfg);
    });

    server.on("/config", HTTP_POST,
        [](AsyncWebServerRequest *req) {},  // onRequest (called after body)
        nullptr,                             // onUpload
        [&cfg](AsyncWebServerRequest *req, uint8_t *data, size_t len,
               size_t index, size_t total) {
            handle_post_config(req, data, len, index, total, cfg);
        }
    );

    server.on("/factory-reset", HTTP_POST, [&cfg](AsyncWebServerRequest *req) {
        handle_factory_reset(req, cfg);
    });

    // Captive portal probes — redirect before the catch-all
    server.onNotFound([](AsyncWebServerRequest *req) {
        if (is_captive_probe(req)) {
            redirect_to_portal(req);
        } else {
            redirect_to_portal(req);
        }
    });

    // iOS / macOS captive probe (needs 200 with specific content to trigger the prompt)
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/generate_204", HTTP_GET, redirect_to_portal);
    server.on("/gen_204",      HTTP_GET, redirect_to_portal);
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/plain", "Microsoft Connect Test");
    });
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/plain", "Microsoft NCSI");
    });

    server.begin();
    Serial.println("[portal] Web server started — http://192.168.4.1/");

    while (!config_saved) {
        dns.processNextRequest();
        delay(10);
    }

    Serial.println("[portal] Config saved — rebooting...");
    server.end();
    dns.stop();
    WiFi.softAPdisconnect(true);

    delay(500);
    ESP.restart();
}
