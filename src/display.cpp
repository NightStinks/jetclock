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

static void st7701s_init() {
    pinMode(PIN_SPI_CLK,  OUTPUT);
    pinMode(PIN_SPI_MOSI, OUTPUT);
    pinMode(PIN_LCD_CS,   OUTPUT);
    digitalWrite(PIN_LCD_CS,   HIGH);
    digitalWrite(PIN_SPI_CLK, HIGH);

    delay(10);

    // Enter command page 1 (matches ESPHome init_sequence)
    static const uint8_t cmd_ff[] = {0x77, 0x01, 0x00, 0x00, 0x10};
    lcd_send_cmd(0xFF, cmd_ff, sizeof(cmd_ff));

    // Disable MDT (mirror data timing)
    static const uint8_t cmd_cd[] = {0x00};
    lcd_send_cmd(0xCD, cmd_cd, sizeof(cmd_cd));
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
    // Data pins: R[4:0], G[5:0], B[4:0]
    cfg.data_gpio_nums[0]  = 11; // R0
    cfg.data_gpio_nums[1]  = 12; // R1
    cfg.data_gpio_nums[2]  = 13; // R2
    cfg.data_gpio_nums[3]  = 14; // R3
    cfg.data_gpio_nums[4]  = 0;  // R4
    cfg.data_gpio_nums[5]  = 8;  // G0
    cfg.data_gpio_nums[6]  = 20; // G1
    cfg.data_gpio_nums[7]  = 3;  // G2
    cfg.data_gpio_nums[8]  = 46; // G3
    cfg.data_gpio_nums[9]  = 9;  // G4
    cfg.data_gpio_nums[10] = 10; // G5
    cfg.data_gpio_nums[11] = 4;  // B0
    cfg.data_gpio_nums[12] = 5;  // B1
    cfg.data_gpio_nums[13] = 6;  // B2
    cfg.data_gpio_nums[14] = 7;  // B3
    cfg.data_gpio_nums[15] = 15; // B4
    cfg.disp_gpio_num      = GPIO_NUM_NC;
    cfg.flags.fb_in_psram  = 1;  // frame buffer lives in PSRAM

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
}

// ── LVGL callbacks ────────────────────────────────────────────────────────

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map) {
    int x1 = area->x1, y1 = area->y1;
    int x2 = area->x2, y2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, px_map);
    lv_disp_flush_ready(drv);
}

// ── GT911 touch ───────────────────────────────────────────────────────────

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

    last_x        = (int16_t)((buf[2] << 8) | buf[1]);
    last_y        = (int16_t)((buf[4] << 8) | buf[3]);
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

    // SPI init for ST7701S command interface
    st7701s_init();

    // RGB panel
    panel_init();

    // LVGL
    lv_init();

    draw_buf1 = (lv_color_t *)heap_caps_malloc(
        LCD_W * DRAW_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    draw_buf2 = (lv_color_t *)heap_caps_malloc(
        LCD_W * DRAW_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);

    lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf1, draw_buf2, LCD_W * DRAW_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res    = LCD_W;
    disp_drv.ver_res    = LCD_H;
    disp_drv.flush_cb   = lvgl_flush_cb;
    disp_drv.draw_buf   = &draw_buf_dsc;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = gt911_read;
    lv_indev_drv_register(&indev_drv);

    // Backlight on
    analogWrite(PIN_BL, 255);
}

void display_tick() {
    lv_timer_handler();
}
