#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "ui.h"
#include "flight.h"
#include "enrichment.h"
#include "ha_client.h"
#include "setup_portal.h"

// ── Timing ────────────────────────────────────────────────────────────────
#define FLIGHT_POLL_MS   30000   // 30 s — adsb.lol soft rate limit
#define LVGL_TICK_MS         5   // LVGL heartbeat

static AppConfig cfg;

// ── WiFi ──────────────────────────────────────────────────────────────────

static bool wifi_connect() {
    Serial.printf("[wifi] Connecting to %s\n", cfg.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    for (int i = 0; i < 40; i++) {  // 20 s timeout
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(500);
    }

    Serial.println("[wifi] Connection failed");
    return false;
}

// ── NTP time ──────────────────────────────────────────────────────────────

static void ntp_init() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.print("[ntp] Syncing");
    struct tm t;
    for (int i = 0; i < 20; i++) {
        if (getLocalTime(&t)) { Serial.println(" OK"); return; }
        Serial.print(".");
        delay(500);
    }
    Serial.println(" timeout");
}

static void update_time_labels() {
    struct tm t;
    if (!getLocalTime(&t)) return;
    char hhmm[6], date[16];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
    strftime(date, sizeof(date), "%a %d %b", &t);
    ui_set_time(hhmm, date);
}

// ── Flight polling ────────────────────────────────────────────────────────

static NearestAircraft s_aircraft = {};
static char            s_last_callsign[16] = {};

static void poll_flight() {
    NearestAircraft ac;
    if (!flight_poll(ac)) {
        // Clear display on error / no traffic
        FlightData fd = {"NO FLIGHT", "", "---", "---", 0, 0, 0};
        ui_set_flight(fd, cfg.screen_bearing);
        memset(&s_aircraft, 0, sizeof(s_aircraft));
        s_last_callsign[0] = '\0';
        return;
    }

    s_aircraft = ac;

    // Enrich route info if the nearest aircraft changed
    static RouteInfo route = {};
    if (strcmp(ac.callsign, s_last_callsign) != 0) {
        strlcpy(s_last_callsign, ac.callsign, sizeof(s_last_callsign));
        route = {};
        enrichment_lookup(ac.callsign, route);
    }

    FlightData fd;
    fd.callsign    = ac.callsign;
    fd.airline     = route.valid ? route.airline     : "";
    fd.origin      = route.valid ? route.origin      : "---";
    fd.dest        = route.valid ? route.destination : "---";
    fd.distance_km = ac.distance_km;
    fd.bearing_deg = ac.bearing_deg;
    fd.track_deg   = ac.track_deg;

    ui_set_flight(fd, cfg.screen_bearing);
}

// ── HA callbacks ──────────────────────────────────────────────────────────

static void on_button_state(int idx, bool on) {
    ui_set_button_state(idx, on);
}

static void on_temp(float celsius) {
    ui_set_temp(celsius);
}

static void on_humidity(float pct) {
    ui_set_humidity(pct);
}

// ── Arduino entry points ──────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[boot] JetClock v1.0");

    // Load config from NVS
    bool has_config = config_load(cfg);

    // Initialise display regardless — show a status screen
    display_init();

    auto run_portal = [&]() {
        if (!LittleFS.begin(true)) {
            Serial.println("[boot] LittleFS failed — cannot serve setup UI");
        }
        // Show instructions on-screen while the portal is active
        lv_obj_t *scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        lv_obj_t *title = lv_label_create(scr);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0x58a6ff), 0);
        lv_label_set_text(title, "JetClock");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

        lv_obj_t *sub = lv_label_create(scr);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(0xe6edf3), 0);
        lv_label_set_text(sub, "Setup mode");
        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 116);

        lv_obj_t *body = lv_label_create(scr);
        lv_obj_set_style_text_font(body, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(body, lv_color_hex(0x8b949e), 0);
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(body, 400);
        lv_label_set_text(body,
            "Connect your phone or laptop to:\n\n"
            "Wi-Fi:  JetClock-Setup\n\n"
            "A setup page will open automatically.\n"
            "Enter your Wi-Fi details and\n"
            "Home Assistant connection.");
        lv_obj_align(body, LV_ALIGN_CENTER, 0, 30);

        uint32_t last_tick = millis();
        setup_portal_run(cfg, [&]() {
            uint32_t now = millis();
            lv_tick_inc(now - last_tick);
            last_tick = now;
            lv_timer_handler();
        });
    };

    if (!has_config || !config_is_configured(cfg)) {
        Serial.println("[boot] No config — starting setup portal");
        run_portal();
        return;  // unreachable (run_portal reboots)
    }

    // Connect to WiFi
    if (!wifi_connect()) {
        Serial.println("[boot] WiFi failed — starting setup portal");
        run_portal();
        return;  // unreachable
    }

    // Mount filesystem for any future OTA assets
    LittleFS.begin(false);

    // Sync time
    ntp_init();

    // Build UI with configured button labels
    ui_set_toggle_callback([](int idx) { ha_client_toggle(idx); });
    ui_init(cfg);

    // Restore last theme from NVS (stored separately — not in AppConfig for now)
    // ui_apply_theme("Warm Brown");  // default

    // Configure flight polling
    flight_set_home(cfg.home_lat, cfg.home_lon, cfg.radius_nm);

    // Connect to Home Assistant
    ha_client_init(cfg, on_button_state, on_temp, on_humidity);

    // Initial flight poll
    poll_flight();

    Serial.println("[boot] Ready");
}

static uint32_t last_flight_poll = 0;
static uint32_t last_time_update = 0;
static uint32_t last_lvgl_tick   = 0;

void loop() {
    uint32_t now = millis();

    // LVGL heartbeat
    if (now - last_lvgl_tick >= LVGL_TICK_MS) {
        lv_tick_inc(now - last_lvgl_tick);
        last_lvgl_tick = now;
    }
    display_tick();

    // HA WebSocket
    ha_client_tick();

    // Time labels — update every minute
    if (now - last_time_update >= 60000 || last_time_update == 0) {
        update_time_labels();
        last_time_update = now;
    }

    // Flight poll every 30 s
    if (now - last_flight_poll >= FLIGHT_POLL_MS || last_flight_poll == 0) {
        last_flight_poll = now;
        poll_flight();
    }

    delay(1);
}
