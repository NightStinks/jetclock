#pragma once
#include <lvgl.h>

// Initialise the ST7701S RGB panel, GT911 touch, and LVGL.
// Must be called once in setup() before any LVGL widget creation.
void display_init();

// Call every loop() iteration — drives LVGL tick and flush.
void display_tick();

// Set backlight brightness. pct=0 turns the display off; 1–100 sets level.
void display_set_brightness(int pct);

// Diagnostic: fill the whole panel with a solid colour, talking directly to
// the RGB panel (no LVGL). colour is a raw RGB565 value.
void display_fill_test(uint16_t color);
