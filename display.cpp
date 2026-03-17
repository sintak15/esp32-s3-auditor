#include "display.h"
#include "constants.h"
#include <TFT_eSPI.h>
#include <Wire.h>
#include <esp_heap_caps.h>

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
extern lv_obj_t *ui_spinner;

// Global objects for this module
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t *lvgl_buf1 = nullptr;
static lv_color_t *lvgl_buf2 = nullptr;

static AppContext* ui_context = nullptr;

// Thread-safe touch state memory to completely decouple LVGL from the I2C bus
struct VolatileTouchState {
    volatile bool pressed;
    volatile int16_t x;
    volatile int16_t y;
};
static VolatileTouchState shared_touch = {false, 0, 0};

void set_ui_update_context(AppContext* context) {
    ui_context = context;
}

TouchPoint get_touch() {
    TouchPoint pt = {0, 0, false};

    // ISOLATION TEST 2: Uncomment to stub touch completely
    // return pt;

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
    uint32_t t0 = millis();
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // pushImage is significantly faster than setAddrWindow + pushColors
    // because it uses block memory transfers instead of byte-by-byte
    tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)cm);
    
    lv_disp_flush_ready(drv);
    
    uint32_t dt = millis() - t0;
    if (dt > 20) {
        Serial.printf("[FLUSH] %lux%lu dt=%lu area=(%d,%d)-(%d,%d)\n",
            (unsigned long)w, (unsigned long)h, (unsigned long)dt,
            area->x1, area->y1, area->x2, area->y2);
    }
}

// This LVGL callback now returns instantly, entirely immune to I2C bus hangs
void lvgl_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t lx = 0, ly = 0;
    
    // ISOLATION TEST: Uncomment this single line to permanently disable touch entirely
    // data->state = LV_INDEV_STATE_REL; return;
    
    bool is_pressed = shared_touch.pressed;
    if (is_pressed) {
        lx = shared_touch.x;
        ly = shared_touch.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = lx;
    data->point.y = ly;
}

// Dedicated background task to handle slow/unreliable I2C polling safely on Core 0
static void touch_service_task(void *pv) {
    for (;;) {
        uint32_t t0 = millis();
        TouchPoint pt = get_touch();
        
        shared_touch.pressed = pt.pressed;
        if (pt.pressed) {
            shared_touch.x = pt.x;
            shared_touch.y = pt.y;
        }
        
        uint32_t dt = millis() - t0;
        if (dt > 15) {
            Serial.printf("[TOUCH-WARN] I2C read stalled for %lu ms\n", (unsigned long)dt);
        }
        
        vTaskDelay(pdMS_TO_TICKS(30)); // Poll safely at ~33Hz without starving FreeRTOS
    }
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

    // Spin up the background I2C touch polling task away from the LVGL render core
    xTaskCreatePinnedToCore(touch_service_task, "touch_task", 2048, NULL, 2, NULL, 0);

    tft.begin();
    tft.setRotation(0);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    
    tft.setSwapBytes(true); // Required for pushImage to correctly display LVGL colors

    // CRITICAL FIX: Force LVGL buffers to PSRAM to preserve internal RAM for stacks and tasks.
    // The ESP32-S3 has 8MB of PSRAM that's barely used, while internal RAM is critically low.
    // With OPI PSRAM, the performance penalty is minimal (~5ms extra flush time), but we gain
    // ~13KB of internal RAM which prevents heap exhaustion crashes.
    uint32_t buf_pixels = SCREEN_W * 20; 
    lvgl_buf1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lvgl_buf1) {
        Serial.println("[FATAL] PSRAM alloc failed - check PSRAM is enabled in Arduino IDE");
        while(1) delay(100);
    }
    Serial.printf("[UI] LVGL buffer allocated in PSRAM: %u bytes\n", buf_pixels * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, nullptr, buf_pixels);
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
    // ISOLATION TEST A: Uncomment to completely disable UI tick updates
    // return;

    uint32_t t0 = millis();
    uint32_t t_stage;
    
    if (!ui_context) return;
    // Guard against timer firing before ui_build() has finished assigning all objects
    if (!lbl_batt || !lbl_batt_pct || !lbl_sd || !lbl_wifi || !lbl_msg || !tabview ||
        !ta_lora_log || !lora_log_panel || !ta_lora_chat || !lora_chat_panel ||
        !lbl_lora_stats || !lora_stats_panel || !nodedb_list || !lora_nodedb_panel) return;

    t_stage = millis();
    static int last_batteryPct = -1;
    static bool last_isCharging = false;
    static bool last_sdMounted = false;
    static int last_batt_col = -1;

    StatusSnapshot ss = read_status_snapshot();

    // Battery
    uint32_t batt_col = ss.batteryPct < 20 ? 0xFF4444 : ss.batteryPct < 50 ? 0xFFFF00 : 0xFFFFFF;
    if (ss.isCharging) {
        batt_col = 0x00FF88; // Bright green when charging
    }
    
    if (ss.batteryPct != last_batteryPct || ss.isCharging != last_isCharging) {
        if (ss.isCharging) {
            lv_label_set_text(lbl_batt, LV_SYMBOL_CHARGE);
        } else {
            if (ss.batteryPct > 80) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_FULL);
            else if (ss.batteryPct > 60) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_3);
            else if (ss.batteryPct > 35) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_2);
            else if (ss.batteryPct > 15) lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_1);
            else lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_EMPTY);
        }
        char bpbuf[8];
        snprintf(bpbuf, sizeof(bpbuf), "%d%%", ss.batteryPct);
        lv_label_set_text(lbl_batt_pct, bpbuf);
        last_batteryPct = ss.batteryPct;
        last_isCharging = ss.isCharging;
    }

    if ((int)batt_col != last_batt_col) {
        lv_obj_set_style_text_color(lbl_batt, lv_color_hex(batt_col), 0);
        lv_obj_set_style_text_color(lbl_batt_pct, lv_color_hex(batt_col), 0);
        last_batt_col = batt_col;
    }

    if (ss.sdMounted != last_sdMounted) {
        lv_obj_set_style_text_color(lbl_sd, lv_color_hex(ss.sdMounted ? 0x00FF88 : 0xFF4444), 0);
        last_sdMounted = ss.sdMounted;
    }

    if (millis() - t_stage > 5) Serial.printf("[UI] battery block %lu ms\n", (unsigned long)(millis() - t_stage));

    t_stage = millis();

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
                lv_indev_t * indev = lv_indev_get_next(NULL);
                if (!((indev && indev->proc.state == LV_INDEV_STATE_PR) || lv_obj_is_scrolling(ta_lora_log))) {
                    lv_obj_scroll_to_y(ta_lora_log, LV_COORD_MAX, LV_ANIM_OFF);
                }
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
                lv_indev_t * indev = lv_indev_get_next(NULL);
                if (!((indev && indev->proc.state == LV_INDEV_STATE_PR) || lv_obj_is_scrolling(ta_lora_chat))) {
                    lv_obj_scroll_to_y(ta_lora_chat, LV_COORD_MAX, LV_ANIM_OFF);
                }
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
                static uint32_t last_nodedb_draw = 0;
                
                lv_indev_t * indev = lv_indev_get_next(NULL);
                bool is_touched = (indev && indev->proc.state == LV_INDEV_STATE_PR) || lv_obj_is_scrolling(nodedb_list);
                
                if (!is_touched && (millis() - last_nodedb_draw > 2000)) { // Limit destructive UI rebuilds
                    static char ndb_buf[2048];
                    ndb_buf[0] = '\0';
                    for (const auto& n : ui_context->lora.known_nodes) {
                        char buf[128];
                        uint32_t age = (millis() - n.last_heard) / 1000;
                        snprintf(buf, sizeof(buf), "%s\n!%08lx  %lus ago  SNR: %.1f\n\n",
                            (n.long_name[0] != '\0') ? n.long_name : "Unknown",
                            (unsigned long)n.num, (unsigned long)age, n.snr);
                        if (strlen(ndb_buf) + strlen(buf) < sizeof(ndb_buf) - 1) {
                            strcat(ndb_buf, buf);
                        }
                    }
                    lv_textarea_set_text(nodedb_list, ndb_buf);
                    ui_context->lora.nodedb_updated = false;
                    last_nodedb_draw = millis();
                }
            }
        }
        xSemaphoreGive(ui_context->lora.mutex);
    }
    if (millis() - t_stage > 5) Serial.printf("[UI] lora block %lu ms\n", (unsigned long)(millis() - t_stage));

    t_stage = millis();
    static bool last_unread_chat = false;
    static uint32_t last_blink = 0;
    static int last_msg_color = -1;

    if (unread_chat_status) { // Blink continuously if unread
        if (millis() - last_blink > 500) {
            static bool msg_blink = false;
            msg_blink = !msg_blink;
            int new_color = msg_blink ? 0x00FF88 : 0x444444;
            if (new_color != last_msg_color) {
                lv_obj_set_style_text_color(lbl_msg, lv_color_hex(new_color), 0);
                last_msg_color = new_color;
            }
            last_blink = millis();
        }
        last_unread_chat = true;
    } else if (last_unread_chat) { // Revert to gray once only
        if (last_msg_color != 0x444444) {
            lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x444444), 0);
            last_msg_color = 0x444444;
        }
        last_unread_chat = false;
    }

    // Activity Spinner
    if (ui_spinner) {
        bool is_active = (ui_context->sniffer.pcap_active || ui_context->sniffer.probe_active || 
                          ui_context->ble.sniff_active || ui_context->ble.flood_active || 
                          ui_context->pentest.current_mode != PT_NONE);
        static int last_is_active = -1;
        if ((int)is_active != last_is_active) {
            // FIX: Hidden spinners STILL run animation timers and invalidate the screen. 
            // We must pause the animation entirely when hidden.
            if (is_active) { lv_obj_clear_flag(ui_spinner, LV_OBJ_FLAG_HIDDEN); lv_anim_del(ui_spinner, NULL); }
            else { lv_obj_add_flag(ui_spinner, LV_OBJ_FLAG_HIDDEN); lv_anim_del(ui_spinner, NULL); }
            last_is_active = is_active;
        }
    }

    // Screen Dimming Timeout (60 seconds)
    static int last_backlight = -1;
    int current_backlight = (lv_disp_get_inactive_time(NULL) > 60000) ? LOW : HIGH;
    if (current_backlight != last_backlight) {
        digitalWrite(TFT_BL, current_backlight);
        last_backlight = current_backlight;
    }

    if (millis() - t_stage > 5) Serial.printf("[UI] misc block %lu ms\n", (unsigned long)(millis() - t_stage));

    uint32_t dt = millis() - t0;
    if (dt > 10) {
        Serial.printf("[UI] ui_update_tick %lu ms\n", (unsigned long)dt);
    }
}
