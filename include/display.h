#pragma once
#include <lvgl.h>

// Initialise the ST7701S RGB panel, GT911 touch, and LVGL.
// Must be called once in setup() before any LVGL widget creation.
void display_init();

// Call every loop() iteration — drives LVGL tick and flush.
void display_tick();
