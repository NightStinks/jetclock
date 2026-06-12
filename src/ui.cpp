#include "ui.h"

#include <lvgl.h>
#include <math.h>

// Plane image — src/plane_img.c (80x80, nose pointing RIGHT)
// Transparent pixels are 0x0000 (black), rendered transparent via chroma key
LV_IMG_DECLARE(plane_img_80);

// ── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t *page_main   = nullptr;
static lv_obj_t *page_radar  = nullptr;

// Status bar
static lv_obj_t *lbl_time;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_humidity;

// Flight info
static lv_obj_t *lbl_callsign;
static lv_obj_t *lbl_airline;
static lv_obj_t *lbl_origin;
static lv_obj_t *lbl_dest;
static lv_obj_t *lbl_distance;
static lv_obj_t *img_direction_arrow;
static lv_obj_t *arrow_container;

// Smart home buttons (5 slots)
static lv_obj_t *btn_objs[MAX_BUTTONS];
static lv_obj_t *btn_labels[MAX_BUTTONS];

// Radar page
static lv_obj_t *p2_lbl_callsign;
static lv_obj_t *p2_lbl_origin;
static lv_obj_t *p2_lbl_dest;
static lv_obj_t *p2_img_plane;

static void (*s_toggle_cb)(int) = nullptr;

// Cached config
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
    lbl_temp = make_label(status_bar, 318, 16, 0, "--°C",
                          &lv_font_montserrat_26, s_theme.pri);
    lbl_humidity = make_label(status_bar, 406, 16, 0, "--%",
                              &lv_font_montserrat_26, s_theme.sec);

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
    lbl_origin   = make_label(page_main, 0, 186, 292, "---",
                              &lv_font_montserrat_24, s_theme.pri,
                              LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    lbl_dest     = make_label(page_main, 0, 220, 292, "---",
                              &lv_font_montserrat_24, s_theme.pri,
                              LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    lbl_distance = make_label(page_main, 0, 256, 292, "--",
                              &lv_font_montserrat_18, s_theme.sec,
                              LV_TEXT_ALIGN_CENTER);

    // Direction arrow container — tap → radar page
    arrow_container = make_rect(page_main, 0, 284, 292, 196, s_theme.bg);
    lv_obj_add_flag(arrow_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arrow_container, [](lv_event_t *e) {
        lv_obj_move_foreground(page_radar);
    }, LV_EVENT_CLICKED, nullptr);

    img_direction_arrow = lv_img_create(arrow_container);
    lv_img_set_src(img_direction_arrow, &plane_img_80);
    lv_obj_set_pos(img_direction_arrow, 106, 58);
    lv_img_set_pivot(img_direction_arrow, 40, 40);
    lv_img_set_antialias(img_direction_arrow, true);

    // ── Right: Smart home buttons ────────────────────────────────────────
    static const int btn_y[MAX_BUTTONS] = {76, 158, 240, 322, 404};

    for (int i = 0; i < MAX_BUTTONS; i++) {
        const char *lbl_text = (i < cfg.num_buttons && cfg.buttons[i].label[0])
                               ? cfg.buttons[i].label : "";

        lv_obj_t *btn = lv_obj_create(page_main);
        lv_obj_set_pos(btn, 302, btn_y[i]);
        lv_obj_set_size(btn, 170, 76);
        lv_obj_set_style_bg_color(btn, s_theme.card, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, s_theme.border, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        // Only clickable if a slot is configured
        if (i < cfg.num_buttons && cfg.buttons[i].entity_id[0]) {
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            int *idx_ptr = new int(i);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                if (s_toggle_cb) s_toggle_cb(*(int *)lv_event_get_user_data(e));
            }, LV_EVENT_CLICKED, idx_ptr);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(lbl, lbl_text);
        lv_obj_set_width(lbl, 162);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, s_theme.sec, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

        btn_objs[i]   = btn;
        btn_labels[i] = lbl;
    }
}

// ── Page 2 — Radar ────────────────────────────────────────────────────────

static void build_page_radar() {
    static const lv_color_t bg_dark  = LV_COLOR_MAKE(0x02, 0x0A, 0x06);
    static const lv_color_t green    = LV_COLOR_MAKE(0x00, 0xFF, 0x88);
    static const lv_color_t ring1    = LV_COLOR_MAKE(0x0A, 0x30, 0x18);
    static const lv_color_t ring2    = LV_COLOR_MAKE(0x09, 0x1F, 0x10);
    static const lv_color_t ring3    = LV_COLOR_MAKE(0x07, 0x18, 0x0C);
    static const lv_color_t ring4    = LV_COLOR_MAKE(0x05, 0x12, 0x08);
    static const lv_color_t crosshair= LV_COLOR_MAKE(0x0A, 0x28, 0x14);
    static const lv_color_t txt_dim  = LV_COLOR_MAKE(0x1A, 0x4A, 0x2A);
    static const lv_color_t txt_mid  = LV_COLOR_MAKE(0x1A, 0x6A, 0x3A);

    page_radar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page_radar, 480, 480);
    lv_obj_set_pos(page_radar, 0, 0);
    lv_obj_set_style_bg_color(page_radar, bg_dark, 0);
    lv_obj_set_style_bg_opa(page_radar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_radar, 0, 0);
    lv_obj_set_style_pad_all(page_radar, 0, 0);
    lv_obj_clear_flag(page_radar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page_radar, LV_OBJ_FLAG_CLICKABLE);

    // Tap anywhere → back to main
    lv_obj_add_event_cb(page_radar, [](lv_event_t *e) {
        lv_obj_move_foreground(page_main);
    }, LV_EVENT_CLICKED, nullptr);

    // Header labels
    make_label(page_radar, 0, 10, 480, "NEAREST FLIGHT",
               &lv_font_montserrat_12, txt_dim, LV_TEXT_ALIGN_CENTER);
    p2_lbl_callsign = make_label(page_radar, 0, 26, 480, "--",
                                 &lv_font_montserrat_40, green, LV_TEXT_ALIGN_CENTER);
    p2_lbl_origin   = make_label(page_radar, 8, 72, 220, "---",
                                 &lv_font_montserrat_16, txt_mid,
                                 LV_TEXT_ALIGN_RIGHT, LV_LABEL_LONG_DOT);
    make_label(page_radar, 232, 72, 0, ">", &lv_font_montserrat_16, txt_dim);
    p2_lbl_dest     = make_label(page_radar, 242, 72, 230, "---",
                                 &lv_font_montserrat_16, txt_mid,
                                 LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_DOT);

    // Radar rings (centre 240, 310)
    auto make_ring = [&](int x, int y, int w, lv_color_t border_col) {
        lv_obj_t *obj = make_rect(page_radar, x, y, w, w, bg_dark, w / 2);
        lv_obj_set_style_border_color(obj, border_col, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    };
    make_ring(30,  100, 420, ring1);
    make_ring(82,  152, 316, ring2);
    make_ring(135, 205, 210, ring3);
    make_ring(187, 257, 106, ring4);

    // Crosshair lines
    make_rect(page_radar, 30, 309, 420, 1, crosshair);
    make_rect(page_radar, 239, 100, 1, 420, crosshair);

    // Ring distance labels
    make_label(page_radar, 244, 103, 0, "50km", &lv_font_montserrat_12, ring1);
    make_label(page_radar, 244, 156, 0, "37km", &lv_font_montserrat_12, ring2);
    make_label(page_radar, 244, 208, 0, "25km", &lv_font_montserrat_12, ring3);
    make_label(page_radar, 244, 260, 0, "12km", &lv_font_montserrat_12, ring4);

    // Compass points
    static const lv_color_t compass = LV_COLOR_MAKE(0x1A, 0x4A, 0x2A);
    static const lv_color_t compass2= LV_COLOR_MAKE(0x0D, 0x30, 0x18);
    make_label(page_radar, 205, 103, 0, "NE", &lv_font_montserrat_14, compass);
    make_label(page_radar, 392, 296, 0, "SE", &lv_font_montserrat_14, compass2);
    make_label(page_radar, 36,  296, 0, "NW", &lv_font_montserrat_14, compass2);

    // Home location dot (centre 240, 310)
    lv_obj_t *home_dot = make_rect(page_radar, 232, 302, 16, 16, green, 8);
    lv_obj_clear_flag(home_dot, LV_OBJ_FLAG_CLICKABLE);

    // Aircraft icon
    // Radar plane: same 80x80 image scaled to 28px via zoom (89/256 ≈ 28/80)
    p2_img_plane = lv_img_create(page_radar);
    lv_img_set_src(p2_img_plane, &plane_img_80);
    lv_img_set_zoom(p2_img_plane, 89);
    lv_obj_set_size(p2_img_plane, 28, 28);
    lv_obj_set_pos(p2_img_plane, 226, 296);
    lv_img_set_pivot(p2_img_plane, 14, 14);
    lv_img_set_antialias(p2_img_plane, true);
    lv_obj_clear_flag(p2_img_plane, LV_OBJ_FLAG_CLICKABLE);

    // Footer
    make_label(page_radar, 0, 460, 480, "TAP TO RETURN",
               &lv_font_montserrat_14, crosshair, LV_TEXT_ALIGN_CENTER);
}

// ── Public API ─────────────────────────────────────────────────────────────

void ui_init(const AppConfig &cfg) {
    s_screen_bearing = cfg.screen_bearing;
    lv_disp_t *disp  = lv_disp_get_default();
    lv_disp_set_bg_color(disp, lv_color_hex(0x000000));

    build_page_main(cfg);
    build_page_radar();

    // Main page on top
    lv_obj_move_foreground(page_main);
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

void ui_set_flight(const FlightData &f, int screen_bearing_deg) {
    s_screen_bearing = screen_bearing_deg;
    s_bearing_deg    = f.bearing_deg;
    s_distance_km    = f.distance_km;

    if (lbl_callsign) lv_label_set_text(lbl_callsign, f.callsign[0] ? f.callsign : "NO FLIGHT");
    if (lbl_airline)  lv_label_set_text(lbl_airline,  f.airline[0]  ? f.airline  : " ");
    if (lbl_origin)   lv_label_set_text(lbl_origin,   f.origin[0]   ? f.origin   : "---");
    if (lbl_dest)     lv_label_set_text(lbl_dest,     f.dest[0]     ? f.dest     : "---");

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

    // Radar page — callsign, origin, dest
    if (p2_lbl_callsign) lv_label_set_text(p2_lbl_callsign, f.callsign[0] ? f.callsign : "--");
    if (p2_lbl_origin)   lv_label_set_text(p2_lbl_origin, f.origin[0] ? f.origin : "---");
    if (p2_lbl_dest)     lv_label_set_text(p2_lbl_dest, f.dest[0] ? f.dest : "---");

    // Radar plane position
    if (p2_img_plane && f.distance_km > 0.0f) {
        float r = 195.0f * (f.distance_km / 50.0f);
        if (r > 192.0f) r = 192.0f;
        float angle_rad = (f.bearing_deg - screen_bearing_deg) * (float)M_PI / 180.0f;
        int px = (int)(240.0f + r * sinf(angle_rad)) - 14;
        int py = (int)(310.0f - r * cosf(angle_rad)) - 14;
        lv_obj_set_pos(p2_img_plane, px, py);

        // Track heading: nose points UP, so rotate clockwise by the track
        // angle, adjusted for the screen's facing direction.
        int track_dd = (int)(fmodf(f.track_deg - screen_bearing_deg + 360.0f, 360.0f) * 10.0f);
        lv_img_set_pivot(p2_img_plane, 14, 14);
        lv_img_set_angle(p2_img_plane, track_dd);
    }
}

void ui_set_button_state(int idx, bool on) {
    if (idx < 0 || idx >= MAX_BUTTONS || !btn_objs[idx]) return;
    lv_color_t bg_col  = on ? s_theme.sec  : s_theme.card;
    lv_color_t txt_col = on ? s_theme.pri  : s_theme.sec;
    lv_obj_set_style_bg_color(btn_objs[idx],   bg_col,  0);
    lv_obj_set_style_text_color(btn_labels[idx], txt_col, 0);
}

void ui_set_toggle_callback(void (*cb)(int button_idx)) {
    s_toggle_cb = cb;
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

    lv_obj_set_style_text_color(lbl_callsign, s_theme.accent, 0);
    lv_obj_set_style_text_color(lbl_airline,  s_theme.sec, 0);
    lv_obj_set_style_text_color(lbl_origin,   s_theme.pri, 0);
    lv_obj_set_style_text_color(lbl_dest,     s_theme.pri, 0);
    lv_obj_set_style_text_color(lbl_distance, s_theme.sec, 0);

    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (!btn_objs[i]) continue;
        lv_obj_set_style_bg_color(btn_objs[i], s_theme.card, 0);
        lv_obj_set_style_border_color(btn_objs[i], s_theme.border, 0);
        lv_obj_set_style_text_color(btn_labels[i], s_theme.sec, 0);
    }
}
