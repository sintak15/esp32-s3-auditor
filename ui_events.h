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
    UI_EVT_NAVIGATE
};

struct LocalUiEvent {
    LocalUiEventType type;
    char text[96];
    int tab_id;
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
    UiEvent buffer[UI_RING_SIZE];
    volatile uint32_t head = 0; // Write index
    volatile uint32_t tail = 0; // Read index

public:
    // Zero-copy grab: gets a direct pointer to the next available slot in memory
    UiEvent* get_write_slot() {
        uint32_t next_head = (head + 1) % UI_RING_SIZE;
        if (next_head == tail) return nullptr; // Queue Full
        return &buffer[head];
    }
    void commit_write() { head = (head + 1) % UI_RING_SIZE; }

    // Zero-copy read: gets a pointer to the next event
    UiEvent* get_read_slot() {
        if (head == tail) return nullptr; // Queue Empty
        return &buffer[tail];
    }
    void commit_read() { tail = (tail + 1) % UI_RING_SIZE; }
};

extern UiEventQueue ui_queue;

#endif