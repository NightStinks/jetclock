#include "ui.h"

#include <lvgl.h>
#include <math.h>

// Plane image — src/plane_img.c (80x80, nose pointing RIGHT)
// Transparent pixels are 0x0000 (black), rendered transparent via chroma key
LV_IMG_DECLARE(plane_img_80);

// ── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t *page_main = nullptr;

// Status bar
static lv_obj_t *lbl_time;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_humidity;

// Flight info
static lv_obj_t *lbl_callsign;
static lv_obj_t *lbl_airline;
static lv_obj_t *lbl_origin_code;
static lv_obj_t *lbl_origin_city;
static lv_obj_t *lbl_dest_code;
static lv_obj_t *lbl_dest_city;
static lv_obj_t *lbl_distance;
static lv_obj_t *img_direction_arrow;
static lv_obj_t *arrow_container;

// WiFi signal bars (4 rectangles in the status bar)
static lv_obj_t *wifi_bar[4];

// Side-panel slot cards (5 slots). Widgets present depend on card type.
struct SlotWidgets {
    uint8_t   type;
    lv_obj_t *container;
    lv_obj_t *label;    // card name
    lv_obj_t *value;    // state/value text (toggle state, sensor reading, slider %)
    lv_obj_t *slider;   // slider cards only
};
static SlotWidgets s_slots[MAX_SLOTS];
static int         s_slot_idx[MAX_SLOTS];  // stable user_data for event callbacks


static void (*s_activate_cb)(int) = nullptr;
static void (*s_value_cb)(int, int) = nullptr;

// Cached config
static AppConfig s_cfg;
static int   s_screen_bearing = 0;
static float s_bearing_deg    = 0.0f;
static float s_distance_km    = 0.0f;

// ── Colour theme state ────────────────────────────────────────────────────
struct Theme {
    lv_color_t bg, card, border, pri, sec, accent;
    bool       gradient;
};

static Theme s_theme = {
    .bg     = LV_COLOR_MAKE(0x6B, 0x5A, 0x48),
    .card   = LV_COLOR_MAKE(0x5A, 0x4A, 0x3A),
    .border = LV_COLOR_MAKE(0x7A, 0x6A, 0x58),
    .pri    = LV_COLOR_MAKE(0xFE, 0xF4, 0xEA),
    .sec    = LV_COLOR_MAKE(0x9E, 0xAB, 0x9A),
    .accent = LV_COLOR_MAKE(0xE8, 0xD8, 0xC9),
    .gradient = false,
};

// ── Helpers ───────────────────────────────────────────────────────────────

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w,
                             const char *text, const lv_font_t *font,
                             lv_color_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                             lv_label_long_mode_t lm = LV_LABEL_LONG_WRAP) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    if (w > 0) lv_obj_set_width(lbl, w);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, lm);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_align(lbl, align, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    return lbl;
}

static lv_obj_t *make_rect(lv_obj_t *parent, int x, int y, int w, int h,
                            lv_color_t bg, int radius = 0) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// ── Page 1 — Main ─────────────────────────────────────────────────────────

static void build_slot(const AppConfig &cfg, int i, int y);

static void build_page_main(const AppConfig &cfg) {
    page_main = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page_main, 480, 480);
    lv_obj_set_pos(page_main, 0, 0);
    lv_obj_set_style_bg_color(page_main, s_theme.bg, 0);
    lv_obj_set_style_bg_opa(page_main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_main, 0, 0);
    lv_obj_set_style_pad_all(page_main, 0, 0);
    lv_obj_clear_flag(page_main, LV_OBJ_FLAG_SCROLLABLE);

    // ── Status bar ──────────────────────────────────────────────────────
    lv_obj_t *status_bar = make_rect(page_main, 0, 0, 480, 68, s_theme.card);

    lbl_time = make_label(status_bar, 16, 12, 0, "--:--",
                          &lv_font_montserrat_40, s_theme.pri);
    lbl_date = make_label(status_bar, 156, 22, 0, "--- -- ---",
                          &lv_font_montserrat_18, s_theme.sec);
    lbl_temp = make_label(status_bar, 316, 16, 0, "--°C",
                          &lv_font_montserrat_26, s_theme.pri);
    lbl_humidity = make_label(status_bar, 406, 16, 0, "--%",
                              &lv_font_montserrat_26, s_theme.sec);

    // WiFi signal bars — small, discreet icon-style indicator between date
    // and temp. 4 bars × 3px wide, 3px gap. Heights: 5, 8, 11, 14px.
    static const int bar_h[4]   = {5, 8, 11, 14};
    static const int bar_x0     = 284;
    static const int bar_bottom = 44;
    for (int b = 0; b < 4; b++) {
        lv_obj_t *bar = lv_obj_create(status_bar);
        lv_obj_set_size(bar, 3, bar_h[b]);
        lv_obj_set_pos(bar, bar_x0 + b * 6, bar_bottom - bar_h[b]);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, s_theme.sec, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_20, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        wifi_bar[b] = bar;
    }

    // Horizontal divider
    make_rect(page_main, 0, 68, 480, 1, s_theme.border);
    // Vertical divider
    make_rect(page_main, 294, 68, 1, 412, s_theme.border);

    // ── Left: Flight info ────────────────────────────────────────────────
    make_label(page_main, 14, 76, 0, "NEAREST FLIGHT",
               &lv_font_montserrat_12, s_theme.sec);

    lbl_callsign = make_label(page_main, 0, 92, 292, "------",
                              &lv_font_montserrat_48, s_theme.accent,
                              LV_TEXT_ALIGN_CENTER);
    lbl_airline  = make_label(page_main, 0, 158, 292, " ",
                              &lv_font_montserrat_16, s_theme.sec,
                              LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);

    // Origin/destination stacked as code (large) over city name (small) —
    // full 292px width per line gives long city names room to breathe.
    lbl_origin_code = make_label(page_main, 0, 184, 292, "---",
                              &lv_font_montserrat_24, s_theme.pri,
                              LV_TEXT_ALIGN_CENTER);
    lbl_origin_city = make_label(page_main, 0, 212, 292, "",
                              &lv_font_montserrat_14, s_theme.sec,
                              LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    lbl_dest_code   = make_label(page_main, 0, 238, 292, "---",
                              &lv_font_montserrat_24, s_theme.pri,
                              LV_TEXT_ALIGN_CENTER);
    lbl_dest_city   = make_label(page_main, 0, 266, 292, "",
                              &lv_font_montserrat_14, s_theme.sec,
                              LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    lbl_distance = make_label(page_main, 0, 292, 292, "--",
                              &lv_font_montserrat_16, s_theme.sec,
                              LV_TEXT_ALIGN_CENTER);

    // Direction arrow area (non-interactive for now) — a touch smaller than
    // before to make room for the stacked origin/destination lines above.
    arrow_container = make_rect(page_main, 0, 316, 292, 164, s_theme.bg);

    img_direction_arrow = lv_img_create(arrow_container);
    lv_img_set_src(img_direction_arrow, &plane_img_80);
    lv_img_set_zoom(img_direction_arrow, 500);   // 500/256 × 80px ≈ 156px
    // pos + pivot = container center (146, 82); zoom scales around the
    // pivot without moving it, so this stays centered at any zoom level.
    lv_obj_set_pos(img_direction_arrow, 106, 42);
    lv_img_set_pivot(img_direction_arrow, 40, 40);
    lv_img_set_antialias(img_direction_arrow, true);

    // ── Right: Slot cards ────────────────────────────────────────────────
    static const int slot_y[MAX_SLOTS] = {76, 158, 240, 322, 404};
    for (int i = 0; i < MAX_SLOTS; i++) build_slot(cfg, i, slot_y[i]);
}

// Build one slot card (170x76 at x=302) according to its card type.
static void build_slot(const AppConfig &cfg, int i, int y) {
    s_slot_idx[i] = i;
    SlotWidgets &w = s_slots[i];
    memset(&w, 0, sizeof(w));
    uint8_t type = (i < cfg.num_slots) ? cfg.slots[i].type : CARD_EMPTY;
    const char *name = (i < cfg.num_slots && cfg.slots[i].label[0]) ? cfg.slots[i].label : "";
    bool configured = (i < cfg.num_slots && cfg.slots[i].entity_id[0]);
    w.type = type;

    lv_obj_t *card = lv_obj_create(page_main);
    lv_obj_set_pos(card, 302, y);
    lv_obj_set_size(card, 170, 76);
    lv_obj_set_style_bg_color(card, s_theme.card, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, s_theme.border, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    w.container = card;

    if (type == CARD_EMPTY || !configured) {
        if (name[0]) {
            lv_obj_t *lbl = make_label(card, 0, 0, 162, name, &lv_font_montserrat_16,
                                       s_theme.sec, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
            lv_obj_center(lbl);
            w.label = lbl;
        }
        return;
    }

    if (type == CARD_SLIDER) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        // Fill bar behind the labels — grows left to right as user drags
        lv_obj_t *fill = lv_obj_create(card);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, 1, 76);
        lv_obj_set_style_bg_color(fill, s_theme.accent, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_30, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 12, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        w.slider = fill;
        w.label = make_label(card, 10, 10, 100, name, &lv_font_montserrat_14, s_theme.sec,
                             LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_DOT);
        w.value = make_label(card, 0, 10, 158, "--%", &lv_font_montserrat_14, s_theme.pri,
                             LV_TEXT_ALIGN_RIGHT);
        // Update fill + label while dragging (visual only)
        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            int idx = *(int *)lv_event_get_user_data(e);
            lv_indev_t *indev = lv_indev_get_act(); if (!indev) return;
            lv_point_t pt; lv_indev_get_point(indev, &pt);
            int rel = pt.x - 302; if (rel < 0) rel = 0; if (rel > 170) rel = 170;
            if (s_slots[idx].slider) lv_obj_set_width(s_slots[idx].slider, rel);
            if (s_slots[idx].value) {
                char b[8]; snprintf(b, sizeof(b), "%d%%", rel * 100 / 170);
                lv_label_set_text(s_slots[idx].value, b);
            }
        }, LV_EVENT_PRESSING, &s_slot_idx[i]);
        // Send to HA on release
        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            int idx = *(int *)lv_event_get_user_data(e);
            lv_indev_t *indev = lv_indev_get_act(); if (!indev) return;
            lv_point_t pt; lv_indev_get_point(indev, &pt);
            int rel = pt.x - 302; if (rel < 0) rel = 0; if (rel > 170) rel = 170;
            if (s_value_cb) s_value_cb(idx, rel * 100 / 170);
        }, LV_EVENT_RELEASED, &s_slot_idx[i]);
        return;
    }

    if (type == CARD_SENSOR) {
        // Small name on top, big value below.
        w.label = make_label(card, 8, 10, 154, name, &lv_font_montserrat_12, s_theme.sec,
                             LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
        w.value = make_label(card, 8, 32, 154, "--", &lv_font_montserrat_24, s_theme.pri,
                             LV_TEXT_ALIGN_CENTER);
        return;
    }

    // Tappable cards: TOGGLE / LIGHT / ACTION / COVER / LOCK / SELECT / CLIMATE
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    // Visual press feedback so the user can confirm touch is landing.
    lv_obj_add_event_cb(card, [](lv_event_t *e) {
        lv_obj_set_style_bg_opa(lv_event_get_target(e), LV_OPA_70, 0);
    }, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(card, [](lv_event_t *e) {
        lv_obj_set_style_bg_opa(lv_event_get_target(e), LV_OPA_COVER, 0);
        if (s_activate_cb) s_activate_cb(*(int *)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, &s_slot_idx[i]);
    lv_obj_add_event_cb(card, [](lv_event_t *e) {
        lv_obj_set_style_bg_opa(lv_event_get_target(e), LV_OPA_COVER, 0);
    }, LV_EVENT_PRESS_LOST, nullptr);

    w.label = make_label(card, 6, 14, 158, name, &lv_font_montserrat_16, s_theme.sec,
                         LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    w.value = make_label(card, 6, 44, 158, "--", &lv_font_montserrat_14, s_theme.accent,
                         LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
}


// ── Public API ─────────────────────────────────────────────────────────────

void ui_init(const AppConfig &cfg) {
    s_cfg = cfg;
    s_screen_bearing = cfg.screen_bearing;
    lv_disp_t *disp  = lv_disp_get_default();
    lv_disp_set_bg_color(disp, lv_color_hex(0x000000));

    build_page_main(cfg);
}

void ui_set_time(const char *hhmm, const char *date) {
    if (lbl_time) lv_label_set_text(lbl_time, hhmm);
    if (lbl_date) lv_label_set_text(lbl_date, date);
}

void ui_set_temp(float celsius) {
    if (!lbl_temp) return;
    if (isnan(celsius)) { lv_label_set_text(lbl_temp, "--°C"); return; }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", celsius);
    lv_label_set_text(lbl_temp, buf);
}

void ui_set_humidity(float pct) {
    if (!lbl_humidity) return;
    if (isnan(pct)) { lv_label_set_text(lbl_humidity, "--%"); return; }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)roundf(pct));
    lv_label_set_text(lbl_humidity, buf);
}

// f.origin/f.dest arrive as "IATA / City" (see enrichment.cpp). Split into
// a code part and a city part so they can render as stacked labels.
static void split_code_city(const char *combined, char *code, size_t code_sz,
                             char *city, size_t city_sz) {
    const char *sep = strstr(combined, " / ");
    if (sep) {
        size_t code_len = (size_t)(sep - combined);
        if (code_len >= code_sz) code_len = code_sz - 1;
        memcpy(code, combined, code_len);
        code[code_len] = '\0';
        strlcpy(city, sep + 3, city_sz);
    } else {
        strlcpy(code, combined, code_sz);
        city[0] = '\0';
    }
}

void ui_set_flight(const FlightData &f, int screen_bearing_deg) {
    s_screen_bearing = screen_bearing_deg;
    s_bearing_deg    = f.bearing_deg;
    s_distance_km    = f.distance_km;

    if (lbl_callsign) lv_label_set_text(lbl_callsign, f.callsign[0] ? f.callsign : "NO FLIGHT");
    if (lbl_airline)  lv_label_set_text(lbl_airline,  f.airline[0]  ? f.airline  : " ");

    char origin_code[16], origin_city[48];
    char dest_code[16],   dest_city[48];
    split_code_city(f.origin[0] ? f.origin : "---", origin_code, sizeof(origin_code), origin_city, sizeof(origin_city));
    split_code_city(f.dest[0]   ? f.dest   : "---", dest_code,   sizeof(dest_code),   dest_city,   sizeof(dest_city));
    if (lbl_origin_code) lv_label_set_text(lbl_origin_code, origin_code);
    if (lbl_origin_city) lv_label_set_text(lbl_origin_city, origin_city);
    if (lbl_dest_code)   lv_label_set_text(lbl_dest_code,   dest_code);
    if (lbl_dest_city)   lv_label_set_text(lbl_dest_city,   dest_city);

    if (lbl_distance) {
        char buf[20];
        if (f.distance_km > 0.0f)
            snprintf(buf, sizeof(buf), "%.1f km", f.distance_km);
        else
            strlcpy(buf, "--", sizeof(buf));
        lv_label_set_text(lbl_distance, buf);
    }

    // Direction arrow (page 1)
    // Plane image nose points UP (north). bearing=0 → no rotation; rotate
    // clockwise by the bearing (minus the screen's facing direction).
    if (img_direction_arrow) {
        float b = f.bearing_deg;
        int   angle_dd = (int)(fmodf(b - screen_bearing_deg + 720.0f, 360.0f) * 10.0f);
        lv_img_set_pivot(img_direction_arrow, 40, 40);
        lv_img_set_angle(img_direction_arrow, angle_dd);
    }

}

void ui_set_slot_state(int idx, const char *state, float value, bool has_value) {
    if (idx < 0 || idx >= MAX_SLOTS) return;
    SlotWidgets &w = s_slots[idx];
    if (!w.container) return;

    bool on = state && (strcmp(state, "on") == 0 || strcmp(state, "open") == 0
                || strcmp(state, "unlocked") == 0 || strcmp(state, "home") == 0
                || strcmp(state, "playing") == 0);

    switch (w.type) {
    case CARD_SLIDER:
        if (has_value) {
            int v = (int)(value + 0.5f);
            if (v < 0) v = 0; if (v > 100) v = 100;
            if (w.slider) lv_obj_set_width(w.slider, v * 170 / 100);
            if (w.value) { char b[8]; snprintf(b, sizeof(b), "%d%%", v); lv_label_set_text(w.value, b); }
        }
        break;

    case CARD_SENSOR:
        if (w.value) {
            char b[24];
            const SlotConfig *sc = (idx < s_cfg.num_slots) ? &s_cfg.slots[idx] : nullptr;
            if (has_value && sc && sc->unit[0])
                snprintf(b, sizeof(b), "%g%s", value, sc->unit);
            else if (state && state[0])
                strlcpy(b, state, sizeof(b));
            else strlcpy(b, "--", sizeof(b));
            lv_label_set_text(w.value, b);
        }
        break;

    default: {
        // Tappable cards: tint card by on/off; show the raw HA state capitalised.
        lv_obj_set_style_bg_color(w.container, on ? s_theme.sec : s_theme.card, 0);
        if (w.label) lv_obj_set_style_text_color(w.label, on ? s_theme.pri : s_theme.sec, 0);
        if (w.value && state && state[0]) {
            // Capitalise first letter of HA state ("open" → "Open", "on" → "On")
            char buf[32];
            strlcpy(buf, state, sizeof(buf));
            buf[0] = toupper((unsigned char)buf[0]);
            lv_label_set_text(w.value, buf);
        }
        break;
    }
    }
}

void ui_set_wifi(int rssi) {
    if (!wifi_bar[0]) return;
    // Number of filled bars: ≥-60→4, ≥-70→3, ≥-80→2, else→1. 0=unknown→all dim.
    int filled = 0;
    if      (rssi >= -60) filled = 4;
    else if (rssi >= -70) filled = 3;
    else if (rssi >= -80) filled = 2;
    else if (rssi <  -80) filled = 1;

    lv_color_t col = (rssi >= -60) ? lv_color_hex(0x3FB950) :  // green
                     (rssi >= -75) ? lv_color_hex(0xE3B341) :  // yellow
                                     lv_color_hex(0xF85149);   // red
    for (int b = 0; b < 4; b++) {
        bool active = (rssi != 0) && (b < filled);
        lv_obj_set_style_bg_color(wifi_bar[b], active ? col : s_theme.sec, 0);
        lv_obj_set_style_bg_opa(wifi_bar[b],   active ? LV_OPA_70 : LV_OPA_20, 0);
    }
}

void ui_set_slot_callbacks(void (*activate)(int idx), void (*set_value)(int idx, int value)) {
    s_activate_cb = activate;
    s_value_cb    = set_value;
}

void ui_apply_theme(const char *name) {
    if (strcmp(name, "Midnight") == 0) {
        s_theme = {
            lv_color_hex(0x0D1117), lv_color_hex(0x161B22),
            lv_color_hex(0x21262D), lv_color_hex(0xE6EDF3),
            lv_color_hex(0x58A6FF), lv_color_hex(0x79C0FF), false
        };
    } else if (strcmp(name, "Forest") == 0) {
        s_theme = {
            lv_color_hex(0x1A2B1A), lv_color_hex(0x162316),
            lv_color_hex(0x2D4A2D), lv_color_hex(0xE8F0E8),
            lv_color_hex(0x7FB87F), lv_color_hex(0xB8D8B8), false
        };
    } else if (strcmp(name, "iOS") == 0) {
        s_theme = {
            lv_color_hex(0x1C1C2E), lv_color_hex(0x2C2C3E),
            lv_color_hex(0x48485A), lv_color_hex(0xFFFFFF),
            lv_color_hex(0xAEAEB2), lv_color_hex(0x0A84FF), true
        };
    } else {  // Warm Brown (default)
        s_theme = {
            lv_color_hex(0x6B5A48), lv_color_hex(0x5A4A3A),
            lv_color_hex(0x7A6A58), lv_color_hex(0xFEF4EA),
            lv_color_hex(0x9EAB9A), lv_color_hex(0xE8D8C9), false
        };
    }

    if (!page_main) return;

    lv_obj_set_style_bg_color(page_main, s_theme.bg, 0);
    if (s_theme.gradient) {
        lv_obj_set_style_bg_grad_color(page_main, lv_color_hex(0x2D2B55), 0);
        lv_obj_set_style_bg_grad_dir(page_main, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_grad_color(arrow_container, lv_color_hex(0x2D2B55), 0);
        lv_obj_set_style_bg_grad_dir(arrow_container, LV_GRAD_DIR_VER, 0);
    } else {
        lv_obj_set_style_bg_grad_dir(page_main, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_grad_dir(arrow_container, LV_GRAD_DIR_NONE, 0);
    }
    lv_obj_set_style_bg_color(arrow_container, s_theme.bg, 0);

    lv_obj_set_style_text_color(lbl_callsign,     s_theme.accent, 0);
    lv_obj_set_style_text_color(lbl_airline,      s_theme.sec, 0);
    lv_obj_set_style_text_color(lbl_origin_code,  s_theme.pri, 0);
    lv_obj_set_style_text_color(lbl_origin_city,  s_theme.sec, 0);
    lv_obj_set_style_text_color(lbl_dest_code,    s_theme.pri, 0);
    lv_obj_set_style_text_color(lbl_dest_city,    s_theme.sec, 0);
    lv_obj_set_style_text_color(lbl_distance,     s_theme.sec, 0);

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!s_slots[i].container) continue;
        lv_obj_set_style_bg_color(s_slots[i].container, s_theme.card, 0);
        lv_obj_set_style_border_color(s_slots[i].container, s_theme.border, 0);
        if (s_slots[i].label) lv_obj_set_style_text_color(s_slots[i].label, s_theme.sec, 0);
    }
}
