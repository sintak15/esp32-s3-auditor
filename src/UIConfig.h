#ifndef UI_CONFIG_H
#define UI_CONFIG_H

#include <stdint.h>

/**
 * @brief UI Design Tokens for the ESP32-S3 Auditor.
 * Use these constants instead of hardcoded pixel values to ensure
 * a uniform look and feel across different hardware models.
 */
namespace UI {

    // --- Color Palette (RGB565) ---
    namespace Colors {
        static const uint16_t Primary   = 0x07E0; // Meshtastic Green
        static const uint16_t Secondary = 0x0410; // Dark Green
        static const uint16_t Background = 0x0000; // Black
        static const uint16_t Surface    = 0x2104; // Dark Gray
        static const uint16_t Text       = 0xFFFF; // White
        static const uint16_t Warning    = 0xF800; // Red
    }

    // --- Layout & Uniformity ---
    namespace Layout {
        static const int Padding        = 8;  // Standard gap between elements
        static const int PaddingInner   = 4;  // Tight gap for grouped items
        static const int Margin         = 12; // Gap from screen edge
        static const int CornerRadius   = 4;  // Uniform rounding for buttons/panels
        
        // Header/Footer heights
        static const int HeaderHeight   = 28;
        static const int FooterHeight   = 22;

        // Control Sizing
        static const int ButtonHeight   = 36;
        static const int ButtonPaddingH = 16; // Horizontal internal padding
        static const int ListItemHeight = 40; // Uniform height for menu/list items
        static const int IconSize       = 16;
    }

    // --- Typography ---
    namespace Type {
        static const int SizeSmall      = 1; // Scaling factor or index
        static const int SizeNormal     = 2;
        static const int SizeLarge      = 3;
    }

    // --- Soft Terminology ---
    namespace Labels {
        static const char* AppTitle     = "System Auditor";
        static const char* AuditTask    = "Network Audit";
        static const char* AuditNode    = "Node Auditor";
        static const char* StatusRun    = "Auditing...";
        static const char* LogLabel     = "Audit Logs";
    }

    /**
     * @brief Helper to calculate a centered Y position for an element
     */
    inline int getCenterY(int screenHeight, int elementHeight) {
        return (screenHeight - elementHeight) / 2;
    }

    /**
     * @brief Helper to calculate a centered X position for an element
     */
    inline int getCenterX(int screenWidth, int elementWidth) {
        return (screenWidth - elementWidth) / 2;
    }
}

#endif // UI_CONFIG_H