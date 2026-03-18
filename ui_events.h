#ifndef UI_EVENTS_H
#define UI_EVENTS_H

#include <stdint.h>
#include <string.h>

enum LocalUiEventType {
    UI_EVT_SET_PCAP_BUTTON,
    UI_EVT_SET_BLE_FLOOD_BUTTON,
    UI_EVT_SET_BLE_SNIFF_BUTTON,
    UI_EVT_SET_PROBE_BUTTON,
    UI_EVT_SET_SCAN_PAUSE,
    UI_EVT_SET_PCAP_STATUS,
    UI_EVT_SET_BLE_STATUS,
    UI_EVT_ADD_PROBE_TEXT,
    UI_EVT_CLEAR_PROBE_LIST,
    UI_EVT_NAVIGATE
};

struct LocalUiEvent {
    LocalUiEventType type;
    char text[128];
    uint16_t tab_id;
    uint32_t value_u32;
};

struct UiEvent {
    enum Type {
        ADD_PROBE,
        CLEAR_PROBES,
        SET_BLE_STATUS,
        SET_PCAP_STATUS
    } type;
    
    char text[256];
};

#define UI_RING_SIZE 32

class UiEventQueue {
private:
    UiEvent* buffer = nullptr;
    volatile uint32_t head = 0; // Write index
    volatile uint32_t tail = 0; // Read index

public:
    void init() {
        buffer = (UiEvent*)ps_calloc(UI_RING_SIZE, sizeof(UiEvent));
        if (!buffer) buffer = (UiEvent*)calloc(UI_RING_SIZE, sizeof(UiEvent)); // fallback
    }

    // Zero-copy grab: gets a direct pointer to the next available slot in memory
    UiEvent* get_write_slot() {
        if (!buffer) return nullptr;
        uint32_t next_head = (head + 1) % UI_RING_SIZE;
        if (next_head == tail) return nullptr; // Queue Full
        return &buffer[head];
    }
    void commit_write() { head = (head + 1) % UI_RING_SIZE; }

    // Zero-copy read: gets a pointer to the next event
    UiEvent* get_read_slot() {
        if (!buffer) return nullptr;
        if (head == tail) return nullptr; // Queue Empty
        return &buffer[tail];
    }
    void commit_read() { tail = (tail + 1) % UI_RING_SIZE; }
};

extern UiEventQueue ui_queue;

// Helper to safely queue UI text updates from any Core 0 task
extern void queue_local_ui_text(LocalUiEventType type, const char *text);

#endif