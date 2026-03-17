/**
 * @file lv_conf.h
 * Configuration file for LVGL v8.x.x
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888) */
/* TFT_eSPI typically uses 16-bit colors */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color. Useful if the display has an 8-bit interface (e.g. SPI) */
/* Set to 0 because tft.setSwapBytes(true) is already called in display_init() */
#define LV_COLOR_16_SWAP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/

/* 1: use custom malloc/free, 0: use the built-in `lv_mem_alloc()` and `lv_mem_free()` */
/* CRITICAL FIX: Set to 1 to use PSRAM and prevent internal heap exhaustion */
#define LV_MEM_CUSTOM 1

#if LV_MEM_CUSTOM != 0
    /* Wrapper for custom allocation functions */
    #include <esp_heap_caps.h>
    #include <esp32-hal-psram.h>
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    
    /* Map LVGL allocations directly to ESP32 PSRAM */
    #define LV_MEM_CUSTOM_ALLOC   ps_malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC ps_realloc
#else
    /* Size of the memory available for `lv_mem_alloc()` in bytes (>= 2kB) */
    #define LV_MEM_SIZE (48 * 1024U) 
#endif

/*====================
   HAL SETTINGS
 *====================*/

/* Use a custom tick source that tells the elapsed time in milliseconds.
 * It removes the need to manually update the tick with `lv_tick_inc()` */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE <Arduino.h>         /* Header for the system time function */
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())    /* Expression evaluating to current system time in ms */
#endif

/*=======================
   FONT USAGE
 *=======================*/

/* Montserrat fonts with ASCII range and some symbols using bpp = 4
 * https://fonts.google.com/specimen/Montserrat */
#define LV_FONT_MONTSERRAT_14 1  /* Used in ui_module.cpp */
#define LV_FONT_MONTSERRAT_16 1  
#define LV_FONT_MONTSERRAT_20 1

/* Enable the default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*=========================
   EXTRA COMPONENTS
 *=========================*/

/* Show CPU usage and FPS count (Set to 1 for debugging UI lag) */
#define LV_USE_PERF_MONITOR 0

/* Show the used memory and the memory fragmentation */
#define LV_USE_MEM_MONITOR 0

#endif /*LV_CONF_H*/