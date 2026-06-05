#include "setup_portal.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char   *AP_SSID   = "JetClock-Setup";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);
static const uint8_t DNS_PORT  = 53;

static DNSServer      dns;
static AsyncWebServer server(80);
static bool           saved = false;

static bool is_captive_probe(AsyncWebServerRequest *req) {
    const String &p = req->url();
    return p == "/generate_204" || p == "/gen_204"
        || p == "/hotspot-detect.html"
        || p == "/library/test/success.html"
        || p == "/connecttest.txt"
        || p == "/redirect"
        || p == "/ncsi.txt"
        || p == "/success.txt";
}

void setup_portal_run(AppConfig &cfg, std::function<void()> tick_fn) {
    Serial.println("[portal] Starting WiFi setup AP");

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(AP_SSID);
    dns.start(DNS_PORT, "*", AP_IP);

    if (!LittleFS.begin(true)) {
        Serial.println("[portal] LittleFS mount failed");
    }

    // Serve the minimal WiFi setup page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/wifi.html", "text/html");
    });
    server.on("/wifi.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/wifi.html", "text/html");
    });

    // Captive portal OS probes → redirect to setup page
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/generate_204", HTTP_GET,
        [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
    server.on("/gen_204", HTTP_GET,
        [](AsyncWebServerRequest *req) { req->redirect("http://192.168.4.1/"); });
    server.on("/connecttest.txt", HTTP_GET,
        [](AsyncWebServerRequest *req) { req->send(200, "text/plain", "Microsoft Connect Test"); });
    server.on("/ncsi.txt", HTTP_GET,
        [](AsyncWebServerRequest *req) { req->send(200, "text/plain", "Microsoft NCSI"); });

    // POST /wifi — save WiFi credentials and reboot
    server.on("/wifi", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [&cfg](AsyncWebServerRequest *req, uint8_t *data, size_t len,
               size_t index, size_t total) {
            static String buf;
            if (index == 0) buf = "";
            buf += String((char *)data, len);
            if (index + len < total) return;

            JsonDocument doc;
            if (deserializeJson(doc, buf)) {
                req->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            const char *ssid = doc["wifi_ssid"] | "";
            const char *pass = doc["wifi_password"] | "";
            if (ssid[0] == '\0') {
                req->send(400, "application/json", "{\"error\":\"ssid required\"}");
                return;
            }

            strlcpy(cfg.wifi_ssid,     ssid, sizeof(cfg.wifi_ssid));
            strlcpy(cfg.wifi_password, pass, sizeof(cfg.wifi_password));
            config_save(cfg);
            saved = true;

            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // POST /factory-reset
    server.on("/factory-reset", HTTP_POST, [&cfg](AsyncWebServerRequest *req) {
        config_erase();
        config_reset(cfg);
        req->send(200, "application/json", "{\"ok\":true}");
        delay(300);
        ESP.restart();
    });

    server.onNotFound([](AsyncWebServerRequest *req) {
        if (is_captive_probe(req)) {
            req->redirect("http://192.168.4.1/");
        } else {
            req->redirect("http://192.168.4.1/");
        }
    });

    server.begin();
    Serial.println("[portal] Ready at http://192.168.4.1/");

    while (!saved) {
        dns.processNextRequest();
        if (tick_fn) tick_fn();
        delay(10);
    }

    Serial.println("[portal] WiFi credentials saved — rebooting");
    server.end();
    dns.stop();
    WiFi.softAPdisconnect(true);
    delay(300);
    ESP.restart();
}
