#include "display.h"
#include "constants.h"
#include <TFT_eSPI.h>
#include <Wire.h>

// Forward declare from ui_module
extern lv_obj_t *lbl_batt, *lbl_batt_pct, *lbl_sd, *lbl_wifi, *lbl_msg;
extern lv_obj_t *ta_lora_log;
extern lv_obj_t* tabview;
extern lv_obj_t *lbl_lora_stats;
extern lv_obj_t *lora_stats_panel;
extern lv_obj_t *lora_nodedb_panel;
extern lv_obj_t *nodedb_list;
extern lv_obj_t *lora_chat_panel;
extern lv_obj_t *ta_lora_chat;
extern lv_obj_t *lora_log_panel;

// Global objects for this module
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t *lvgl_buf1 = nullptr;
static lv_color_t *lvgl_buf2 = nullptr;

static AppContext* ui_context = nullptr;

void set_ui_update_context(AppContext* context) {
    ui_context = context;
}

TouchPoint get_touch() {
    TouchPoint pt = {0, 0, false};
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission() != 0) return pt;
    Wire.requestFrom(TOUCH_ADDR, (uint8_t)5);
    if (Wire.available() == 5) {
        uint8_t t = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
        if (t > 0 && t < 3) {
            pt.pressed = true;
            pt.x = constrain(((uint16_t)(xh & 0x0F) << 8) | xl, 0, SCREEN_W);
            pt.y = constrain(((uint16_t)(yh & 0x0F) << 8) | yl, 0, SCREEN_H);
        }
    }
    return pt;
}

void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cm) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)cm, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

void lvgl_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t lx = 0, ly = 0;
    TouchPoint pt = get_touch();
    if (pt.pressed) {
        lx = pt.x;
        ly = pt.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = lx;
    data->point.y = ly;
}

void display_init() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(100);
    digitalWrite(TP_RST, HIGH);
    delay(50);

    Wire.begin(I2C_SDA, I2C_SCL, 100000);
    Wire.setTimeOut(20); // Prevent hardware I2C bus noise from permanently hanging the UI core

    tft.begin();
    tft.setRotation(0);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lvgl_buf1 = (lv_color_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(lv_color_t));
    lvgl_buf2 = (lv_color_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(lv_color_t));
    if (!lvgl_buf1 || !lvgl_buf2) {
        Serial.println("FATAL: PSRAM alloc failed");
        while (1) delay(1000);
    }

    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, lvgl_buf2, SCREEN_W * SCREEN_H);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = lvgl_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read;
    lv_indev_drv_register(&indev_drv);
}

static StatusSnapshot read_status_snapshot() {
  StatusSnapshot s = {};
  if (ui_context && ui_context->status.mutex && xSemaphoreTake(ui_context->status.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    s = ui_context->status.snap;
    xSemaphoreGive(ui_context->status.mutex);
  }
  return s;
}


void ui_update_tick(lv_timer_t *timer) {
    if (!ui_context) return;
    // Guard against timer firing before ui_build() has finished assigning all objects
    if (!lbl_batt || !lbl_batt_pct || !lbl_sd || !lbl_wifi || !lbl_msg || !tabview ||
        !ta_lora_log || !lora_log_panel || !ta_lora_chat || !lora_chat_panel ||
        !lbl_lora_stats || !lora_stats_panel || !nodedb_list || !lora_nodedb_panel) return;

    StatusSnapshot ss = read_status_snapshot();

    // Battery
    uint32_t batt_col = ss.batteryPct < 20 ? 0xFF4444 : ss.batteryPct < 50 ? 0xFFFF00 : 0xFFFFFF;
    if (ss.isCharging) {
        lv_label_set_text(lbl_batt, LV_SYMBOL_CHARGE);
        batt_col = 0x00FF88; // Bright green when charging
    } else {
        if (ss.batteryPct > 80) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_FULL);
        else if (ss.batteryPct > 60) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_3);
        else if (ss.batteryPct > 35) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_2);
        else if (ss.batteryPct > 15) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_1);
        else lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_EMPTY);
    }
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(batt_col), 0);
    char bpbuf[8];
    snprintf(bpbuf, sizeof(bpbuf), "%d%%", ss.batteryPct);
    lv_label_set_text(lbl_batt_pct, bpbuf);
    lv_obj_set_style_text_color(lbl_batt_pct, lv_color_hex(batt_col), 0);

    // SD icon
    lv_obj_set_style_text_color(lbl_sd, lv_color_hex(ss.sdMounted ? 0x00FF88 : 0xFF4444), 0);

    // Message icon (blinks when unread chat)
    bool unread_chat_status = false;
    if (ui_context->lora.mutex && xSemaphoreTake(ui_context->lora.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        unread_chat_status = ui_context->lora.unread_chat;

        // Update LoRa log in terminal
        if (ta_lora_log && lora_log_panel && !lv_obj_has_flag(lora_log_panel, LV_OBJ_FLAG_HIDDEN)) {
            static uint32_t last_log_draw = 0;
            if (ui_context->lora.log_updated && millis() - last_log_draw > 250) {
                ui_context->lora.log_data[sizeof(ui_context->lora.log_data) - 1] = '\0';
                if (strlen(ui_context->lora.log_data) > 1900) { strcpy(ui_context->lora.log_data, "--- Buffer Cleared ---\n"); }
                lv_textarea_set_text(ta_lora_log, ui_context->lora.log_data);
                lv_obj_scroll_to_y(ta_lora_log, LV_COORD_MAX, LV_ANIM_OFF);
                ui_context->lora.log_updated = false;
                last_log_draw = millis();
            }
        }

        // Update LoRa Chat Panel
        if (ta_lora_chat && lora_chat_panel && !lv_obj_has_flag(lora_chat_panel, LV_OBJ_FLAG_HIDDEN)) {
            static uint32_t last_chat_draw = 0;
            if (ui_context->lora.chat_updated && millis() - last_chat_draw > 250) {
                ui_context->lora.chat_data[sizeof(ui_context->lora.chat_data) - 1] = '\0';
                if (strlen(ui_context->lora.chat_data) > 1900) { strcpy(ui_context->lora.chat_data, "--- Buffer Cleared ---\n"); }
                lv_textarea_set_text(ta_lora_chat, ui_context->lora.chat_data);
                lv_obj_scroll_to_y(ta_lora_chat, LV_COORD_MAX, LV_ANIM_OFF);
                ui_context->lora.chat_updated = false;
                last_chat_draw = millis();
            }
        }

        // Update LoRa Stats Panel
        if (lbl_lora_stats && lora_stats_panel && !lv_obj_has_flag(lora_stats_panel, LV_OBJ_FLAG_HIDDEN)) {
            if (ui_context->lora.stats.updated) {
                char buf[512];
                snprintf(buf, sizeof(buf),
                    "#AAAAAA Node ID:# !%08lX\n"
                    "#AAAAAA Uptime:# %lu s\n"
                    "#AAAAAA Battery:# %lu%% (%.2fV)\n"
                    "#AAAAAA Chan Util:# %.1f%%\n"
                    "#AAAAAA TX Air Util:# %.1f%%\n"
                    "#AAAAAA Packets RX:# %lu\n"
                    "#AAAAAA Packets TX:# %lu\n"
                    "#AAAAAA Nodes Online:# %u / %u",
                    (unsigned long)ui_context->lora.stats.my_node_num,
                    (unsigned long)ui_context->lora.stats.uptime_seconds,
                    (unsigned long)ui_context->lora.stats.battery_level,
                    ui_context->lora.stats.voltage,
                    ui_context->lora.stats.channel_utilization,
                    ui_context->lora.stats.air_util_tx,
                    (unsigned long)ui_context->lora.stats.num_packets_rx,
                    (unsigned long)ui_context->lora.stats.num_packets_tx,
                    ui_context->lora.stats.num_online_nodes,
                    ui_context->lora.stats.num_total_nodes
                );
                lv_label_set_text(lbl_lora_stats, buf);
                ui_context->lora.stats.updated = false;
            }
        }

        // Update LoRa Node DB Panel
        if (nodedb_list && lora_nodedb_panel && !lv_obj_has_flag(lora_nodedb_panel, LV_OBJ_FLAG_HIDDEN)) {
            if (ui_context->lora.nodedb_updated) {
                lv_obj_clean(nodedb_list);
                for (const auto& n : ui_context->lora.known_nodes) {
                    char buf[128];
                    uint32_t age = (millis() - n.last_heard) / 1000;
                    char safe_long_name[sizeof(n.long_name) + 1];
                    snprintf(safe_long_name, sizeof(safe_long_name), "%.*s", sizeof(n.long_name), n.long_name);
                    snprintf(buf, sizeof(buf), "%s\n!%08lx  %lus ago  SNR: %.1f",
                        safe_long_name[0] != '\0' ? safe_long_name : "Unknown",
                        (unsigned long)n.num, (unsigned long)age, n.snr);
                    lv_list_add_text(nodedb_list, buf);
                }
                ui_context->lora.nodedb_updated = false;
            }
        }
        xSemaphoreGive(ui_context->lora.mutex);
    }

    if (unread_chat_status) {
        static bool msg_blink = false;
        msg_blink = !msg_blink;
        lv_obj_set_style_text_color(lbl_msg, lv_color_hex(msg_blink ? 0x00FF88 : 0x444444), 0);
    } else {
        lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x444444), 0);
    }
}
