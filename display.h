#pragma once

#include <lvgl.h>
#include "state.h"

void display_init();
void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cm);
void lvgl_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
TouchPoint get_touch();
void ui_update_tick(lv_timer_t *timer);

// Make the app context available to the UI update tick
void set_ui_update_context(AppContext* context);
