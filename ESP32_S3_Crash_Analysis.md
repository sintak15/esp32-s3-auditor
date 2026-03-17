# ESP32-S3 Pentester Crash Analysis & Fix Guide

## Executive Summary
Your ESP32-S3 is experiencing **heap memory exhaustion** causing repeated crashes and reboots. The root cause is **LVGL memory fragmentation** combined with **slow UI rendering** that's blocking the watchdog timer.

---

## Critical Issues Found

### 1. **LVGL Buffer Allocation Problem** ⚠️ HIGHEST PRIORITY
**Location:** `display.cpp` line 150-151

**Current Code:**
```cpp
uint32_t buf_pixels = SCREEN_W * 20;  // 240 * 20 = 4,800 pixels
lvgl_buf1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

**Problem:**
- Allocating **4,800 pixels × 2 bytes = 9,600 bytes** from internal RAM
- Your internal RAM is only **~242KB total**, with only **97KB free** after setup
- LVGL is consuming nearly **10% of your free heap** just for the draw buffer
- This is causing the heap to drop from 97KB to 53KB during operation

**The Crash Sequence:**
1. System starts with 97KB free heap
2. LVGL allocates 9.6KB + UI objects consume ~40KB
3. LoRa task allocates 16KB stack
4. WiFi/BLE operations allocate dynamic buffers
5. Heap fragments and drops to 52KB minimum
6. **CRITICAL:** Largest free block shrinks to 53KB → memory allocations start failing
7. System crashes with memory errors

---

### 2. **Slow LVGL Timer Handler** ⏱️
**Observed Performance:**
```
[DIAG] slow: lv_timer_handler 54-183 ms  ← Should be <20ms!
```

**Why This Matters:**
- LVGL needs to complete rendering in **<16ms for 60Hz** (or <33ms for 30Hz)
- Your timer is taking **54-183ms** = 3-11x too slow
- This blocks the `main_app_task` from feeding the watchdog timer
- Eventually triggers watchdog timeout → crash/reboot

**Root Causes:**
- Large UI updates (LoRa logs, node lists, chat) happening inside the timer
- Excessive string operations (strlen, strcpy, strcat) on 2KB+ buffers
- `lv_textarea_set_text()` calls with 1900+ character strings

---

### 3. **Memory Fragmentation** 📊
**Progression from logs:**
```
Initial:  [LVGL] frag=1%   (healthy)
Mid-run:  [LVGL] frag=5%   (warning)
Crash:    [LVGL] frag=19%  (critical!)
```

**What's Happening:**
- UI objects are being created and destroyed
- String buffers (log_data, chat_data) are repeatedly cleared/filled
- LVGL memory allocator can't find contiguous blocks
- `free_biggest=1564 bytes` when it should be >8KB

---

### 4. **Breadcrumb Overflow Risk** 🍞
**Location:** `esp32-s3-pentester.ino` line 717

**Current Code:**
```cpp
char bc_buf[4096];  // 4KB allocated on STACK!

void add_bc(const char* msg) {
    if (strlen(bc_buf) + strlen(msg) + 2 > sizeof(bc_buf)) {
        strcpy(bc_buf, "");
    }
    strcat(bc_buf, msg);
    strcat(bc_buf, "\n");
}
```

**Problems:**
1. **4KB static buffer** in main task eats stack space
2. Called frequently (`add_bc("lv_timer pre")` etc.)
3. Stack overflow risk if main_app_task stack < 16KB
4. Unnecessary overhead - breadcrumbs are diagnostic only

---

## The Fix (Step-by-Step)

### **FIX 1: Move LVGL Buffer to PSRAM** (Critical)

**Edit `display.cpp` lines 146-157:**

```cpp
void display_init() {
    // ... existing code ...
    
    lv_init();
    tft.setSwapBytes(true);

    // CRITICAL FIX: Allocate LVGL buffer in PSRAM, NOT internal RAM
    // You have 8MB of PSRAM with only 36KB used - plenty of space!
    uint32_t buf_pixels = SCREEN_W * 20;  // 4,800 pixels
    
    // Try PSRAM first (you have 8MB available!)
    lvgl_buf1 = (lv_color_t *)ps_malloc(buf_pixels * sizeof(lv_color_t));
    if (!lvgl_buf1) {
        Serial.println("[FATAL] PSRAM alloc failed - cannot continue");
        while(1) delay(1000);
    }
    
    Serial.printf("[UI] Allocated %u bytes in PSRAM for LVGL buffer\n", 
                  buf_pixels * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, nullptr, buf_pixels);
    // ... rest of code ...
}
```

**Expected Impact:**
- Frees up **9.6KB of internal RAM**
- Heap should stay above **100KB** instead of dropping to 52KB
- Eliminates primary crash cause

---

### **FIX 2: Reduce LVGL Timer Load** (High Priority)

**Edit `display.cpp` - Optimize `ui_update_tick()`:**

**Problem Area 1: LoRa Log Updates (lines 246-258)**
```cpp
// BEFORE (BAD):
if (strlen(ui_context->lora.log_data) > 1900) { 
    strcpy(ui_context->lora.log_data, "--- Buffer Cleared ---\n"); 
}
lv_textarea_set_text(ta_lora_log, ui_context->lora.log_data);

// AFTER (FIXED):
// Only update if buffer changed AND throttle updates
static uint32_t last_log_hash = 0;
uint32_t current_hash = strlen(ui_context->lora.log_data); // Simple hash
if (current_hash != last_log_hash && millis() - last_log_draw > 500) { // 500ms throttle
    if (strlen(ui_context->lora.log_data) > 1900) { 
        strcpy(ui_context->lora.log_data, "--- Buffer Cleared ---\n"); 
    }
    lv_textarea_set_text(ta_lora_log, ui_context->lora.log_data);
    last_log_hash = current_hash;
    last_log_draw = millis();
}
```

**Problem Area 2: Node DB Updates (lines 307-332)**
```cpp
// BEFORE (BAD):
if (millis() - last_nodedb_draw > 2000) {
    static char ndb_buf[2048];  // 2KB on stack!
    ndb_buf[0] = '\0';
    for (const auto& n : ui_context->lora.known_nodes) {
        // Heavy string operations in loop
    }
    lv_textarea_set_text(nodedb_list, ndb_buf);
}

// AFTER (FIXED):
// Move to PSRAM and reduce update frequency
static char* ndb_buf = nullptr;
if (!ndb_buf) ndb_buf = (char*)ps_malloc(2048); // Allocate once in PSRAM

if (millis() - last_nodedb_draw > 5000) { // Increase to 5 seconds
    ndb_buf[0] = '\0';
    size_t len = 0;
    for (const auto& n : ui_context->lora.known_nodes) {
        if (len > 1800) break; // Safety limit
        char buf[128];
        int written = snprintf(buf, sizeof(buf), "%s\n!%08lx  %lus ago  SNR: %.1f\n\n",
            (n.long_name[0] != '\0') ? n.long_name : "Unknown",
            (unsigned long)n.num, (unsigned long)((millis() - n.last_heard) / 1000), n.snr);
        if (len + written < 2000) {
            strcat(ndb_buf, buf);
            len += written;
        }
    }
    lv_textarea_set_text(nodedb_list, ndb_buf);
    last_nodedb_draw = millis();
}
```

---

### **FIX 3: Disable/Reduce Breadcrumbs** (Medium Priority)

**Option A: Disable Completely (Recommended for Production)**

Edit `esp32-s3-pentester.ino`:
```cpp
void add_bc(const char* msg) {
    return; // Disabled - breadcrumbs were for debugging only
}
```

**Option B: Keep but Reduce Buffer Size**
```cpp
// Change from 4096 to 512 bytes
char bc_buf[512];

void add_bc(const char* msg) {
    size_t msg_len = strlen(msg);
    if (strlen(bc_buf) + msg_len + 2 > sizeof(bc_buf)) {
        // Keep only last 256 bytes
        memmove(bc_buf, bc_buf + 256, sizeof(bc_buf) - 256);
        bc_buf[sizeof(bc_buf) - 256] = '\0';
    }
    strcat(bc_buf, msg);
    strcat(bc_buf, "\n");
}
```

---

### **FIX 4: Increase Main Task Stack** (Safety Measure)

**Edit `esp32-s3-pentester.ino` line 838:**

```cpp
// BEFORE:
xTaskCreatePinnedToCore(main_app_task, "main_app_task", 16384, NULL, 1, NULL, 1);

// AFTER:
xTaskCreatePinnedToCore(main_app_task, "main_app_task", 20480, NULL, 1, NULL, 1);
//                                                         ^^^^^ Increase from 16KB to 20KB
```

**Why:**
- LoRa task already uses 16KB stack
- Main task has breadcrumbs (4KB), local buffers, and function call overhead
- 20KB provides safety margin

---

### **FIX 5: Add Memory Guard Rails** (Proactive Monitoring)

**Add to `main_app_task()` after line 962:**

```cpp
// Memory emergency brake
if (ESP.getFreeHeap() < 40000) { // Less than 40KB free
    Serial.println("[EMERGENCY] Heap critically low - stopping all tasks");
    stop_pentest(&g_app_context);
    stop_ble(&g_app_context);
    stop_pcap(&g_app_context);
    stop_probe_sniffer(&g_app_context);
    
    // Force garbage collection
    lv_obj_clean(tabview);
    lv_mem_defrag();
    
    Serial.printf("[RECOVERY] Heap after cleanup: %u bytes\n", ESP.getFreeHeap());
}
```

---

## Additional Optimizations

### **OPTIONAL: Reduce LVGL Memory Usage**

Add to `lv_conf.h` (or create it if it doesn't exist):
```cpp
#define LV_MEM_SIZE (48 * 1024U)  // Reduce from 64KB to 48KB
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC ps_malloc  // Use PSRAM instead of internal RAM
```

### **OPTIONAL: Reduce Buffer Sizes**

Edit `constants.h`:
```cpp
#define PCAP_QUEUE_SIZE 50      // Reduce from 100 to 50
#define PROBE_QUEUE_SIZE 25     // Reduce from 50 to 25
#define BLE_RING_SIZE 5         // Reduce from 10 to 5
```

---

## Testing Checklist

After applying fixes, verify:

1. **Heap Stability:**
   ```
   [DIAG] heap should stay >90KB (was dropping to 52KB)
   [DIAG] min should stay >85KB
   ```

2. **LVGL Performance:**
   ```
   [DIAG] slow: lv_timer_handler should be <30ms (was 54-183ms)
   ```

3. **Fragmentation:**
   ```
   [LVGL] frag should stay <10% (was reaching 19%)
   [LVGL] free_biggest should be >5KB (was 1564 bytes)
   ```

4. **No Crashes:**
   - Run for 10+ minutes
   - Switch between all tabs
   - Start/stop PCAP, BLE sniff, probe capture
   - Should NOT see `Backtrace:` or `Rebooting...`

---

## Priority Implementation Order

1. **FIX 1** (PSRAM buffer) - Do this FIRST, it's the primary crash cause
2. **FIX 4** (Increase stack) - Quick safety measure
3. **FIX 3** (Disable breadcrumbs) - Easy win, frees stack space
4. **FIX 2** (Optimize UI tick) - Improves performance
5. **FIX 5** (Memory guards) - Safety net

---

## Expected Results

**Before Fixes:**
- Heap: 97KB → 52KB (crashes)
- LVGL frag: 1% → 19% (crashes)
- lv_timer: 54-183ms (too slow)
- Uptime: ~2-5 minutes before crash

**After Fixes:**
- Heap: 107KB → 95KB (stable)
- LVGL frag: 1% → 5% (acceptable)
- lv_timer: <25ms (smooth)
- Uptime: Hours/days

---

## Files to Modify

1. `display.cpp` - LVGL buffer allocation (lines 146-157)
2. `display.cpp` - UI update optimization (lines 246-332)
3. `esp32-s3-pentester.ino` - Stack size (line 838)
4. `esp32-s3-pentester.ino` - Breadcrumbs (line 717 or disable)
5. `esp32-s3-pentester.ino` - Memory guard (after line 962)

---

## Monitoring Commands

Add these to help diagnose if issues persist:

```cpp
// In setup() after SD card init:
Serial.printf("[STARTUP] Internal RAM: Total=%u Free=%u MinFree=%u\n",
    ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
Serial.printf("[STARTUP] PSRAM: Total=%u Free=%u MinFree=%u\n",
    ESP.getPsramSize(), ESP.getFreePsram(), ESP.getMinFreePsram());

// In main_app_task() diagnostics (every 10 seconds instead of 1):
if (millis() - last_diag_ms > 10000) {
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("[STACK] main_app_task has %u bytes free (total 20480)\n", 
                  stack_high_water);
}
```

---

## Root Cause Summary

Your ESP32-S3 is crashing because:
1. LVGL draw buffer in internal RAM (9.6KB) is too large
2. UI update operations are too slow and memory-intensive
3. Memory fragmentation prevents new allocations
4. Watchdog timeout occurs when heap is exhausted

The fix is to move heavy allocations to PSRAM (you have 8MB unused!) and optimize the UI update loop.
