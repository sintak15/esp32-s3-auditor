#include "ui_module.h"
#include "sd_logger.h"
#include <Arduino.h>

// Instantiate UI Handles
lv_obj_t *main_screen, *status_bar, *tabview;
lv_obj_t *lbl_sd, *lbl_wifi, *lbl_gps_fix, *lbl_batt, *lbl_batt_pct;
lv_obj_t *tab_home, *tab_scan, *tab_pentest, *tab_gps;
lv_obj_t *tab_ble, *tab_pcap, *tab_probes;
lv_obj_t *lbl_gps_info;
lv_obj_t *scan_list, *lbl_scan_count, *btn_scan_pause, *lbl_scan_pause;
lv_obj_t *btn_view_ap, *btn_view_sta, *btn_view_linked;
lv_obj_t *lbl_pt_target, *lbl_pt_bssid, *lbl_pt_status;
lv_obj_t *btn_deauth, *btn_beacon, *btn_pmkid, *btn_stop_pt;
lv_obj_t *lbl_ble_status;
lv_obj_t *lbl_pcap_status;
lv_obj_t *lbl_pcap_ch;
lv_obj_t *btn_pcap_lock;
lv_obj_t *probe_list;
lv_obj_t *btn_ble_sniff  = nullptr;
lv_obj_t *btn_ble_flood  = nullptr;
lv_obj_t *btn_pcap_start = nullptr;
lv_obj_t *btn_probe_start = nullptr;

lv_style_t style_btn_dark, style_btn_red, style_btn_orange,
           style_btn_blue, style_view_active, style_view_inactive;

bool pcap_ch_locked = false;
uint8_t pcap_locked_ch = 1;

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

lv_obj_t* make_atk_btn(lv_obj_t *parent, const char *label, lv_style_t *style,
                        uint32_t tc, lv_event_cb_t cb, int y) {
  lv_obj_t *b=lv_btn_create(parent); lv_obj_set_size(b, SCREEN_W-20, 36);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y); lv_obj_add_style(b, style, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l=lv_label_create(b); lv_label_set_text(l, label);
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
  lv_obj_set_size(status_bar, SCREEN_W, 32); lv_obj_set_pos(status_bar, 0, 0);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_border_width(status_bar, 0, 0);
  lv_obj_set_style_radius(status_bar, 0, 0);
  lv_obj_set_style_pad_all(status_bar, 4, 0);
  no_scroll(status_bar);

  lv_obj_t *title=lv_label_create(status_bar);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS " PENTEST");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00FFCC), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

  lv_obj_t *icont=lv_obj_create(status_bar);
  lv_obj_set_size(icont, 130, 24); lv_obj_align(icont, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(icont, 0, 0); lv_obj_set_style_border_width(icont, 0, 0);
  lv_obj_set_flex_flow(icont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(icont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(icont, 6, 0);
  no_scroll(icont);

  lbl_gps_fix=lv_label_create(icont); lv_label_set_text(lbl_gps_fix, LV_SYMBOL_GPS);
  lv_obj_set_style_text_color(lbl_gps_fix, lv_color_hex(0x444444), 0);
  lbl_wifi=lv_label_create(icont);    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xFFFFFF), 0);
  lbl_sd=lv_label_create(icont);      lv_label_set_text(lbl_sd, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_color(lbl_sd, sd_card_ready()?lv_color_hex(0x00FF88):lv_color_hex(0xFF4444), 0);
  lbl_batt=lv_label_create(icont);    lv_label_set_text(lbl_batt, LV_SYMBOL_BATTERY_FULL);
  lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0xFFFFFF), 0);
  lbl_batt_pct=lv_label_create(icont); lv_label_set_text(lbl_batt_pct, "--%");
  lv_obj_set_style_text_color(lbl_batt_pct, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(lbl_batt_pct, &lv_font_montserrat_14, 0);

  // ── Tabview ───────────────────────────────────
  tabview=lv_tabview_create(main_screen, LV_DIR_TOP, 0);
  lv_obj_set_size(tabview, SCREEN_W, SCREEN_H-32);
  lv_obj_align(tabview, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), 0);
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
  tab_pentest =lv_tabview_add_tab(tabview, "Pentest");
  tab_gps     =lv_tabview_add_tab(tabview, "GPS");
  tab_ble     =lv_tabview_add_tab(tabview, "BLE");
  tab_pcap    =lv_tabview_add_tab(tabview, "PCAP");
  tab_probes  =lv_tabview_add_tab(tabview, "Probes");

  lv_obj_t *all_tabs[]={tab_home,tab_scan,tab_pentest,tab_gps,tab_ble,tab_pcap,tab_probes,tv_cont};
  for (int i=0; i<8; i++) no_scroll(all_tabs[i]);

  // ── Home Hub 3x2 ──────────────────────────────
  lv_obj_set_layout(tab_home, LV_LAYOUT_GRID);
  static lv_coord_t col_dsc[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
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
    lv_obj_set_style_text_color(li,lv_color_hex(col),0);
    lv_obj_t *lt=lv_label_create(b); lv_label_set_text(lt,txt);
    lv_obj_set_style_text_color(lt,lv_color_hex(0xCCCCCC),0);
  };
  hub(tab_home,LV_SYMBOL_WIFI,      "SCAN",    0,0,cb_nav_scan,                                               0x00FFCC);
  hub(tab_home,LV_SYMBOL_WARNING,   "PENTEST", 1,0,[](lv_event_t*){ navigate_to(2); },0xFF4444);
  hub(tab_home,LV_SYMBOL_GPS,       "GPS",     0,1,[](lv_event_t*){ navigate_to(3); },0x00AAFF);
  hub(tab_home,LV_SYMBOL_BLUETOOTH, "BLE",     1,1,[](lv_event_t*){ navigate_to(4); },0x4444FF);
  hub(tab_home,LV_SYMBOL_FILE,      "PCAP",    0,2,[](lv_event_t*){ navigate_to(5); },0xFFFF00);
  hub(tab_home,LV_SYMBOL_EYE_OPEN,  "PROBES",  1,2,[](lv_event_t*){ navigate_to(6); },0xFF00FF);

  // ── Shared return-home button builder ─────────
  auto add_return_btn=[](lv_obj_t *parent) {
    lv_obj_t *b=lv_btn_create(parent); lv_obj_set_size(b,SCREEN_W-20,35);
    lv_obj_align(b,LV_ALIGN_BOTTOM_MID,0,-4); lv_obj_add_style(b,&style_btn_dark,0);
    lv_obj_add_event_cb(b,cb_nav_home,LV_EVENT_CLICKED,nullptr);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,LV_SYMBOL_HOME " RETURN HOME");
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
  lv_obj_add_style(btn_view_ap,&style_view_active,0);

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
  lv_obj_set_style_border_color(btn_scan_pause,lv_color_hex(0x00FFCC),0);
  lv_obj_set_style_border_width(btn_scan_pause,1,0);
  lv_obj_add_event_cb(btn_scan_pause,cb_pause_scan,LV_EVENT_CLICKED,nullptr);
  lbl_scan_pause=lv_label_create(btn_scan_pause);
  lv_label_set_text(lbl_scan_pause,LV_SYMBOL_PLAY " START");
  lv_obj_set_style_text_color(lbl_scan_pause,lv_color_hex(0x00FFCC),0);
  lv_obj_center(lbl_scan_pause);

  // ── Pentest Tab ───────────────────────────────
  lv_obj_set_style_pad_all(tab_pentest,6,0);
  lbl_pt_target=lv_label_create(tab_pentest);
  lv_label_set_recolor(lbl_pt_target,true);
  lv_label_set_text(lbl_pt_target,"#888888 Select a target from SCAN#");
  lv_obj_set_width(lbl_pt_target,SCREEN_W-20);
  lv_label_set_long_mode(lbl_pt_target,LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(lbl_pt_target,LV_ALIGN_TOP_MID,0,2);
  lbl_pt_bssid=lv_label_create(tab_pentest);
  lv_obj_set_style_text_color(lbl_pt_bssid,lv_color_hex(0x555555),0);
  lv_obj_set_style_text_font(lbl_pt_bssid,&lv_font_montserrat_14,0);
  lv_label_set_text(lbl_pt_bssid,"---");
  lv_obj_align_to(lbl_pt_bssid,lbl_pt_target,LV_ALIGN_OUT_BOTTOM_MID,0,2);
  lbl_pt_status=lv_label_create(tab_pentest);
  lv_label_set_recolor(lbl_pt_status,true);
  lv_obj_set_style_text_font(lbl_pt_status,&lv_font_montserrat_14,0);
  lv_label_set_text(lbl_pt_status,"#444444 IDLE#");
  lv_obj_set_width(lbl_pt_status,SCREEN_W-20);
  lv_obj_align_to(lbl_pt_status,lbl_pt_bssid,LV_ALIGN_OUT_BOTTOM_MID,0,4);
  int y=62;
  btn_deauth=make_atk_btn(tab_pentest,LV_SYMBOL_WARNING   " DEAUTH TEST",   &style_btn_red,   0xFF4444,cb_start_deauth,y); y+=42;
  btn_beacon=make_atk_btn(tab_pentest,LV_SYMBOL_AUDIO     " BEACON FLOOD",  &style_btn_orange,0xFFAA00,cb_start_beacon,y); y+=42;
  btn_pmkid =make_atk_btn(tab_pentest,LV_SYMBOL_EYE_OPEN  " PMKID CAPTURE",&style_btn_blue,  0x00AAFF,cb_start_pmkid, y);
  btn_stop_pt=lv_btn_create(tab_pentest);
  lv_obj_set_size(btn_stop_pt,SCREEN_W-20,38); lv_obj_align(btn_stop_pt,LV_ALIGN_BOTTOM_MID,0,-52);
  lv_obj_set_style_bg_color(btn_stop_pt,lv_color_hex(0x1A0000),0);
  lv_obj_set_style_border_color(btn_stop_pt,lv_color_hex(0xFF0000),0);
  lv_obj_set_style_border_width(btn_stop_pt,2,0);
  lv_obj_add_event_cb(btn_stop_pt,cb_stop_pentest,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *sl2=lv_label_create(btn_stop_pt); lv_label_set_text(sl2,LV_SYMBOL_STOP " STOP");
  lv_obj_set_style_text_color(sl2,lv_color_hex(0xFF0000),0); lv_obj_center(sl2);
  lv_obj_add_flag(btn_stop_pt,LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *bbk=lv_btn_create(tab_pentest); lv_obj_set_size(bbk,SCREEN_W-20,36);
  lv_obj_align(bbk,LV_ALIGN_BOTTOM_MID,0,-8); lv_obj_add_style(bbk,&style_btn_dark,0);
  lv_obj_add_event_cb(bbk,cb_nav_scan,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lbbk=lv_label_create(bbk); lv_label_set_text(lbbk,LV_SYMBOL_LEFT " BACK TO SCAN"); lv_obj_center(lbbk);

  // ── GPS Tab ───────────────────────────────────
  lv_obj_set_style_pad_all(tab_gps,10,0);
  lv_obj_t *gtitle=lv_label_create(tab_gps);
  lv_label_set_recolor(gtitle,true);
  lv_label_set_text(gtitle,"#00AAFF " LV_SYMBOL_GPS " GPS TRACKER#");
  lv_obj_align(gtitle,LV_ALIGN_TOP_MID,0,4);
  lbl_gps_info=lv_label_create(tab_gps);
  lv_label_set_recolor(lbl_gps_info,true);
  lv_label_set_long_mode(lbl_gps_info,LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_gps_info,SCREEN_W-20);
  lv_label_set_text(lbl_gps_info,"#888888 Waiting for signal...#\n\nOpen sky view needed.");
  lv_obj_align(lbl_gps_info,LV_ALIGN_TOP_LEFT,0,32);
  add_return_btn(tab_gps);

  // ── BLE Tab ───────────────────────────────────
  lbl_ble_status=lv_label_create(tab_ble);
  lv_label_set_recolor(lbl_ble_status,true);
  lv_label_set_text(lbl_ble_status,"#00FFCC BLE READY#\n\nSelect an action.");
  lv_obj_align(lbl_ble_status,LV_ALIGN_TOP_LEFT,5,5);
  lv_obj_set_width(lbl_ble_status,SCREEN_W-10);
  lv_label_set_long_mode(lbl_ble_status,LV_LABEL_LONG_WRAP);
  btn_ble_sniff=lv_btn_create(tab_ble); lv_obj_set_size(btn_ble_sniff,SCREEN_W-20,40);
  lv_obj_align(btn_ble_sniff,LV_ALIGN_BOTTOM_MID,0,-96);
  lv_obj_add_style(btn_ble_sniff,&style_btn_dark,0);
  lv_obj_add_event_cb(btn_ble_sniff,cb_toggle_ble_sniff,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *ls=lv_label_create(btn_ble_sniff); lv_label_set_text(ls,"START BLE SNIFF"); lv_obj_center(ls);
  btn_ble_flood=lv_btn_create(tab_ble); lv_obj_set_size(btn_ble_flood,SCREEN_W-20,40);
  lv_obj_align(btn_ble_flood,LV_ALIGN_BOTTOM_MID,0,-50);
  lv_obj_add_style(btn_ble_flood,&style_btn_red,0);
  lv_obj_add_event_cb(btn_ble_flood,cb_toggle_ble_flood,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lsp=lv_label_create(btn_ble_flood); lv_label_set_text(lsp,"START BLE FLOOD"); lv_obj_center(lsp);
  add_return_btn(tab_ble);

  // ── PCAP Tab ──────────────────────────────────
  lbl_pcap_status=lv_label_create(tab_pcap);
  lv_label_set_recolor(lbl_pcap_status,true);
  lv_label_set_text(lbl_pcap_status,"#FFFF00 PCAP CAPTURE#\n\nReady. Saves .pcap to SD.");
  lv_obj_align(lbl_pcap_status,LV_ALIGN_TOP_LEFT,5,5);
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
    lv_obj_t *b=lv_btn_create(p); lv_obj_set_size(b,36,30);
    lv_obj_add_style(b,&style_btn_dark,0);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
    lv_obj_t *lb=lv_label_create(b); lv_label_set_text(lb,l); lv_obj_center(lb);
    return b;
  };
  small_btn(ch_row,"-",[](lv_event_t*) {
    if (pcap_locked_ch>1) pcap_locked_ch--;
    char buf[8]; snprintf(buf,sizeof(buf),"CH %d",pcap_locked_ch);
    lv_label_set_text(lbl_pcap_ch,buf);
  });
  lbl_pcap_ch=lv_label_create(ch_row);
  lv_label_set_text(lbl_pcap_ch,"CH 1");
  lv_obj_set_style_text_color(lbl_pcap_ch,lv_color_hex(0xFFFF00),0);
  small_btn(ch_row,"+",[](lv_event_t*) {
    if (pcap_locked_ch<13) pcap_locked_ch++;
    char buf[8]; snprintf(buf,sizeof(buf),"CH %d",pcap_locked_ch);
    lv_label_set_text(lbl_pcap_ch,buf);
  });
  btn_pcap_lock=lv_btn_create(ch_row); lv_obj_set_size(btn_pcap_lock,68,30);
  lv_obj_set_style_bg_color(btn_pcap_lock,lv_color_hex(0x002200),0);
  lv_obj_set_style_border_color(btn_pcap_lock,lv_color_hex(0x00FF88),0);
  lv_obj_set_style_border_width(btn_pcap_lock,1,0);
  lv_obj_add_event_cb(btn_pcap_lock,[](lv_event_t*) {
    pcap_ch_locked=!pcap_ch_locked;
    lv_obj_t *lb=lv_obj_get_child(btn_pcap_lock,0);
    if (pcap_ch_locked) {
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

  btn_pcap_start=lv_btn_create(tab_pcap); lv_obj_set_size(btn_pcap_start,SCREEN_W-20,40);
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
  probe_list=lv_list_create(tab_probes);
  lv_obj_set_size(probe_list,SCREEN_W-10,195);
  lv_obj_align(probe_list,LV_ALIGN_TOP_MID,0,24);
  lv_obj_set_style_bg_color(probe_list,lv_color_hex(0x080808),0);
  lv_obj_set_style_border_width(probe_list,0,0);
  lv_obj_set_style_pad_all(probe_list,2,0);
  btn_probe_start=lv_btn_create(tab_probes); lv_obj_set_size(btn_probe_start,SCREEN_W-20,38);
  lv_obj_align(btn_probe_start,LV_ALIGN_BOTTOM_MID,0,-44);
  lv_obj_add_style(btn_probe_start,&style_btn_dark,0);
  lv_obj_add_event_cb(btn_probe_start,cb_toggle_probes,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *lpr=lv_label_create(btn_probe_start); lv_label_set_text(lpr,"START PROBE SNIFF"); lv_obj_center(lpr);
  add_return_btn(tab_probes);
}