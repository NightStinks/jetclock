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
#include "web_server.h"

#define FLIGHT_POLL_MS 30000
#define LVGL_TICK_MS       5

static AppConfig cfg;

// ── WiFi ──────────────────────────────────────────────────────────────────

static bool wifi_connect() {
    Serial.printf("[wifi] Connecting to %s\n", cfg.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] Connected: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(500);
    }
    Serial.println("[wifi] Timed out");
    return false;
}

// ── NTP ───────────────────────────────────────────────────────────────────

static void ntp_init() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    struct tm t;
    for (int i = 0; i < 20; i++) {
        if (getLocalTime(&t)) return;
        delay(500);
    }
}

static void update_time_labels() {
    struct tm t;
    if (!getLocalTime(&t)) return;
    char hhmm[6], date[16];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
    strftime(date, sizeof(date), "%a %d %b", &t);
    ui_set_time(hhmm, date);
}

// ── Flight ────────────────────────────────────────────────────────────────

static NearestAircraft s_aircraft   = {};
static char            s_last_cs[16] = {};

static void poll_flight() {
    NearestAircraft ac;
    if (!flight_poll(ac)) {
        FlightData fd = {"NO FLIGHT", "", "---", "---", 0, 0, 0};
        ui_set_flight(fd, cfg.screen_bearing);
        memset(&s_aircraft, 0, sizeof(s_aircraft));
        s_last_cs[0] = '\0';
        return;
    }
    s_aircraft = ac;

    static RouteInfo route = {};
    if (strcmp(ac.callsign, s_last_cs) != 0) {
        strlcpy(s_last_cs, ac.callsign, sizeof(s_last_cs));
        route = {};
        enrichment_lookup(ac.callsign, route);
    }

    FlightData fd;
    fd.callsign    = ac.callsign;
    fd.airline     = route.valid ? route.airline      : "";
    fd.origin      = route.valid ? route.origin       : "---";
    fd.dest        = route.valid ? route.destination  : "---";
    fd.distance_km = ac.distance_km;
    fd.bearing_deg = ac.bearing_deg;
    fd.track_deg   = ac.track_deg;
    ui_set_flight(fd, cfg.screen_bearing);
}

// ── HA callbacks ──────────────────────────────────────────────────────────

static void on_button_state(int i, bool on)  { ui_set_button_state(i, on); }
static void on_temp(float c)                  { ui_set_temp(c); }
static void on_humidity(float p)              { ui_set_humidity(p); }

// ── Screen helpers ────────────────────────────────────────────────────────

static void lvgl_tick_fn(uint32_t &last) {
    uint32_t now = millis();
    lv_tick_inc(now - last);
    last = now;
    lv_timer_handler();
}

// Draw the WiFi-only captive portal screen.
static void show_portal_screen() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x58a6ff), 0);
    lv_label_set_text(title, "JetClock");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *sub = lv_label_create(scr);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xe6edf3), 0);
    lv_label_set_text(sub, "Wi-Fi setup");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 126);

    lv_obj_t *body = lv_label_create(scr);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0x8b949e), 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, 420);
    lv_label_set_text(body,
        "Connect to Wi-Fi:\n\n"
        "JetClock-Setup\n\n"
        "A page will open automatically.");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 20);
}

// Draw the "finish setup at jetclock.local" screen.
static void show_configure_screen() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x58a6ff), 0);
    lv_label_set_text(title, "JetClock");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *sub = lv_label_create(scr);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xe6edf3), 0);
    lv_label_set_text(sub, "Almost ready");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 126);

    lv_obj_t *body = lv_label_create(scr);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0x8b949e), 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, 420);
    lv_label_set_text(body, "Finish setup at:\n");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *url = lv_label_create(scr);
    lv_obj_set_style_text_font(url, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(url, lv_color_hex(0x58a6ff), 0);
    lv_label_set_text(url, "jetclock.local");
    lv_obj_align(url, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, 420);
    lv_label_set_text(hint, "Open a browser on your home network");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -60);
}

// ── Arduino entry points ──────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[boot] JetClock v1.0");

    config_load(cfg);
    display_init();
    LittleFS.begin(true);

    // Phase 1: no WiFi credentials → show WiFi-only captive portal
    if (!config_is_configured(cfg)) {
        Serial.println("[boot] No WiFi — starting captive portal");
        show_portal_screen();
        uint32_t t = millis();
        setup_portal_run(cfg, [&]() { lvgl_tick_fn(t); });
        return; // unreachable — portal reboots
    }

    // Connect to WiFi
    if (!wifi_connect()) {
        Serial.println("[boot] WiFi failed — back to portal");
        show_portal_screen();
        config_reset(cfg); // clear bad credentials
        config_save(cfg);
        uint32_t t = millis();
        setup_portal_run(cfg, [&]() { lvgl_tick_fn(t); });
        return; // unreachable
    }

    // Start mDNS + persistent config web server (always, on home WiFi)
    web_server_init(cfg);
    ntp_init();

    // Phase 2: WiFi connected but location not set → prompt to visit jetclock.local
    bool location_set = (cfg.home_lat != 0.0f || cfg.home_lon != 0.0f);
    if (!location_set) {
        Serial.println("[boot] Location not set — showing configure screen");
        show_configure_screen();
        // Loop forever — web server handles config updates, device reboots on save
        for (;;) {
            uint32_t now = millis();
            static uint32_t lt = 0;
            lv_tick_inc(now - lt); lt = now;
            lv_timer_handler();
            delay(10);
        }
    }

    // Fully configured — start the main app
    flight_set_home(cfg.home_lat, cfg.home_lon, cfg.radius_nm);
    ui_set_toggle_callback([](int idx) { ha_client_toggle(idx); });
    ui_init(cfg);
    ha_client_init(cfg, on_button_state, on_temp, on_humidity);
    poll_flight();
    Serial.println("[boot] Ready");
}

static uint32_t last_flight = 0;
static uint32_t last_time   = 0;
static uint32_t last_tick   = 0;

void loop() {
    uint32_t now = millis();

    if (now - last_tick >= LVGL_TICK_MS) {
        lv_tick_inc(now - last_tick);
        last_tick = now;
    }
    display_tick();
    ha_client_tick();

    if (now - last_time >= 60000 || last_time == 0) {
        update_time_labels();
        last_time = now;
    }
    if (now - last_flight >= FLIGHT_POLL_MS || last_flight == 0) {
        last_flight = now;
        poll_flight();
    }

    delay(1);
}
