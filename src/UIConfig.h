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
        static const uint32_t Primary    = 0x00FF88; // Meshtastic Mint Green
        static const uint32_t Secondary  = 0x444444; // Medium Gray
        static const uint32_t Background = 0x000000; // Black
        static const uint32_t Surface    = 0x212121; // Dark Gray
        static const uint32_t Text       = 0xFFFFFF; // White
        
        static const uint32_t Success    = 0x00FF88; // Green/Mint
        static const uint32_t Warning    = 0xFFFF00; // Yellow
        static const uint32_t Error      = 0xFF4444; // Red
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
        static const char* AppTitle     = "Network Auditor";
        static const char* AuditTask    = "Network Audit";
        static const char* AuditNode    = "Node Auditor";
        static const char* StatusRun    = "Auditing...";
        static const char* LogLabel     = "Activity Logs";
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