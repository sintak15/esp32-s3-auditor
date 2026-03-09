//===================================================================
// TFT_eSPI User_Setup.h for 2.8inch IPS ESP32-S3 Display (ES3C28P)
// Source: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
//===================================================================
#define USER_SETUP_INFO "ES3C28P_2_8_IPS_ESP32S3"

// ==================================================================
// Section 1. Display Driver
// ==================================================================
// Wiki confirms ILI9341V driver IC
#define ILI9341_DRIVER

// ==================================================================
// Section 1.5. Hardware SPI Port
// ==================================================================
// Force HSPI to avoid conflict with ESP32-S3 internal OPI flash on FSPI
#define USE_HSPI_PORT

// ==================================================================
// Section 2. Pin Configuration
// ==================================================================
// --- Display (SPI) ---
#define TFT_CS   10   // IO10 - LCD chip select, active low
#define TFT_DC   46   // IO46 - LCD data/command select
#define TFT_MOSI 11   // IO11 - LCD SPI write data
#define TFT_SCLK 12   // IO12 - LCD SPI clock
#define TFT_MISO 13   // IO13 - LCD SPI read data
#define TFT_RST  -1   // Hardwired to EN pin (shared with ESP32-S3 reset)
#define TFT_BL   45   // IO45 - Backlight control (HIGH = on)
#define TFT_BACKLIGHT_ON HIGH

// --- Touch Screen (I2C) ---
// CORRECTED: was SDA=1(TX!), SCL=9, RST=4 — all wrong
#define TOUCH_SDA  16  // IO16 - Capacitive touch I2C data
#define TOUCH_SCL  15  // IO15 - Capacitive touch I2C clock
#define TOUCH_RST  18  // IO18 - Touch reset, active low
#define TOUCH_INT  17  // IO17 - Touch interrupt, active low on touch event

// ==================================================================
// Section 3. Fonts
// ==================================================================
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ==================================================================
// Section 4. SPI Frequencies
// ==================================================================
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000