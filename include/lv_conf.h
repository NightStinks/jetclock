#if 1  /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888) */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color — mandatory for ST7701S + ESP32-S3 */
#define LV_COLOR_16_SWAP 1

/* Enable use of LV_COLOR_CHROMA_KEY for transparency */
#define LV_COLOR_SCREEN_TRANSP 0

/* Images pixels with this color will not be drawn (with chroma keying) */
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x000000)  /* black = transparent in plane_img */

/* ========================
   Memory settings
   ======================== */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (512U * 1024U)  /* 512KB from internal SRAM */
#define LV_MEM_ADR 0
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC malloc
#define LV_MEM_POOL_FREE free

/* ========================
   HAL settings
   ======================== */
#define LV_DISP_DEF_REFR_PERIOD 16   /* ms — 60Hz */
#define LV_INDEV_DEF_READ_PERIOD 30  /* ms */

/* Default display resolution, overridden at runtime */
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 480

/* Scale for all LV_DPx values */
#define LV_DPI_DEF 130

/* ========================
   Draw settings
   ======================== */
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0
#define LV_DISP_ROT_MAX_BUF (10*1024)
#define LV_COLOR_MIX_ROUND_OFS 0

/* ========================
   GPU (none)
   ======================== */
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/* ========================
   Logging
   ======================== */
#define LV_USE_LOG 0
#define LV_LOG_PRINTF 0
#define LV_LOG_TRACE_MEM 0
#define LV_LOG_TRACE_TIMER 0
#define LV_LOG_TRACE_INDEV 0
#define LV_LOG_TRACE_DISP_REFR 0
#define LV_LOG_TRACE_EVENT 0
#define LV_LOG_TRACE_OBJ_CREATE 0
#define LV_LOG_TRACE_LAYOUT 0
#define LV_LOG_TRACE_ANIM 0

/* ========================
   Asserts
   ======================== */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/* ========================
   Debug
   ======================== */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

/* ========================
   Others
   ======================== */
#define LV_USE_SPRINTF_CUSTOM 0
#define LV_SPRINTF_CUSTOM_INCLUDE <stdio.h>
#define LV_SPRINTF_CUSTOM_FUNC sprintf
#define LV_SPRINTF_USE_FLOAT 0

#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0
#define LV_BIN_DECODER_RAM_LOAD 0
#define LV_USE_LARGE_COORD 0

/* ========================
   Compiler settings
   ======================== */
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 16
#define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(LV_ATTRIBUTE_MEM_ALIGN_SIZE)))
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR
#define LV_ATTRIBUTE_DMA
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_USE_LARGE_COORD 0

/* ========================
   Fonts — all sizes used in the UI
   ======================== */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_MONTSERRAT_SUBPX 0
#define LV_FONT_MONTSERRAT_ARABIC 0

#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_SUBPX 0
#define LV_FONT_SUBPX_BGR 0

/* ========================
   Text settings
   ======================== */
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " _-,"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* ========================
   Widgets
   ======================== */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 0
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0

/* ========================
   Extra Widgets
   ======================== */
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/* ========================
   Themes
   ======================== */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/* ========================
   Layouts
   ======================== */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* ========================
   3rd party libraries
   ======================== */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

/* ========================
   Others
   ======================== */
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_MSG 0
#define LV_USE_IME_PINYIN 0

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
