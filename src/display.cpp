#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

// ── Pin assignments (Guition ESP32-S3-4848S040) ──────────────────────────
#define PIN_BL       38
#define PIN_SPI_CLK  48
#define PIN_SPI_MOSI 47
#define PIN_LCD_CS   39
#define PIN_DE       18
#define PIN_HSYNC    16
#define PIN_VSYNC    17
#define PIN_PCLK     21
#define PIN_I2C_SDA  19
#define PIN_I2C_SCL  45
#define GT911_ADDR   0x5D

#define LCD_W 480
#define LCD_H 480
#define PCLK_HZ (12 * 1000 * 1000)

// LVGL draw buffer — two half-screen buffers in PSRAM for DMA
#define DRAW_BUF_LINES 60
static lv_color_t *draw_buf1 = nullptr;
static lv_color_t *draw_buf2 = nullptr;
static lv_disp_draw_buf_t draw_buf_dsc;
static lv_disp_drv_t      disp_drv;
static lv_indev_drv_t     indev_drv;

static esp_lcd_panel_handle_t panel = nullptr;

// ── ST7701S SPI init (9-bit: 1 DC bit + 8 data bits, bit-banged) ─────────

static void lcd_spi_write_byte(uint8_t dc, uint8_t val) {
    // 9-bit word: DC bit first, then 8 data bits, MSB first
    uint16_t word = ((dc ? 1 : 0) << 8) | val;
    for (int bit = 8; bit >= 0; bit--) {
        digitalWrite(PIN_SPI_CLK, LOW);
        digitalWrite(PIN_SPI_MOSI, (word >> bit) & 1);
        delayMicroseconds(1);
        digitalWrite(PIN_SPI_CLK, HIGH);
        delayMicroseconds(1);
    }
}

static void lcd_send_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
    digitalWrite(PIN_LCD_CS, LOW);
    lcd_spi_write_byte(0, cmd);  // DC=0 → command
    for (size_t i = 0; i < len; i++) {
        lcd_spi_write_byte(1, data[i]);  // DC=1 → data
    }
    digitalWrite(PIN_LCD_CS, HIGH);
}

// Full ST7701S init — this is ESPHome's ST7701S_1_INIT preset (referenced as
// `- 1` in the reference YAML) plus the Guition 4848S040 override (0xCD,0x00).
// Format: cmd, len, data… Without the power/gamma setup AND the trailing
// SLEEP_OUT/DISPLAY_ON, a cold panel never wakes and stays black.
static const uint8_t ST7701S_INIT[] = {
    0x01, 0,                                  // SW reset (code adds 6ms delay)
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,    // CMD2 BK0
    0xC0, 2, 0x3B, 0x00,
    0xC1, 2, 0x0D, 0x02,
    0xC2, 2, 0x31, 0x05,
    0xCD, 1, 0x08,
    0xB0, 16, 0x00,0x11,0x18,0x0E,0x11,0x06,0x07,0x08,0x07,0x22,0x04,0x12,0x0F,0xAA,0x31,0x18,
    0xB1, 16, 0x00,0x11,0x19,0x0E,0x12,0x07,0x08,0x08,0x08,0x22,0x04,0x11,0x11,0xA9,0x32,0x18,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,    // CMD2 BK1
    0xB0, 1, 0x60,
    0xB1, 1, 0x32,
    0xB2, 1, 0x07,
    0xB3, 1, 0x80,
    0xB5, 1, 0x49,
    0xB7, 1, 0x85,
    0xB8, 1, 0x21,
    0xC1, 1, 0x78,
    0xC2, 1, 0x78,
    0xE0, 3, 0x00, 0x1B, 0x02,
    0xE1, 11, 0x08,0xA0,0x00,0x00,0x07,0xA0,0x00,0x00,0x00,0x44,0x44,
    0xE2, 12, 0x11,0x11,0x44,0x44,0xED,0xA0,0x00,0x00,0xEC,0xA0,0x00,0x00,
    0xE3, 4, 0x00, 0x00, 0x11, 0x11,
    0xE4, 2, 0x44, 0x44,
    0xE5, 16, 0x0A,0xE9,0xD8,0xA0,0x0C,0xEB,0xD8,0xA0,0x0E,0xED,0xD8,0xA0,0x10,0xEF,0xD8,0xA0,
    0xE6, 4, 0x00, 0x00, 0x11, 0x11,
    0xE7, 2, 0x44, 0x44,
    0xE8, 16, 0x09,0xE8,0xD8,0xA0,0x0B,0xEA,0xD8,0xA0,0x0D,0xEC,0xD8,0xA0,0x0F,0xEE,0xD8,0xA0,
    0xEB, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,
    0xEC, 2, 0x3C, 0x00,
    0xED, 16, 0xAB,0x89,0x76,0x54,0x02,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x20,0x45,0x67,0x98,0xBA,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,    // CMD2 BK3
    0xE5, 1, 0xE4,
    0x3A, 1, 0x60,
};

static void st7701s_init() {
    pinMode(PIN_SPI_CLK,  OUTPUT);
    pinMode(PIN_SPI_MOSI, OUTPUT);
    pinMode(PIN_LCD_CS,   OUTPUT);
    digitalWrite(PIN_LCD_CS,   HIGH);
    digitalWrite(PIN_SPI_CLK, HIGH);

    delay(10);

    // Run the init sequence
    for (size_t i = 0; i < sizeof(ST7701S_INIT);) {
        uint8_t cmd = ST7701S_INIT[i++];
        uint8_t len = ST7701S_INIT[i++];
        lcd_send_cmd(cmd, &ST7701S_INIT[i], len);
        i += len;
        if (cmd == 0x01) delay(6);  // SW reset settle
    }

    // Epilogue (ESPHome writes these after the sequence):
    // re-select BK0, set scan/mirror direction, MADCTL, inversion off,
    // then SLEEP_OUT + DISPLAY_ON — the commands that actually light the panel.
    static const uint8_t bk0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
    lcd_send_cmd(0xFF, bk0, sizeof(bk0));

    // Guition 4848S040 override — MUST be on page BK0 (after the re-select
    // above), exactly as the reference YAML does it. Sets the 16-bit RGB
    // pixel format; the preset leaves BK0 0xCD at 0x08 which mismaps colours.
    static const uint8_t cd0 = 0x00;
    lcd_send_cmd(0xCD, &cd0, 1);

    static const uint8_t sdir = 0x00;   // SDIR: no mirror_x
    lcd_send_cmd(0xC7, &sdir, 1);

    static const uint8_t madctl = 0x00; // MADCTL: RGB order, no mirror_y
    lcd_send_cmd(0x36, &madctl, 1);

    lcd_send_cmd(0x20, nullptr, 0);     // INVERT OFF
    delay(120);
    lcd_send_cmd(0x11, nullptr, 0);     // SLEEP OUT
    lcd_send_cmd(0x29, nullptr, 0);     // DISPLAY ON
    delay(10);
}

// ── RGB panel ─────────────────────────────────────────────────────────────

static void panel_init() {
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src                    = LCD_CLK_SRC_XTAL;
    cfg.timings.pclk_hz            = PCLK_HZ;
    cfg.timings.h_res              = LCD_W;
    cfg.timings.v_res              = LCD_H;
    cfg.timings.hsync_pulse_width  = 10;
    cfg.timings.hsync_back_porch   = 10;
    cfg.timings.hsync_front_porch  = 20;
    cfg.timings.vsync_pulse_width  = 10;
    cfg.timings.vsync_back_porch   = 10;
    cfg.timings.vsync_front_porch  = 10;
    cfg.timings.flags.pclk_active_neg = 0;
    cfg.data_width                 = 16;
    cfg.sram_trans_align           = 8;
    cfg.psram_trans_align          = 64;
    cfg.hsync_gpio_num             = PIN_HSYNC;
    cfg.vsync_gpio_num             = PIN_VSYNC;
    cfg.de_gpio_num                = PIN_DE;
    cfg.pclk_gpio_num              = PIN_PCLK;
    // Pixel word bit layout: B[4:0]=bits0-4, G[5:0]=bits5-10, R[4:0]=bits11-15
    // data_gpio_nums[n] maps bit n → the GPIO that the display uses for that channel.
    // Pin assignments from the working ESPHome YAML (color_order: RGB).
    cfg.data_gpio_nums[0]  = 4;  // B0
    cfg.data_gpio_nums[1]  = 5;  // B1
    cfg.data_gpio_nums[2]  = 6;  // B2
    cfg.data_gpio_nums[3]  = 7;  // B3
    cfg.data_gpio_nums[4]  = 15; // B4
    cfg.data_gpio_nums[5]  = 8;  // G0
    cfg.data_gpio_nums[6]  = 20; // G1
    cfg.data_gpio_nums[7]  = 3;  // G2
    cfg.data_gpio_nums[8]  = 46; // G3
    cfg.data_gpio_nums[9]  = 9;  // G4
    cfg.data_gpio_nums[10] = 10; // G5
    cfg.data_gpio_nums[11] = 11; // R0
    cfg.data_gpio_nums[12] = 12; // R1
    cfg.data_gpio_nums[13] = 13; // R2
    cfg.data_gpio_nums[14] = 14; // R3
    cfg.data_gpio_nums[15] = 0;  // R4
    cfg.disp_gpio_num      = GPIO_NUM_NC;
    cfg.flags.fb_in_psram  = 1;  // frame buffer lives in PSRAM

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
}

// ── LVGL callbacks ────────────────────────────────────────────────────────

// Device is mounted upside down (fixed by antenna/WiFi placement), so the
// panel image needs a 180° flip. Reversing a rectangular chunk end-to-end
// (swap px_map[i] with px_map[count-1-i]) is exactly equivalent to mirroring
// both axes of that chunk — for row-major data, index i = row*w+col maps to
// count-1-i = (h-1-row)*w + (w-1-col) — so no extra scratch buffer is needed.
// The chunk is then written to the mirrored destination rectangle.
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map) {
    int x1 = area->x1, y1 = area->y1;
    int x2 = area->x2, y2 = area->y2;

    int count = (x2 - x1 + 1) * (y2 - y1 + 1);
    for (int i = 0, j = count - 1; i < j; i++, j--) {
        lv_color_t tmp = px_map[i];
        px_map[i] = px_map[j];
        px_map[j] = tmp;
    }

    int rx1 = LCD_W - 1 - x2;
    int rx2 = LCD_W - 1 - x1;
    int ry1 = LCD_H - 1 - y2;
    int ry2 = LCD_H - 1 - y1;

    esp_lcd_panel_draw_bitmap(panel, rx1, ry1, rx2 + 1, ry2 + 1, px_map);
    lv_disp_flush_ready(drv);
}

// ── GT911 touch ───────────────────────────────────────────────────────────

// GT911 native panel resolution — read from config registers at init.
// Used to scale raw coords to screen coords regardless of chip config.
static int16_t s_gt911_x_max = LCD_W - 1;
static int16_t s_gt911_y_max = LCD_H - 1;

static void gt911_read_config() {
    // Config registers 0x8047-0x804B
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x80); Wire.write(0x47);
    Wire.endTransmission();
    Wire.requestFrom((int)GT911_ADDR, 5);
    if (Wire.available() >= 5) {
        Wire.read();  // config version, skip
        uint8_t xl = Wire.read(), xh = Wire.read();
        uint8_t yl = Wire.read(), yh = Wire.read();
        int16_t xm = (int16_t)((xh << 8) | xl);
        int16_t ym = (int16_t)((yh << 8) | yl);
        if (xm > 0) s_gt911_x_max = xm;
        if (ym > 0) s_gt911_y_max = ym;
    }
    Serial.printf("[gt911] configured X_max=%d Y_max=%d\n", s_gt911_x_max, s_gt911_y_max);
}

static void gt911_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t last_x = 0, last_y = 0;

    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x81);  // Status register MSB
    Wire.write(0x4E);  // Status register LSB
    Wire.endTransmission();

    Wire.requestFrom((int)GT911_ADDR, 1);
    if (!Wire.available()) {
        data->state   = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    uint8_t status      = Wire.read();
    uint8_t touch_count = status & 0x0F;

#ifdef JETCLOCK_TOUCH_DEBUG
    {
        static uint32_t last = 0;
        if (status != 0 || millis() - last > 1000) {
            last = millis();
            Serial.printf("[gt911] status=0x%02X count=%d\n", status, touch_count);
        }
    }
#endif

    if (!(status & 0x80) || touch_count == 0) {
        Wire.beginTransmission(GT911_ADDR);
        Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
        Wire.endTransmission();
        data->state   = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    // Read first touch point (8 bytes starting at 0x8150)
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x81); Wire.write(0x50);
    Wire.endTransmission();
    Wire.requestFrom((int)GT911_ADDR, 8);

    uint8_t buf[8] = {};
    for (int i = 0; i < 8 && Wire.available(); i++) buf[i] = Wire.read();

    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();

    int16_t raw_x = (int16_t)((buf[1] << 8) | buf[2]);
    int16_t raw_y = (int16_t)((buf[3] << 8) | buf[4]);
    // Scale from GT911 native resolution to screen resolution, then swap axes.
    // Flip both to match the 180°-rotated panel image (device is mounted
    // upside down) so taps land on the visually rotated UI elements.
    last_x = (LCD_W - 1) - (int16_t)((int32_t)raw_y * (LCD_W - 1) / s_gt911_y_max);
    last_y = (LCD_H - 1) - (int16_t)((int32_t)raw_x * (LCD_H - 1) / s_gt911_x_max);
#ifdef JETCLOCK_TOUCH_DEBUG
    {
        static uint32_t last = 0;
        if (millis() - last > 150) { last = millis(); Serial.printf("[gt911] raw x=%d y=%d\n", last_x, last_y); }
    }
#endif
    data->point.x = last_x;
    data->point.y = last_y;
    data->state   = LV_INDEV_STATE_PR;
}

// ── Public API ─────────────────────────────────────────────────────────────

void display_init() {
    // Backlight off during init
    pinMode(PIN_BL, OUTPUT);
    analogWrite(PIN_BL, 0);

    // Touch I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    delay(100);  // let GT911 settle after power-on
    gt911_read_config();

#ifdef JETCLOCK_TOUCH_DEBUG
    Serial.println("[i2c] scanning bus...");
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf("[i2c] device found at 0x%02X\n", a);
    }
    Serial.println("[i2c] scan done");
#endif

    // SPI init for ST7701S command interface
    st7701s_init();

    // RGB panel
    panel_init();

    // Clear the panel frame buffer to black so there's no PSRAM garbage on screen
    {
        static lv_color_t black_row[LCD_W] = {};
        for (int y = 0; y < LCD_H; y++) {
            esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_W, y + 1, black_row);
        }
    }

    // LVGL
    lv_init();

    // LVGL draw buffers: MALLOC_CAP_SPIRAM only (no MALLOC_CAP_DMA — that
    // flag is for internal SRAM DMA and is incompatible with PSRAM on S3).
    // Fall back to internal SRAM if PSRAM allocation fails.
    size_t buf_bytes = LCD_W * DRAW_BUF_LINES * sizeof(lv_color_t);
    draw_buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    draw_buf2 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!draw_buf1 || !draw_buf2) {
        Serial.println("[display] PSRAM alloc failed, falling back to SRAM");
        free(draw_buf1); free(draw_buf2);
        draw_buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL);
        draw_buf2 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL);
    }
    if (!draw_buf1) { Serial.println("[display] FATAL: no memory for draw buffer"); return; }

    lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf1, draw_buf2, LCD_W * DRAW_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_W;
    disp_drv.ver_res      = LCD_H;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &draw_buf_dsc;
    disp_drv.full_refresh = 1;  // always redraw full screen to avoid partial-flush artefacts
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type         = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb      = gt911_read;
    indev_drv.scroll_limit = 50;  // GT911 can drift >10px; raise limit so taps aren't cancelled
    lv_indev_drv_register(&indev_drv);

    // Backlight on
    analogWrite(PIN_BL, 255);
}

void display_set_brightness(int pct) {
    if (pct <= 0) { analogWrite(PIN_BL, 0); return; }
    if (pct > 100) pct = 100;
    analogWrite(PIN_BL, pct * 255 / 100);
}

void display_tick() {
    lv_timer_handler();
}

void display_fill_test(uint16_t color) {
    if (!panel) return;
    static uint16_t row[LCD_W];
    for (int x = 0; x < LCD_W; x++) row[x] = color;
    for (int y = 0; y < LCD_H; y++) {
        esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_W, y + 1, row);
    }
}
