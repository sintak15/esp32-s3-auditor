#include "ui_module.h"
#include "sd_logger.h"
#include <Arduino.h>
#include <nvs_flash.h> // For NVS functions
#include "audit_actions.h" // For save_beacon_ssids_to_nvs
#include "types.h" // Includes constants.h indirectly
#include "constants.h" // Explicitly include constants.h for MAX_BEACON_SSIDS etc.
#include "display.h"

// Global AppContext instance (defined in main .ino file)
extern AppContext g_app_context;
extern void reset_battery_calibration();

// Instantiate UI Handles
lv_obj_t *main_screen, *status_bar, *tabview;
lv_obj_t *lbl_sd, *lbl_wifi, *lbl_batt, *lbl_batt_pct, *lbl_msg;
lv_obj_t *tab_home, *tab_scan, *tab_audit, *tab_ble, *tab_pcap, *tab_probes, *tab_settings, *tab_lora, *tab_gps; 
lv_obj_t *scan_list, *lbl_scan_count, *btn_scan_pause, *lbl_scan_pause;
lv_obj_t *btn_view_ap, *btn_view_sta, *btn_view_linked;
lv_obj_t *lbl_audit_target, *lbl_audit_bssid, *lbl_audit_status;
lv_obj_t *btn_reconnect, *btn_beacon, *btn_pmkid, *btn_stop_audit;
lv_obj_t *lbl_ble_status;
lv_obj_t *lbl_pcap_ch;
lv_obj_t *btn_pcap_lock;
lv_obj_t *probe_list;
lv_obj_t *btn_ble_scan  = nullptr;
lv_obj_t *btn_ble_adv_test  = nullptr;
lv_obj_t *btn_pcap_start = nullptr;
lv_obj_t *ta_beacon_ssids = nullptr; // Text area for beacon SSIDs (declared here)
lv_obj_t *btn_probe_start = nullptr; // Corrected to be a single declaration
lv_obj_t *ta_lora_log = nullptr;
lv_obj_t *ta_lora_input = nullptr;
static lv_obj_t *kb = nullptr;
lv_obj_t *lora_stats_panel = nullptr;
lv_obj_t *lbl_lora_stats = nullptr;
lv_obj_t *lora_nodedb_panel = nullptr;
lv_obj_t *nodedb_list = nullptr;
lv_obj_t *lora_chat_panel = nullptr;
lv_obj_t *ta_lora_chat = nullptr;
lv_obj_t *ta_lora_chat_input = nullptr;
lv_obj_t *lora_log_panel = nullptr;
lv_obj_t *temp_warning_border = nullptr;
lv_obj_t *ui_spinner = nullptr;
lv_obj_t *lbl_gps_data = nullptr;
lv_obj_t *lbl_pcap_status = nullptr; // Corrected to be a single declaration
lv_obj_t *low_batt_border = nullptr;
lv_obj_t *battery_stats_panel = nullptr;
lv_obj_t *reboot_panel = nullptr;
lv_obj_t *brightness_panel = nullptr;
lv_obj_t *lbl_battery_stats = nullptr;
lv_obj_t *ui_battery_chart = nullptr;
lv_chart_series_t *ui_battery_series = nullptr;
lv_obj_t *lbl_reset_cal_button = nullptr; // Globally define the label for the reset button

// Confirmation dialog elements
lv_obj_t *confirm_reset_panel = nullptr;
lv_chart_series_t *ui_heap_series = nullptr;

lv_obj_t *beacon_ssid_panel = nullptr;
lv_obj_t *diagnostics_panel = nullptr;
lv_obj_t *ta_diagnostics = nullptr;
lv_obj_t *sys_stats_panel = nullptr;
lv_obj_t *ta_sys_stats = nullptr;

extern void toggle_web_server();
extern void reboot_cyd();

lv_style_t style_btn_dark, style_btn_red, style_btn_orange,
           style_btn_blue, style_view_active, style_view_inactive;

void no_scroll(lv_obj_t *o) {
  lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(o, LV_DIR_NONE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_width(o, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_height(o, 0, LV_PART_SCROLLBAR);
}

void init_btn_style(lv_style_t *s, uint32_t bg, uint32_t border) {
  lv_style_init(s);
  lv_style_set_bg_color(s, lv_color_hex(bg));
  lv_style_set_border_color(s, lv_color_hex(border));
  lv_style_set_border_width(s, 1);
  lv_style_set_radius(s, 6);
}

lv_obj_t* make_action_btn(lv_obj_t *parent, const char *label, lv_style_t *style,
                        uint32_t tc, lv_event_cb_t cb, int y) {
  lv_obj_t *b = lv_btn_create(parent); 
  lv_obj_set_size(b, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y); lv_obj_add_style(b, style, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l=lv_label_create(b); lv_label_set_text(l, label);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(tc), 0); lv_obj_center(l);
  lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
  return b;
}

void ui_build() {
  init_btn_style(&style_btn_dark,   0x1E1E1E, 0x333333);
  init_btn_style(&style_btn_red,    0x300000, 0xFF4444);
  init_btn_style(&style_btn_orange, 0x2A1500, 0xFFAA00);
  init_btn_style(&style_btn_blue,   0x001A30, 0x00AAFF);

  lv_style_init(&style_view_active);
  lv_style_set_bg_color(&style_view_active, lv_color_hex(0x003322));
  lv_style_set_border_color(&style_view_active, lv_color_hex(0x00FFCC));
  lv_style_set_border_width(&style_view_active, 1);
  lv_style_init(&style_view_inactive);
  lv_style_set_bg_color(&style_view_inactive, lv_color_hex(0x1A1A1A));
  lv_style_set_border_color(&style_view_inactive, lv_color_hex(0x333333));
  lv_style_set_border_width(&style_view_inactive, 1);

  main_screen=lv_scr_act();
  lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x000000), 0);

  // ── Status Bar ────────────────────────────────
  status_bar=lv_obj_create(main_screen);
  lv_obj_set_size(status_bar, SCREEN_W, UI::Layout::HeaderHeight); lv_obj_set_pos(status_bar, 0, 0);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_width(status_bar, 0, 0);
  lv_obj_set_style_radius(status_bar, 0, 0);
  lv_obj_set_style_pad_all(status_bar, 4, 0);
  no_scroll(status_bar);

  lv_obj_t *title=lv_label_create(status_bar);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS); // Removed "AUDITOR" to save space
  lv_obj_set_style_text_color(title, lv_color_hex(UI::Colors::Primary), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

  ui_spinner = lv_spinner_create(status_bar, 1000, 60);
  lv_obj_set_size(ui_spinner, 16, 16);
  lv_obj_align(ui_spinner, LV_ALIGN_LEFT_MID, 22, 0);
  lv_obj_add_flag(ui_spinner, LV_OBJ_FLAG_HIDDEN); // Hidden by default

  lv_obj_t *icont=lv_obj_create(status_bar);
  lv_obj_set_size(icont, 200, 24); lv_obj_align(icont, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(icont, 0, 0); lv_obj_set_style_border_width(icont, 0, 0);
  lv_obj_set_flex_flow(icont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(icont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(icont, 6, 0);
  no_scroll(icont);

  lbl_msg=lv_label_create(icont);     lv_label_set_text(lbl_msg, LV_SYMBOL_ENVELOPE);
  lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x444444), 0);
  lbl_wifi=lv_label_create(icont);    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xFFFFFF), 0);
  lbl_sd=lv_label_create(icont);      lv_label_set_text(lbl_sd, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_color(lbl_sd, sd_card_ready()?lv_color_hex(UI::Colors::Success):lv_color_hex(UI::Colors::Error), 0);
  lbl_batt=lv_label_create(icont);    lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_FULL);
  lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0xFFFFFF), 0);
  lbl_batt_pct=lv_label_create(icont); lv_label_set_text(lbl_batt_pct, "--%");
  lv_obj_set_style_text_color(lbl_batt_pct, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl_batt_pct, &lv_font_montserrat_14, 0);

  // ── Tabview ───────────────────────────────────
  tabview=lv_tabview_create(main_screen, LV_DIR_TOP, 0);
  lv_obj_set_size(tabview, SCREEN_W, SCREEN_H - UI::Layout::HeaderHeight);
  lv_obj_align(tabview, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), 0);
  
  // ISOLATION TEST 3: Disable all tabview switch animations
  lv_obj_set_style_anim_time(tabview, 0, 0);

  lv_obj_t *tv_cont=lv_tabview_get_content(tabview);
  no_scroll(tv_cont);
  lv_obj_set_scrollbar_mode(tabview, LV_SCROLLBAR_MODE_OFF);
  uint32_t cc=lv_obj_get_child_cnt(tabview);
  for (uint32_t i=0; i<cc; i++) {
    lv_obj_t *ch=lv_obj_get_child(tabview,i);
    if (ch!=tv_cont) lv_obj_add_flag(ch, LV_OBJ_FLAG_HIDDEN);
  }

  tab_home    =lv_tabview_add_tab(tabview, "Home");
  tab_scan    =lv_tabview_add_tab(tabview, "Scan");
  tab_audit   =lv_tabview_add_tab(tabview, "Audit");
  tab_ble     =lv_tabview_add_tab(tabview, "BLE");
  tab_pcap    =lv_tabview_add_tab(tabview, "PCAP");
  tab_probes  =lv_tabview_add_tab(tabview, "Probes");
  tab_settings =lv_tabview_add_tab(tabview, "Settings");
  tab_lora    =lv_tabview_add_tab(tabview, "LoRa");
  tab_gps     =lv_tabview_add_tab(tabview, "GPS");
  beacon_ssid_panel = lv_tabview_add_tab(tabview, "SSIDs"); // Index 9
  diagnostics_panel = lv_tabview_add_tab(tabview, "Diag");  // Index 10
  sys_stats_panel   = lv_tabview_add_tab(tabview, "Perf");  // Index 11
  battery_stats_panel = lv_tabview_add_tab(tabview, "Batt");// Index 12
  reboot_panel = lv_tabview_add_tab(tabview, "Reboot");     // Index 13
  brightness_panel = lv_tabview_add_tab(tabview, "Bright"); // Index 14
  lora_log_panel     = lv_tabview_add_tab(tabview, "LoRaTerm");  // Index 15
  lora_nodedb_panel  = lv_tabview_add_tab(tabview, "LoRaNodes"); // Index 16
  lora_stats_panel   = lv_tabview_add_tab(tabview, "LoRaStats"); // Index 17
  lora_chat_panel    = lv_tabview_add_tab(tabview, "LoRaChat");  // Index 18
  
  lv_obj_t *all_tabs[]={tab_home,tab_scan,tab_audit,tab_ble,tab_pcap,tab_probes,tab_settings,tab_lora,tab_gps,
                        beacon_ssid_panel, diagnostics_panel, sys_stats_panel, battery_stats_panel, reboot_panel, brightness_panel,
                        lora_log_panel, lora_nodedb_panel, lora_stats_panel, lora_chat_panel,
                        tv_cont};
  for (int i=0; i<sizeof(all_tabs)/sizeof(all_tabs[0]); i++) no_scroll(all_tabs[i]);

  // ── Home Hub 3x2 ──────────────────────────────
  lv_obj_set_layout(tab_home, LV_LAYOUT_GRID);
  static lv_coord_t col_dsc[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(tab_home, col_dsc, row_dsc);

  auto hub=[](lv_obj_t *p,const char *icon,const char *txt,int c,int r,lv_event_cb_t cb,uint32_t col) {
    lv_obj_t *b=lv_btn_create(p);
    lv_obj_set_grid_cell(b,LV_GRID_ALIGN_STRETCH,c,1,LV_GRID_ALIGN_STRETCH,r,1);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x181818),0);
    lv_obj_set_style_border_color(b,lv_color_hex(col),0);
    lv_obj_set_style_border_width(b,1,0); lv_obj_set_style_radius(b,6,0);
    lv_obj_set_flex_flow(b,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_t *li=lv_label_create(b); lv_label_set_text(li,icon);
    lv_obj_set_style_text_color(li, lv_color_hex(col), 0);
    lv_obj_t *lt=lv_label_create(b); lv_label_set_text(lt,txt);
    lv_obj_set_width(lt, lv_pct(100));
    lv_label_set_long_mode(lt, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lt,lv_color_hex(0xCCCCCC),0);
  };
  hub(tab_home,LV_SYMBOL_WIFI,      "SCAN",    0,0,cb_nav_scan,                                               UI::Colors::Primary);
  hub(tab_home,LV_SYMBOL_WARNING,   "AUDIT",   1,0,[](lv_event_t*){ navigate_to(2); },0xFF4444);
  hub(tab_home,LV_SYMBOL_BLUETOOTH, "BLE",     0,1,[](lv_event_t*){ navigate_to(3); },0x4444FF);
  hub(tab_home,LV_SYMBOL_FILE,      "PCAP",    1,1,[](lv_event_t*){ navigate_to(4); },UI::Colors::Warning); // Corrected color token
  hub(tab_home,LV_SYMBOL_EYE_OPEN,  "PROBES",  0,2,[](lv_event_t*){ navigate_to(5); },0xFF00FF);
  hub(tab_home,LV_SYMBOL_SETTINGS,  "SETTINGS",1,2,[](lv_event_t*){ navigate_to(6); },0xAAAAAA);
  hub(tab_home,LV_SYMBOL_USB,       "LORA",    0,3,[](lv_event_t*){ navigate_to(7); },0x00AAFF);
  hub(tab_home,LV_SYMBOL_GPS,       "GPS",     1,3,[](lv_event_t*){ navigate_to(8); },UI::Colors::Success);

  // ── Shared return-home button builder ─────────
  auto add_return_btn=[](lv_obj_t *parent) {
    lv_obj_t *btn=lv_btn_create(parent); 
    lv_obj_add_flag(btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(btn, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight); // Standardized size
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -UI::Layout::Padding); lv_obj_add_style(btn, &style_btn_dark, 0); // Standardized position
    lv_obj_add_event_cb(btn, cb_nav_home, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l=lv_label_create(btn); lv_label_set_text(l,LV_SYMBOL_HOME " RETURN HOME");
    lv_obj_center(l);
  };

  // Drill-down return to Settings helper
  auto add_settings_back_btn=[](lv_obj_t *parent) {
    lv_obj_t *btn=lv_btn_create(parent); 
    lv_obj_add_flag(btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(btn, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -UI::Layout::Padding); lv_obj_add_style(btn, &style_btn_dark, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t*){ navigate_to(6); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l=lv_label_create(btn); lv_label_set_text(l, LV_SYMBOL_LEFT " BACK TO SETTINGS");
    lv_obj_center(l);
  };

  // ── Scan Tab ──────────────────────────────────
  lv_obj_set_style_pad_all(tab_scan, 0, 0);
  lv_obj_t *vrow=lv_obj_create(tab_scan);
  lv_obj_set_size(vrow,SCREEN_W,28); lv_obj_align(vrow,LV_ALIGN_TOP_MID,0,0);
  lv_obj_set_style_bg_color(vrow,lv_color_hex(0x111111),0);
  lv_obj_set_style_border_width(vrow,0,0); lv_obj_set_style_radius(vrow,0,0);
  lv_obj_set_style_pad_all(vrow,2,0); no_scroll(vrow);
  lv_obj_set_flex_flow(vrow,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vrow,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  auto vbtn=[&](const char *l,lv_event_cb_t cb,lv_obj_t **out) {
    lv_obj_t *b=lv_btn_create(vrow); lv_obj_set_size(b,72,22);
    lv_obj_add_style(b,&style_view_inactive,0);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
    lv_obj_t *lbl=lv_label_create(b); lv_label_set_text(lbl,l);
    lv_obj_set_style_text_font(lbl,&lv_font_montserrat_14,0);
    lv_obj_center(lbl); *out=b;
  };
  vbtn("APs",    cb_view_ap,     &btn_view_ap);
  vbtn("STAs",   cb_view_sta,    &btn_view_sta);
  vbtn("Linked", cb_view_linked, &btn_view_linked);
  lv_obj_add_style(btn_view_ap, &style_view_active, 0);

  lbl_scan_count=lv_label_create(tab_scan);
  lv_label_set_text(lbl_scan_count,"APs: 0  STAs: 0");
  lv_obj_set_style_text_color(lbl_scan_count,lv_color_hex(0x888888),0);
  lv_obj_set_style_text_font(lbl_scan_count,&lv_font_montserrat_14,0);
  lv_obj_align(lbl_scan_count,LV_ALIGN_TOP_MID,0,30);

  // scan_list: 288 - 28 (vrow) - 20 (count) - 2 (gap) - 44 (footer) = 194, use 190 for safety
  scan_list=lv_list_create(tab_scan);
  lv_obj_set_size(scan_list,SCREEN_W,190); lv_obj_align(scan_list,LV_ALIGN_TOP_MID,0,50);
  lv_obj_set_style_bg_color(scan_list,lv_color_hex(0x080808),0);
  lv_obj_set_style_border_width(scan_list,0,0); lv_obj_set_style_radius(scan_list,0,0);
  lv_obj_set_style_pad_all(scan_list,0,0);
  lv_obj_set_scrollbar_mode(scan_list,LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(scan_list,LV_DIR_VER);
  lv_obj_set_style_width(scan_list,0,LV_PART_SCROLLBAR);
  lv_obj_set_style_height(scan_list,0,LV_PART_SCROLLBAR);

  lv_obj_t *sfooter=lv_obj_create(tab_scan);
  lv_obj_set_size(sfooter,SCREEN_W,44); lv_obj_align(sfooter,LV_ALIGN_BOTTOM_MID,0,0);
  lv_obj_set_style_bg_color(sfooter,lv_color_hex(0x0D0D0D),0);
  lv_obj_set_style_border_width(sfooter,0,0); lv_obj_set_style_radius(sfooter,0,0);
  lv_obj_set_style_pad_all(sfooter,4,0); no_scroll(sfooter);
  lv_obj_set_flex_flow(sfooter,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sfooter,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  lv_obj_t *b_home=lv_btn_create(sfooter); lv_obj_set_size(b_home,100,34);
  lv_obj_add_style(b_home,&style_btn_dark,0);
  lv_obj_add_event_cb(b_home,cb_nav_home,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lh=lv_label_create(b_home); lv_label_set_text(lh,LV_SYMBOL_HOME " HOME"); lv_obj_center(lh);
  btn_scan_pause=lv_btn_create(sfooter); lv_obj_set_size(btn_scan_pause,120,34);
  lv_obj_set_style_bg_color(btn_scan_pause,lv_color_hex(0x003322),0);
  lv_obj_set_style_border_color(btn_scan_pause,lv_color_hex(UI::Colors::Secondary),0);
  lv_obj_set_style_border_width(btn_scan_pause, 1, 0);
  lv_obj_add_event_cb(btn_scan_pause,cb_pause_scan,LV_EVENT_CLICKED,nullptr);
  lbl_scan_pause=lv_label_create(btn_scan_pause);
  lv_label_set_text(lbl_scan_pause,LV_SYMBOL_PLAY " START");
  lv_obj_set_style_text_color(lbl_scan_pause,lv_color_hex(UI::Colors::Primary),0);
  lv_obj_center(lbl_scan_pause);

  // ── Audit Tab ───────────────────────────────────
  lv_obj_set_style_pad_all(tab_audit, 6, 0);
  lbl_audit_target = lv_label_create(tab_audit);
  lv_label_set_recolor(lbl_audit_target, true);
  lv_label_set_text(lbl_audit_target, "#888888 Select a network from SCAN#");
  lv_obj_set_width(lbl_audit_target, SCREEN_W - (UI::Layout::Margin * 2));
  // FIX: SCROLL_CIRCULAR runs an infinite animation timer that invalidates the screen even when idle
  lv_label_set_long_mode(lbl_audit_target, LV_LABEL_LONG_DOT);
  lv_obj_align(lbl_audit_target, LV_ALIGN_TOP_MID, 0, 2);
  lbl_audit_bssid = lv_label_create(tab_audit);
  lv_obj_set_style_text_color(lbl_audit_bssid, lv_color_hex(0x555555), 0);
  lv_obj_set_style_text_font(lbl_audit_bssid, &lv_font_montserrat_14, 0);
  lv_label_set_text(lbl_audit_bssid, "---");
  // Prevent scrolling label from cascading full-screen redraws:
  lv_obj_align(lbl_audit_bssid, LV_ALIGN_TOP_MID, 0, 24);
  lbl_audit_status = lv_label_create(tab_audit);
  lv_label_set_recolor(lbl_audit_status, true);
  lv_obj_set_style_text_font(lbl_audit_status, &lv_font_montserrat_14, 0);
  lv_label_set_text(lbl_audit_status, "#666666 IDLE#");
  lv_obj_set_width(lbl_audit_status, SCREEN_W - 20);
  lv_obj_align(lbl_audit_status, LV_ALIGN_TOP_MID, 0, 42);
  int y=62;
  btn_reconnect = make_action_btn(tab_audit, LV_SYMBOL_WARNING   " RECONNECT TEST", &style_btn_red,   0xFF4444, cb_start_reconnect_test, y); y += 42;
  btn_beacon = make_action_btn(tab_audit, LV_SYMBOL_AUDIO     " BEACON LOAD",  &style_btn_orange, 0xFFAA00, cb_start_beacon, y); y += 42;
  btn_pmkid = make_action_btn(tab_audit, LV_SYMBOL_EYE_OPEN  " PMKID CAPTURE", &style_btn_blue,  0x00AAFF, cb_start_pmkid, y);
  btn_stop_audit = lv_btn_create(tab_audit);
  lv_obj_set_size(btn_stop_audit, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight); 
  lv_obj_align(btn_stop_audit, LV_ALIGN_BOTTOM_MID, 0, -52);
  lv_obj_set_style_bg_color(btn_stop_audit, lv_color_hex(0x1A0000), 0);
  lv_obj_set_style_border_color(btn_stop_audit, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_border_width(btn_stop_audit, 2, 0);
  lv_obj_add_event_cb(btn_stop_audit, cb_stop_audit_action, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sl2=lv_label_create(btn_stop_audit); lv_label_set_text(sl2,LV_SYMBOL_STOP " STOP");
  lv_obj_set_style_text_color(sl2, lv_color_hex(UI::Colors::Error), 0); lv_obj_center(sl2);
  lv_obj_add_flag(btn_stop_audit,LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *bbk=lv_btn_create(tab_audit); 
  lv_obj_set_size(bbk, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(bbk,LV_ALIGN_BOTTOM_MID,0,-UI::Layout::Padding); lv_obj_add_style(bbk,&style_btn_dark,0);
  lv_obj_add_event_cb(bbk,cb_nav_scan,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lbbk=lv_label_create(bbk); lv_label_set_text(lbbk,LV_SYMBOL_LEFT " BACK TO SCAN"); lv_obj_center(lbbk);

  // ── BLE Tab ───────────────────────────────────
  lbl_ble_status=lv_label_create(tab_ble);
  lv_label_set_recolor(lbl_ble_status,true);
  lv_label_set_text(lbl_ble_status,"#00FF88 BLE READY#\n\nSelect an action.");
  lv_obj_align(lbl_ble_status,LV_ALIGN_TOP_LEFT,5,5);
  lv_obj_set_width(lbl_ble_status, SCREEN_W - (UI::Layout::Margin * 2));
  lv_label_set_long_mode(lbl_ble_status,LV_LABEL_LONG_WRAP);
  btn_ble_scan=lv_btn_create(tab_ble); 
  lv_obj_set_size(btn_ble_scan, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(btn_ble_scan,LV_ALIGN_BOTTOM_MID,0,-96);
  lv_obj_add_style(btn_ble_scan,&style_btn_dark,0);
  lv_obj_add_event_cb(btn_ble_scan,cb_toggle_ble_scan,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *ls=lv_label_create(btn_ble_scan); lv_label_set_text(ls,"START BLE SCAN"); lv_obj_center(ls);
  btn_ble_adv_test=lv_btn_create(tab_ble); 
  lv_obj_set_size(btn_ble_adv_test, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(btn_ble_adv_test,LV_ALIGN_BOTTOM_MID,0,-50);
  lv_obj_add_style(btn_ble_adv_test,&style_btn_red,0);
  lv_obj_add_event_cb(btn_ble_adv_test,cb_toggle_ble_adv_test,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lsp=lv_label_create(btn_ble_adv_test); lv_label_set_text(lsp,"START BLE ADV TEST"); lv_obj_center(lsp);
  add_return_btn(tab_ble);

  // ── PCAP Tab ──────────────────────────────────
  lbl_pcap_status=lv_label_create(tab_pcap);
  lv_label_set_recolor(lbl_pcap_status,true);
  lv_label_set_text(lbl_pcap_status,"#00FF88 PCAP CAPTURE#\n\nReady. Saves .pcap to SD.");
  lv_obj_align(lbl_pcap_status, LV_ALIGN_TOP_LEFT, UI::Layout::PaddingInner, UI::Layout::PaddingInner);
  lv_obj_set_width(lbl_pcap_status,SCREEN_W-10);
  lv_label_set_long_mode(lbl_pcap_status,LV_LABEL_LONG_WRAP);

  // Channel lock row: [-] [CH N] [+] [HOP/LOCKED]
  lv_obj_t *ch_row=lv_obj_create(tab_pcap);
  lv_obj_set_size(ch_row,SCREEN_W-10,36); lv_obj_align(ch_row,LV_ALIGN_BOTTOM_MID,0,-96);
  lv_obj_set_style_bg_opa(ch_row,0,0); lv_obj_set_style_border_width(ch_row,0,0);
  lv_obj_set_flex_flow(ch_row,LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ch_row,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
  no_scroll(ch_row);
  auto small_btn=[&](lv_obj_t *p,const char *l,lv_event_cb_t cb)->lv_obj_t* {
    // Assuming g_app_context is globally available
    extern AppContext g_app_context;
    lv_obj_t *b=lv_btn_create(p); lv_obj_set_size(b,36,30);
    lv_obj_add_style(b,&style_btn_dark,0);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
    lv_obj_t *lb=lv_label_create(b); lv_label_set_text(lb,l); lv_obj_center(lb);
    return b;
  };
  small_btn(ch_row,"-",[](lv_event_t*) { // Use g_app_context.capture.pcap_locked_ch
    if (g_app_context.capture.pcap_locked_ch > 1) g_app_context.capture.pcap_locked_ch--;
    char buf[8]; snprintf(buf,sizeof(buf),"CH %d",g_app_context.capture.pcap_locked_ch);
    lv_label_set_text(lbl_pcap_ch,buf);
  });
  lbl_pcap_ch=lv_label_create(ch_row);
  lv_label_set_text(lbl_pcap_ch,"CH 1"); // Initial value, will be updated by process_channel_hop
  lv_obj_set_style_text_color(lbl_pcap_ch,lv_color_hex(0xFFFF00),0);
  small_btn(ch_row,"+",[](lv_event_t*) {
    if (g_app_context.capture.pcap_locked_ch < 13) g_app_context.capture.pcap_locked_ch++;
    char buf[8]; snprintf(buf,sizeof(buf),"CH %d",g_app_context.capture.pcap_locked_ch);
    lv_label_set_text(lbl_pcap_ch,buf);
  });
  btn_pcap_lock=lv_btn_create(ch_row); lv_obj_set_size(btn_pcap_lock,68,30);
  lv_obj_set_style_bg_color(btn_pcap_lock,lv_color_hex(0x002200),0);
  lv_obj_set_style_border_color(btn_pcap_lock,lv_color_hex(0x00FF88),0);
  lv_obj_set_style_border_width(btn_pcap_lock,1,0);
  lv_obj_add_event_cb(btn_pcap_lock,[](lv_event_t*) {
    g_app_context.capture.pcap_ch_locked = !g_app_context.capture.pcap_ch_locked;
    lv_obj_t *lb=lv_obj_get_child(btn_pcap_lock,0);
    if (g_app_context.capture.pcap_ch_locked) {
      lv_label_set_text(lb,"LOCKED");
      lv_obj_set_style_bg_color(btn_pcap_lock,lv_color_hex(0x2A1500),0);
      lv_obj_set_style_border_color(btn_pcap_lock,lv_color_hex(0xFF8800),0);
    } else {
      lv_label_set_text(lb,"HOP");
      lv_obj_set_style_bg_color(btn_pcap_lock,lv_color_hex(0x002200),0);
      lv_obj_set_style_border_color(btn_pcap_lock,lv_color_hex(0x00FF88),0);
    }
  },LV_EVENT_CLICKED,nullptr);
  lv_obj_t *llock=lv_label_create(btn_pcap_lock); lv_label_set_text(llock,"HOP"); lv_obj_center(llock);

  btn_pcap_start=lv_btn_create(tab_pcap); 
  lv_obj_set_size(btn_pcap_start, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(btn_pcap_start,LV_ALIGN_BOTTOM_MID,0,-50);
  lv_obj_add_style(btn_pcap_start,&style_btn_orange,0);
  lv_obj_add_event_cb(btn_pcap_start,cb_toggle_pcap,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lpcap=lv_label_create(btn_pcap_start); lv_label_set_text(lpcap,"START PCAP"); lv_obj_center(lpcap);
  add_return_btn(tab_pcap);

  // ── Probes Tab ────────────────────────────────
  lv_obj_t *lbl_pr=lv_label_create(tab_probes);
  lv_label_set_text(lbl_pr,"Probe Requests:");
  lv_obj_set_style_text_color(lbl_pr,lv_color_hex(0xFF00FF),0);
  lv_obj_align(lbl_pr,LV_ALIGN_TOP_LEFT,5,5);
  probe_list=lv_textarea_create(tab_probes); lv_obj_set_size(probe_list, SCREEN_W - (UI::Layout::Margin * 2), 195);
  lv_obj_align(probe_list,LV_ALIGN_TOP_MID,0,24);
  lv_textarea_set_text(probe_list, "Waiting for probes...\n");
  lv_obj_clear_flag(probe_list, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  btn_probe_start=lv_btn_create(tab_probes); 
  lv_obj_set_size(btn_probe_start, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(btn_probe_start,LV_ALIGN_BOTTOM_MID,0,-44);
  lv_obj_add_style(btn_probe_start,&style_btn_dark,0);
  lv_obj_add_event_cb(btn_probe_start,cb_toggle_probes,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lpr=lv_label_create(btn_probe_start); lv_label_set_text(lpr,"START PROBE MONITOR"); lv_obj_center(lpr);
  add_return_btn(tab_probes);

  // ── Settings Tab ──────────────────────────────
  lv_obj_set_style_pad_all(tab_settings, 0, 0);
  lv_obj_set_layout(tab_settings, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(tab_settings, col_dsc, row_dsc);

  hub(tab_settings, LV_SYMBOL_EDIT,      "SSIDs",    0, 0, [](lv_event_t*){ navigate_to(9); },  0xAAAAAA);
  hub(tab_settings, LV_SYMBOL_SETTINGS,  "DIAG",     1, 0, [](lv_event_t*){ navigate_to(10); }, UI::Colors::Primary);
  hub(tab_settings, LV_SYMBOL_LIST,      "PERF",     0, 1, [](lv_event_t*){ navigate_to(11); }, UI::Colors::Warning);
  hub(tab_settings, LV_SYMBOL_BATTERY_3, "BATTERY",  1, 1, [](lv_event_t*){ navigate_to(12); }, UI::Colors::Success);
  hub(tab_settings, LV_SYMBOL_EYE_OPEN,  "BRIGHT",   0, 2, [](lv_event_t*){ navigate_to(14); }, UI::Colors::Warning);
  hub(tab_settings, LV_SYMBOL_POWER,     "REBOOT",   1, 2, [](lv_event_t*){ navigate_to(13); }, UI::Colors::Error);
  hub(tab_settings, LV_SYMBOL_HOME,      "HOME",     0, 3, cb_nav_home,                          UI::Colors::Primary);

  // --- Beacon SSID Sub-Tab ---
  lv_obj_set_style_bg_color(beacon_ssid_panel, lv_color_hex(0x050505), 0);
  lv_obj_set_style_border_color(beacon_ssid_panel, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_border_width(beacon_ssid_panel, 2, 0);

  lv_obj_t *lbl_beacon_title = lv_label_create(beacon_ssid_panel);
  lv_label_set_text(lbl_beacon_title, "#AAAAAA " LV_SYMBOL_EDIT " BEACON SSIDs#");
  lv_label_set_recolor(lbl_beacon_title, true);
  lv_obj_align(lbl_beacon_title, LV_ALIGN_TOP_MID, 0, 0);

  ta_beacon_ssids = lv_textarea_create(beacon_ssid_panel);
  lv_obj_set_size(ta_beacon_ssids, SCREEN_W - 20, 110);
  lv_obj_align(ta_beacon_ssids, LV_ALIGN_TOP_MID, 0, 30);
  lv_textarea_set_max_length(ta_beacon_ssids, MAX_BEACON_SSIDS * (MAX_BEACON_SSID_LENGTH + 1));
  lv_textarea_set_placeholder_text(ta_beacon_ssids, "Enter SSIDs (one per line)...");
  lv_textarea_set_one_line(ta_beacon_ssids, false);
  lv_obj_add_event_cb(ta_beacon_ssids, [](lv_event_t *e) {
    const char* text = lv_textarea_get_text(ta_beacon_ssids);
    g_app_context.audit.custom_beacon_ssids.clear();
    
    char current_ssid[MAX_BEACON_SSID_LENGTH + 1] = {0};
    int idx = 0;
    for (int i = 0; text[i] != '\0'; ++i) {
      if (text[i] == '\n') {
        if (idx > 0) {
          if (g_app_context.audit.custom_beacon_ssids.size() < MAX_BEACON_SSIDS) {
            g_app_context.audit.custom_beacon_ssids.push_back(String(current_ssid));
          }
        }
        idx = 0;
        memset(current_ssid, 0, sizeof(current_ssid));
      } else {
        if (idx < MAX_BEACON_SSID_LENGTH) current_ssid[idx++] = text[i];
      }
    }
    if (idx > 0 && g_app_context.audit.custom_beacon_ssids.size() < MAX_BEACON_SSIDS) {
      g_app_context.audit.custom_beacon_ssids.push_back(String(current_ssid));
    }
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  String all_ssids = "";
  for (const String& ssid : g_app_context.audit.custom_beacon_ssids) {
    all_ssids += ssid + "\n";
  }
  lv_textarea_set_text(ta_beacon_ssids, all_ssids.c_str());

  lv_obj_t *btn_save_ssids = lv_btn_create(beacon_ssid_panel); // Corrected to be a single declaration
  lv_obj_set_size(btn_save_ssids, (SCREEN_W - (UI::Layout::Margin * 2) - UI::Layout::Padding) / 2, UI::Layout::ButtonHeight);
  lv_obj_align(btn_save_ssids, LV_ALIGN_BOTTOM_RIGHT, -UI::Layout::Margin, -UI::Layout::Padding);
  lv_obj_add_style(btn_save_ssids, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_save_ssids, [](lv_event_t *e) {
    save_beacon_ssids_to_nvs(&g_app_context);
    lv_label_set_text(lv_obj_get_child(e->target, 0), LV_SYMBOL_OK " SAVED!");
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_save_ssids = lv_label_create(btn_save_ssids); lv_label_set_text(lbl_save_ssids, "SAVE"); lv_obj_center(lbl_save_ssids);

  lv_obj_t *btn_close_beacon = lv_btn_create(beacon_ssid_panel); // Corrected to be a single declaration
  lv_obj_set_size(btn_close_beacon, (SCREEN_W - (UI::Layout::Margin * 2) - UI::Layout::Padding) / 2, UI::Layout::ButtonHeight);
  lv_obj_align(btn_close_beacon, LV_ALIGN_BOTTOM_LEFT, UI::Layout::Margin, -UI::Layout::Padding);
  lv_obj_add_style(btn_close_beacon, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_close_beacon, [](lv_event_t *e) {
      navigate_to(6);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_close_beacon = lv_label_create(btn_close_beacon); lv_label_set_text(lbl_close_beacon, LV_SYMBOL_CLOSE " CLOSE"); lv_obj_center(lbl_close_beacon);

  // --- Diagnostics Sub-Tab ---
  lv_obj_set_style_bg_color(diagnostics_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(diagnostics_panel, lv_color_hex(UI::Colors::Primary), 0);
  lv_obj_set_style_border_width(diagnostics_panel, 2, 0);

  lv_obj_t *lbl_diag_title = lv_label_create(diagnostics_panel);
  lv_label_set_text(lbl_diag_title, "#00FFCC " LV_SYMBOL_SETTINGS " SYSTEM DIAGNOSTICS#");
  lv_label_set_recolor(lbl_diag_title, true);
  lv_obj_align(lbl_diag_title, LV_ALIGN_TOP_MID, 0, 0);

  ta_diagnostics = lv_textarea_create(diagnostics_panel);
  lv_obj_set_size(ta_diagnostics, SCREEN_W - 20, SCREEN_H - 90);
  lv_obj_align(ta_diagnostics, LV_ALIGN_TOP_MID, 0, 30);
  lv_textarea_set_text(ta_diagnostics, "Gathering data...\n");
  lv_obj_clear_flag(ta_diagnostics, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_set_style_text_font(ta_diagnostics, &lv_font_montserrat_14, 0);

  add_settings_back_btn(diagnostics_panel);

  // --- System Stats Sub-Tab ---
  lv_obj_set_style_bg_color(sys_stats_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(sys_stats_panel, lv_color_hex(UI::Colors::Warning), 0);
  lv_obj_set_style_border_width(sys_stats_panel, 2, 0);

  lv_obj_t *lbl_stats_title2 = lv_label_create(sys_stats_panel);
  lv_label_set_text(lbl_stats_title2, "#FFAA00 " LV_SYMBOL_LIST " PERFORMANCE STATS#");
  lv_label_set_recolor(lbl_stats_title2, true);
  lv_obj_align(lbl_stats_title2, LV_ALIGN_TOP_MID, 0, 0);

  ta_sys_stats = lv_textarea_create(sys_stats_panel);
  lv_obj_set_size(ta_sys_stats, SCREEN_W - 20, SCREEN_H - 90);
  lv_obj_align(ta_sys_stats, LV_ALIGN_TOP_MID, 0, 30);
  lv_textarea_set_text(ta_sys_stats, "Gathering data...\n");
  lv_obj_clear_flag(ta_sys_stats, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_set_style_text_font(ta_sys_stats, &lv_font_montserrat_14, 0);

  add_settings_back_btn(sys_stats_panel);

  // --- Battery Stats Sub-Tab ---
  lv_obj_set_style_bg_color(battery_stats_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(battery_stats_panel, lv_color_hex(UI::Colors::Success), 0);
  lv_obj_set_style_border_width(battery_stats_panel, 2, 0);

  lv_obj_t *lbl_batt_title = lv_label_create(battery_stats_panel);
  lv_label_set_text(lbl_batt_title, "#00FF88 " LV_SYMBOL_BATTERY_FULL " DETAILED POWER STATS#");
  lv_label_set_recolor(lbl_batt_title, true);
  lv_obj_align(lbl_batt_title, LV_ALIGN_TOP_MID, 0, 0);

  lbl_battery_stats = lv_label_create(battery_stats_panel);
  lv_label_set_text(lbl_battery_stats, "Reading sensors...");
  lv_label_set_recolor(lbl_battery_stats, true);
  lv_obj_align(lbl_battery_stats, LV_ALIGN_TOP_LEFT, 5, 40);

  // Voltage History Chart
  ui_battery_chart = lv_chart_create(battery_stats_panel);
  lv_obj_set_size(ui_battery_chart, SCREEN_W - 30, 85);
  lv_chart_set_update_mode(ui_battery_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(ui_battery_chart, 30); // Show last 2.5 minutes (30 * 5s)
  lv_chart_set_range(ui_battery_chart, LV_CHART_AXIS_PRIMARY_Y, 3200, 4300);
  lv_chart_set_range(ui_battery_chart, LV_CHART_AXIS_SECONDARY_Y, 50, 300); // Heap in KB
  lv_chart_set_div_line_count(ui_battery_chart, 4, 6);
  
  lv_obj_set_style_bg_color(ui_battery_chart, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_width(ui_battery_chart, 1, 0);
  lv_obj_set_style_line_width(ui_battery_chart, 2, LV_PART_ITEMS);
  ui_battery_series = lv_chart_add_series(ui_battery_chart, lv_color_hex(0x00FF88), LV_CHART_AXIS_PRIMARY_Y);
  ui_heap_series = lv_chart_add_series(ui_battery_chart, lv_color_hex(0x00AAFF), LV_CHART_AXIS_SECONDARY_Y);

  lv_obj_t *btn_reset_cal = lv_btn_create(battery_stats_panel);
  lv_obj_set_size(btn_reset_cal, SCREEN_W - (UI::Layout::Margin * 2), 30);
  lv_obj_align(btn_reset_cal, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_obj_add_style(btn_reset_cal, &style_btn_dark, 0);
  // When the reset button is clicked, show the confirmation dialog
  lv_obj_add_event_cb(btn_reset_cal, [](lv_event_t *e) {
      if (confirm_reset_panel) {
          lv_obj_clear_flag(confirm_reset_panel, LV_OBJ_FLAG_HIDDEN);
      }
  }, LV_EVENT_CLICKED, nullptr);
  lbl_reset_cal_button = lv_label_create(btn_reset_cal); lv_label_set_text(lbl_reset_cal_button, "RESET CALIBRATION"); lv_obj_center(lbl_reset_cal_button);

  add_settings_back_btn(battery_stats_panel);

  // ── Confirmation Dialog for Reset Calibration ──
  confirm_reset_panel = lv_obj_create(battery_stats_panel);
  lv_obj_set_size(confirm_reset_panel, SCREEN_W - 40, 120); // Smaller panel
  lv_obj_center(confirm_reset_panel);
  lv_obj_set_style_bg_color(confirm_reset_panel, lv_color_hex(0x300000), 0); // Dark red background for emphasis
  lv_obj_set_style_bg_opa(confirm_reset_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(confirm_reset_panel, lv_color_hex(UI::Colors::Warning), 0);
  lv_obj_set_style_border_width(confirm_reset_panel, 2, 0);
  lv_obj_set_style_radius(confirm_reset_panel, UI::Layout::CornerRadius, 0);
  lv_obj_set_style_pad_all(confirm_reset_panel, UI::Layout::Padding, 0);
  lv_obj_add_flag(confirm_reset_panel, LV_OBJ_FLAG_HIDDEN); // Hidden by default
  no_scroll(confirm_reset_panel);

  lv_obj_t *lbl_confirm_msg = lv_label_create(confirm_reset_panel);
  lv_label_set_text(lbl_confirm_msg, "#FF4444 Are you sure?#\n#AAAAAA This cannot be undone.#");
  lv_label_set_recolor(lbl_confirm_msg, true);
  lv_obj_align(lbl_confirm_msg, LV_ALIGN_TOP_MID, 0, 5);

  // Yes button
  int btn_width = (SCREEN_W - 40 - UI::Layout::Padding * 3) / 2;
  lv_obj_t *btn_confirm_yes = lv_btn_create(confirm_reset_panel);
  lv_obj_set_size(btn_confirm_yes, btn_width, UI::Layout::ButtonHeight);
  lv_obj_align(btn_confirm_yes, LV_ALIGN_BOTTOM_LEFT, UI::Layout::Padding, -UI::Layout::Padding);
  lv_obj_add_style(btn_confirm_yes, &style_btn_red, 0);
  lv_obj_add_event_cb(btn_confirm_yes, [](lv_event_t *e) {
      reset_battery_calibration();
      lv_label_set_text(lbl_reset_cal_button, LV_SYMBOL_REFRESH " CALIBRATION CLEARED");
      
      // Revert text after 3 seconds
      lv_timer_t * t = lv_timer_create([](lv_timer_t * timer) {
          lv_label_set_text((lv_obj_t*)timer->user_data, "RESET CALIBRATION");
          lv_timer_del(timer);
      }, 3000, lbl_reset_cal_button);

      lv_obj_add_flag(confirm_reset_panel, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_yes = lv_label_create(btn_confirm_yes); lv_label_set_text(lbl_yes, LV_SYMBOL_OK " YES"); lv_obj_center(lbl_yes);

  // No button
  lv_obj_t *btn_confirm_no = lv_btn_create(confirm_reset_panel);
  lv_obj_set_size(btn_confirm_no, btn_width, UI::Layout::ButtonHeight);
  lv_obj_align(btn_confirm_no, LV_ALIGN_BOTTOM_RIGHT, -UI::Layout::Padding, -UI::Layout::Padding);
  lv_obj_add_style(btn_confirm_no, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_confirm_no, [](lv_event_t *e) {
      lv_obj_add_flag(confirm_reset_panel, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_no = lv_label_create(btn_confirm_no); lv_label_set_text(lbl_no, LV_SYMBOL_CLOSE " NO"); lv_obj_center(lbl_no);

  // --- Reboot Sub-Tab ---
  lv_obj_set_style_bg_color(reboot_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(reboot_panel, lv_color_hex(UI::Colors::Error), 0);
  lv_obj_set_style_border_width(reboot_panel, 2, 0);

  lv_obj_t *lbl_reboot_title = lv_label_create(reboot_panel);
  lv_label_set_text(lbl_reboot_title, "#FF4444 " LV_SYMBOL_POWER " REBOOT#");
  lv_label_set_recolor(lbl_reboot_title, true);
  lv_obj_align(lbl_reboot_title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *lbl_reboot_msg = lv_label_create(reboot_panel);
  lv_label_set_recolor(lbl_reboot_msg, true);
  lv_label_set_text(lbl_reboot_msg, "#AAAAAA Reboot the CYD to recover from\\nWiFi/BLE stack issues or apply\\nlow-level changes.#");
  lv_obj_set_width(lbl_reboot_msg, SCREEN_W - (UI::Layout::Margin * 2));
  lv_label_set_long_mode(lbl_reboot_msg, LV_LABEL_LONG_WRAP);
  lv_obj_align(lbl_reboot_msg, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *btn_reboot = lv_btn_create(reboot_panel);
  lv_obj_set_size(btn_reboot, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(btn_reboot, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_obj_add_style(btn_reboot, &style_btn_red, 0);
  lv_obj_add_event_cb(btn_reboot, [](lv_event_t *e) {
      lv_obj_t *btn = lv_event_get_current_target(e);
      if (btn) {
          lv_obj_t *lbl = lv_obj_get_child(btn, 0);
          if (lbl) lv_label_set_text(lbl, "REBOOTING...");
      }
      // Give LVGL a moment to repaint before restarting.
      lv_timer_create([](lv_timer_t *t) {
          (void)t;
          reboot_cyd();
      }, 250, nullptr);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_reboot = lv_label_create(btn_reboot); lv_label_set_text(lbl_reboot, LV_SYMBOL_POWER " REBOOT NOW"); lv_obj_center(lbl_reboot);

  add_settings_back_btn(reboot_panel);

  // --- Brightness Sub-Tab ---
  lv_obj_set_style_bg_color(brightness_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(brightness_panel, lv_color_hex(UI::Colors::Warning), 0);
  lv_obj_set_style_border_width(brightness_panel, 2, 0);

  lv_obj_t *lbl_bright_title = lv_label_create(brightness_panel);
  lv_label_set_text(lbl_bright_title, "#FFFF00 " LV_SYMBOL_EYE_OPEN " BRIGHTNESS#");
  lv_label_set_recolor(lbl_bright_title, true);
  lv_obj_align(lbl_bright_title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *lbl_bright_value = lv_label_create(brightness_panel);
  lv_obj_set_style_text_color(lbl_bright_value, lv_color_hex(0xAAAAAA), 0);
  lv_label_set_text_fmt(lbl_bright_value, "Level: %d", 255);
  lv_obj_align(lbl_bright_value, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *slider_bright = lv_slider_create(brightness_panel);
  lv_obj_set_size(slider_bright, SCREEN_W - (UI::Layout::Margin * 2), 12);
  lv_obj_align(slider_bright, LV_ALIGN_TOP_MID, 0, 70);
  lv_slider_set_range(slider_bright, 10, 255);
  lv_slider_set_value(slider_bright, 255, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_bright, [](lv_event_t *e) {
      lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
      lv_obj_t *slider = lv_event_get_target(e);
      if (!slider) return;
      int v = lv_slider_get_value(slider);
      display_set_backlight((uint8_t)v);
      if (lbl) lv_label_set_text_fmt(lbl, "Level: %d", v);
  }, LV_EVENT_VALUE_CHANGED, lbl_bright_value);

  add_settings_back_btn(brightness_panel);

  // --- LoRa Stats Sub-Tab ---
  lv_obj_set_style_bg_color(lora_stats_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(lora_stats_panel, lv_color_hex(UI::Colors::Primary), 0); // Changed to Primary
  lv_obj_set_style_border_width(lora_stats_panel, 2, 0);
  no_scroll(lora_stats_panel);
  lv_obj_set_style_pad_all(lora_stats_panel, 0, 0);

  lv_obj_t *lbl_stats_title = lv_label_create(lora_stats_panel);
  lv_label_set_text(lbl_stats_title, "#00AAFF " LV_SYMBOL_WIFI " LORA NODE STATISTICS#");
  lv_label_set_recolor(lbl_stats_title, true);
  lv_obj_align(lbl_stats_title, LV_ALIGN_TOP_MID, 0, 0);

  lbl_lora_stats = lv_label_create(lora_stats_panel);
  lv_label_set_text(lbl_lora_stats, "#888888 Waiting for telemetry...#\n\n(Note: The node must send\na Telemetry packet first)");
  lv_label_set_recolor(lbl_lora_stats, true);
  lv_obj_align(lbl_lora_stats, LV_ALIGN_TOP_LEFT, 0, 30);
  
  lv_obj_t *btn_close_stats = lv_btn_create(lora_stats_panel);
  lv_obj_set_size(btn_close_stats, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight); // Standardized size
  lv_obj_add_flag(btn_close_stats, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(btn_close_stats, LV_ALIGN_BOTTOM_MID, 0, -UI::Layout::Padding);
  lv_obj_add_style(btn_close_stats, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_close_stats, [](lv_event_t *e) {
      (void)e;
      if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
      navigate_to(7); // LoRa hub
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_close = lv_label_create(btn_close_stats);
  lv_label_set_text(lbl_close, LV_SYMBOL_LEFT " BACK TO LORA");
  lv_obj_center(lbl_close);

  // --- LoRa Node DB Sub-Tab ---
  lv_obj_set_style_bg_color(lora_nodedb_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(lora_nodedb_panel, lv_color_hex(0xFF00FF), 0); // Keep original color for now
  lv_obj_set_style_border_width(lora_nodedb_panel, 2, 0);
  no_scroll(lora_nodedb_panel);
  lv_obj_set_style_pad_all(lora_nodedb_panel, 0, 0);

  lv_obj_t *lbl_nodedb_title = lv_label_create(lora_nodedb_panel);
  lv_label_set_text(lbl_nodedb_title, "#FF00FF " LV_SYMBOL_LIST " KNOWN MESH NODES#");
  lv_label_set_recolor(lbl_nodedb_title, true);
  lv_obj_align(lbl_nodedb_title, LV_ALIGN_TOP_MID, 0, 0);

  nodedb_list = lv_textarea_create(lora_nodedb_panel);
  lv_obj_set_size(nodedb_list, SCREEN_W - 20, SCREEN_H - 110);
  lv_obj_align(nodedb_list, LV_ALIGN_TOP_MID, 0, 30);
  lv_textarea_set_text(nodedb_list, "Waiting for nodes...\n");
  lv_obj_clear_flag(nodedb_list, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  
  lv_obj_t *btn_close_nodedb = lv_btn_create(lora_nodedb_panel);
  lv_obj_set_size(btn_close_nodedb, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight); // Standardized size
  lv_obj_add_flag(btn_close_nodedb, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(btn_close_nodedb, LV_ALIGN_BOTTOM_MID, 0, -UI::Layout::Padding);
  lv_obj_add_style(btn_close_nodedb, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_close_nodedb, [](lv_event_t *e) {
      (void)e;
      if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
      navigate_to(7); // LoRa hub
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_close_nodedb = lv_label_create(btn_close_nodedb);
  lv_label_set_text(lbl_close_nodedb, LV_SYMBOL_LEFT " BACK TO LORA");
  lv_obj_center(lbl_close_nodedb);

  // --- LoRa Terminal Sub-Tab ---
  lv_obj_set_style_bg_color(lora_log_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(lora_log_panel, lv_color_hex(UI::Colors::Primary), 0); // Changed to Primary
  lv_obj_set_style_border_width(lora_log_panel, 2, 0);
  no_scroll(lora_log_panel);
  lv_obj_set_style_pad_all(lora_log_panel, 0, 0);

  lv_obj_t *lbl_log_title = lv_label_create(lora_log_panel);
  lv_label_set_text(lbl_log_title, "#00AAFF " LV_SYMBOL_FILE " LORA TERMINAL#");
  lv_label_set_recolor(lbl_log_title, true);
  lv_obj_align(lbl_log_title, LV_ALIGN_TOP_MID, 0, 0);

  ta_lora_log = lv_textarea_create(lora_log_panel);
  lv_obj_set_size(ta_lora_log, SCREEN_W - 20, 110);
  lv_obj_align(ta_lora_log, LV_ALIGN_TOP_MID, 0, 25);
  lv_textarea_set_text(ta_lora_log, "Awaiting LoRa serial data...\n");
  lv_obj_clear_flag(ta_lora_log, LV_OBJ_FLAG_CLICK_FOCUSABLE); // Prevent cursor glitching

  ta_lora_input = lv_textarea_create(lora_log_panel);
  lv_obj_set_size(ta_lora_input, SCREEN_W - 90, 40);
  lv_obj_align(ta_lora_input, LV_ALIGN_TOP_LEFT, 5, 140);
  lv_textarea_set_one_line(ta_lora_input, true);

  extern void cb_send_lora(lv_event_t* e);
  lv_obj_t *btn_lora_send = make_action_btn(lora_log_panel, "SEND", &style_btn_blue, 0x00AAFF, cb_send_lora, 140);
  lv_obj_set_size(btn_lora_send, 70, 40);
  lv_obj_align(btn_lora_send, LV_ALIGN_TOP_RIGHT, -5, 140);
  lv_obj_clear_flag(btn_lora_send, LV_OBJ_FLAG_HIDDEN);
  // Footer row: prevents CLOSE/CLEAR overlap across themes/padding changes.
  lv_obj_t *log_footer = lv_obj_create(lora_log_panel);
  lv_obj_add_flag(log_footer, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(log_footer, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(log_footer, LV_ALIGN_BOTTOM_MID, 0, -UI::Layout::Padding);
  lv_obj_set_style_bg_opa(log_footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(log_footer, 0, 0);
  lv_obj_set_style_pad_all(log_footer, 0, 0);
  lv_obj_set_style_pad_gap(log_footer, UI::Layout::Padding, 0);
  lv_obj_set_flex_flow(log_footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(log_footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  no_scroll(log_footer);

  lv_obj_t *btn_close_log = lv_btn_create(log_footer);
  lv_obj_set_size(btn_close_log, 0, UI::Layout::ButtonHeight);
  lv_obj_set_flex_grow(btn_close_log, 1);
  lv_obj_add_style(btn_close_log, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_close_log, [](lv_event_t *e) {
      (void)e;
      if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
      navigate_to(7); // LoRa hub
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_close_log = lv_label_create(btn_close_log); lv_label_set_text(lbl_close_log, LV_SYMBOL_LEFT " BACK"); lv_obj_center(lbl_close_log);

  lv_obj_t *btn_lora_clear = lv_btn_create(log_footer);
  lv_obj_set_size(btn_lora_clear, 0, UI::Layout::ButtonHeight);
  lv_obj_set_flex_grow(btn_lora_clear, 1);
  lv_obj_add_style(btn_lora_clear, &style_btn_red, 0);
  lv_obj_add_event_cb(btn_lora_clear, [](lv_event_t *e) {
      if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          g_app_context.lora.log_data[0] = '\0'; // Wipe the buffer cleanly
          g_app_context.lora.log_updated = true; // Trigger UI refresh
          xSemaphoreGive(g_app_context.lora.mutex);
      }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_lora_clear = lv_label_create(btn_lora_clear); lv_label_set_text(lbl_lora_clear, LV_SYMBOL_TRASH " CLEAR"); lv_obj_center(lbl_lora_clear);

  // --- LoRa Chat Sub-Tab ---
  lv_obj_set_style_bg_color(lora_chat_panel, lv_color_hex(UI::Colors::Background), 0);
  lv_obj_set_style_border_color(lora_chat_panel, lv_color_hex(UI::Colors::Success), 0);
  lv_obj_set_style_border_width(lora_chat_panel, 2, 0);
  no_scroll(lora_chat_panel);
  lv_obj_set_style_pad_all(lora_chat_panel, 0, 0);

  lv_obj_t *lbl_chat_title = lv_label_create(lora_chat_panel);
  lv_label_set_text(lbl_chat_title, "#00FF88 " LV_SYMBOL_KEYBOARD " LORA CHAT#");
  lv_label_set_recolor(lbl_chat_title, true);
  lv_obj_align(lbl_chat_title, LV_ALIGN_TOP_MID, 0, 0);

  ta_lora_chat = lv_textarea_create(lora_chat_panel);
  lv_obj_set_size(ta_lora_chat, SCREEN_W - 20, 110);
  lv_obj_align(ta_lora_chat, LV_ALIGN_TOP_MID, 0, 25);
  lv_textarea_set_text(ta_lora_chat, "No messages yet...\n");
  lv_obj_clear_flag(ta_lora_chat, LV_OBJ_FLAG_CLICK_FOCUSABLE); // Prevent cursor glitching

  ta_lora_chat_input = lv_textarea_create(lora_chat_panel);
  lv_obj_set_size(ta_lora_chat_input, SCREEN_W - 90, 40);
  lv_obj_align(ta_lora_chat_input, LV_ALIGN_TOP_LEFT, 5, 140);
  lv_textarea_set_one_line(ta_lora_chat_input, true);

  extern void cb_send_lora_chat(lv_event_t* e);
  lv_obj_t *btn_lora_chat_send = make_action_btn(lora_chat_panel, "SEND", &style_btn_blue, 0x00FF88, cb_send_lora_chat, 140);
  lv_obj_set_size(btn_lora_chat_send, 70, 40);
  lv_obj_align(btn_lora_chat_send, LV_ALIGN_TOP_RIGHT, -5, 140);
  lv_obj_clear_flag(btn_lora_chat_send, LV_OBJ_FLAG_HIDDEN);
  // Footer row: prevents CLOSE/CLEAR overlap across themes/padding changes.
  lv_obj_t *chat_footer = lv_obj_create(lora_chat_panel);
  lv_obj_add_flag(chat_footer, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(chat_footer, SCREEN_W - (UI::Layout::Margin * 2), UI::Layout::ButtonHeight);
  lv_obj_align(chat_footer, LV_ALIGN_BOTTOM_MID, 0, -UI::Layout::Padding);
  lv_obj_set_style_bg_opa(chat_footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chat_footer, 0, 0);
  lv_obj_set_style_pad_all(chat_footer, 0, 0);
  lv_obj_set_style_pad_gap(chat_footer, UI::Layout::Padding, 0);
  lv_obj_set_flex_flow(chat_footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chat_footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  no_scroll(chat_footer);

  lv_obj_t *btn_close_chat = lv_btn_create(chat_footer);
  lv_obj_set_size(btn_close_chat, 0, UI::Layout::ButtonHeight);
  lv_obj_set_flex_grow(btn_close_chat, 1);
  lv_obj_add_style(btn_close_chat, &style_btn_dark, 0);
  lv_obj_add_event_cb(btn_close_chat, [](lv_event_t *e) {
      (void)e;
      if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
      navigate_to(7); // LoRa hub
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_close_chat = lv_label_create(btn_close_chat); lv_label_set_text(lbl_close_chat, LV_SYMBOL_LEFT " BACK"); lv_obj_center(lbl_close_chat);

  lv_obj_t *btn_lora_chat_clear = lv_btn_create(chat_footer);
  lv_obj_set_size(btn_lora_chat_clear, 0, UI::Layout::ButtonHeight);
  lv_obj_set_flex_grow(btn_lora_chat_clear, 1);
  lv_obj_add_style(btn_lora_chat_clear, &style_btn_red, 0);
  lv_obj_add_event_cb(btn_lora_chat_clear, [](lv_event_t *e) {
      if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          g_app_context.lora.chat_data[0] = '\0'; // Wipe the buffer cleanly
          g_app_context.lora.chat_updated = true; // Trigger UI refresh
          xSemaphoreGive(g_app_context.lora.mutex);
      }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_lora_chat_clear = lv_label_create(btn_lora_chat_clear); lv_label_set_text(lbl_lora_chat_clear, LV_SYMBOL_TRASH " CLEAR"); lv_obj_center(lbl_lora_chat_clear);

  // ── LoRa Tab Hub ──────────────────────────────
  lv_obj_set_style_pad_all(tab_lora, 0, 0);
  lv_obj_set_layout(tab_lora, LV_LAYOUT_GRID);
  static lv_coord_t lora_col_dsc[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
  static lv_coord_t lora_row_dsc[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(tab_lora, lora_col_dsc, lora_row_dsc);

  hub(tab_lora, LV_SYMBOL_FILE, "TERMINAL", 0, 0, [](lv_event_t*) {
      if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          g_app_context.lora.log_updated = true; // Force UI redraw when opened
          xSemaphoreGive(g_app_context.lora.mutex);
      }
      navigate_to(15);
  }, 0x00AAFF);

  hub(tab_lora, LV_SYMBOL_LIST, "NODE DB", 1, 0, [](lv_event_t*) {
      if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          g_app_context.lora.nodedb_updated = true;
          xSemaphoreGive(g_app_context.lora.mutex);
      }
      navigate_to(16);
  }, 0xFF00FF);

  hub(tab_lora, LV_SYMBOL_WIFI, "STATS", 0, 1, [](lv_event_t*) {
      if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          g_app_context.lora.stats.updated = true;
          xSemaphoreGive(g_app_context.lora.mutex);
      }
      navigate_to(17);
  }, 0xFFFF00);

  hub(tab_lora, LV_SYMBOL_KEYBOARD, "CHAT", 1, 1, [](lv_event_t*) {
      if (g_app_context.lora.mutex && xSemaphoreTake(g_app_context.lora.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          g_app_context.lora.chat_updated = true;
          g_app_context.lora.unread_chat = false; // Mark as read
          xSemaphoreGive(g_app_context.lora.mutex);
      }
      navigate_to(18);
  }, 0x00FF88);

  add_return_btn(tab_lora);

  // ── GPS Tab ───────────────────────────────────
  lv_obj_set_style_pad_all(tab_gps, 6, 0);
  lv_obj_t *lbl_gps_title = lv_label_create(tab_gps);
  lv_label_set_text(lbl_gps_title, "#00FF00 " LV_SYMBOL_GPS " LOCAL GPS DATA#");
  lv_label_set_recolor(lbl_gps_title, true);
  lv_obj_align(lbl_gps_title, LV_ALIGN_TOP_MID, 0, 10);

  lbl_gps_data = lv_label_create(tab_gps);
  lv_label_set_text(lbl_gps_data, "#888888 Waiting for GPS fix...#\n\n(Node must be broadcasting\nposition packets)");
  lv_label_set_recolor(lbl_gps_data, true);
  lv_obj_align(lbl_gps_data, LV_ALIGN_TOP_LEFT, 10, 50);

  add_return_btn(tab_gps);

  // Setup Global Keyboard layer for the input
  kb = lv_keyboard_create(main_screen);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  
  auto kb_cb = [](lv_event_t *e) {
      lv_event_code_t code = lv_event_get_code(e);
      lv_obj_t *ta = lv_event_get_target(e);
      if (code == LV_EVENT_FOCUSED) {
          lv_keyboard_set_textarea(kb, ta);
          lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
      } else if (code == LV_EVENT_DEFOCUSED) {
          lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
      } else if (code == LV_EVENT_READY) {
          // Automatically send when the checkmark on the keyboard is pressed
          if (ta == ta_lora_input) {
              extern void cb_send_lora(lv_event_t* e);
              cb_send_lora(e);
          } else if (ta == ta_lora_chat_input) {
              extern void cb_send_lora_chat(lv_event_t* e);
              cb_send_lora_chat(e);
          } else if (ta == ta_beacon_ssids) {
              // Just close keyboard
          }
          lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_state(ta, LV_STATE_FOCUSED);
      }
  };
  lv_obj_add_event_cb(ta_lora_input, kb_cb, LV_EVENT_ALL, nullptr);
  lv_obj_add_event_cb(ta_lora_chat_input, kb_cb, LV_EVENT_ALL, nullptr);
  lv_obj_add_event_cb(ta_beacon_ssids, kb_cb, LV_EVENT_ALL, nullptr);

  // ── Low Battery Alert Overlay ────────────────
  // Created on top layer so it persists over all tabs and modals
  low_batt_border = lv_obj_create(lv_layer_top());
  lv_obj_set_size(low_batt_border, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_opa(low_batt_border, 0, 0); // Transparent center
  lv_obj_set_style_border_color(low_batt_border, lv_color_hex(UI::Colors::Error), 0);
  lv_obj_set_style_border_width(low_batt_border, 4, 0);
  lv_obj_add_flag(low_batt_border, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
  no_scroll(low_batt_border);
  lv_obj_set_style_border_side(low_batt_border, LV_BORDER_SIDE_FULL, 0);

  // ── Temperature Warning Overlay ────────────────
  temp_warning_border = lv_obj_create(lv_layer_top());
  lv_obj_set_size(temp_warning_border, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_opa(temp_warning_border, 0, 0);
  lv_obj_set_style_border_color(temp_warning_border, lv_color_hex(UI::Colors::Warning), 0);
  lv_obj_set_style_border_width(temp_warning_border, 4, 0);
  lv_obj_add_flag(temp_warning_border, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
  no_scroll(temp_warning_border);
  lv_obj_set_style_border_side(temp_warning_border, LV_BORDER_SIDE_FULL, 0);
}
