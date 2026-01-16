// ============================================================================
// XTP SSD1306 - Minimal Non-Blocking OLED Driver
// ============================================================================
// A lightweight SSD1306 driver that uses xtp_i2c.h for all I2C operations,
// providing automatic device state tracking and non-blocking behavior.
//
// Features:
// - Non-blocking I2C writes with automatic failure detection
// - 6x8 fixed font (same as ssd1306xled_font6x8)
// - Text-only API for simplicity
// - Compatible with existing OLED state machine
//
// Usage:
//   #include "xtp_ssd1306.h"
//   
//   xtp_ssd1306_init(0x3C);           // Initialize display
//   xtp_ssd1306_clear();              // Clear screen
//   xtp_ssd1306_setCursor(0, 0);      // Set cursor position
//   xtp_ssd1306_print("Hello");       // Print text
//
// ============================================================================

#ifndef XTP_SSD1306_H
#define XTP_SSD1306_H

#include <Arduino.h>
#include "xtp_i2c.h"

// Display dimensions
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES  (SSD1306_HEIGHT / 8)

// I2C control bytes
#define SSD1306_COMMAND     0x00
#define SSD1306_DATA        0x40
#define SSD1306_DATA_CONT   0xC0

// SSD1306 Commands
#define SSD1306_DISPLAYOFF          0xAE
#define SSD1306_DISPLAYON           0xAF
#define SSD1306_SETDISPLAYCLOCKDIV  0xD5
#define SSD1306_SETMULTIPLEX        0xA8
#define SSD1306_SETDISPLAYOFFSET    0xD3
#define SSD1306_SETSTARTLINE        0x40
#define SSD1306_CHARGEPUMP          0x8D
#define SSD1306_MEMORYMODE          0x20
#define SSD1306_SEGREMAP            0xA0
#define SSD1306_COMSCANDEC          0xC8
#define SSD1306_SETCOMPINS          0xDA
#define SSD1306_SETCONTRAST         0x81
#define SSD1306_SETPRECHARGE        0xD9
#define SSD1306_SETVCOMDETECT       0xDB
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_NORMALDISPLAY       0xA6
#define SSD1306_INVERTDISPLAY       0xA7
#define SSD1306_SETLOWCOLUMN        0x00
#define SSD1306_SETHIGHCOLUMN       0x10
#define SSD1306_SETPAGEADDR         0xB0
#define SSD1306_COLUMNADDR          0x21
#define SSD1306_PAGEADDR            0x22

// ============================================================================
// 6x8 Font Data (ASCII 32-127)
// ============================================================================
// Each character is 6 bytes wide, 8 pixels tall (1 page)
static const uint8_t PROGMEM xtp_font_6x8[] = {
    // Space (32)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (33)
    0x00, 0x00, 0x5F, 0x00, 0x00, 0x00,
    // " (34)
    0x00, 0x07, 0x00, 0x07, 0x00, 0x00,
    // # (35)
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00,
    // $ (36)
    0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00,
    // % (37)
    0x23, 0x13, 0x08, 0x64, 0x62, 0x00,
    // & (38)
    0x36, 0x49, 0x55, 0x22, 0x50, 0x00,
    // ' (39)
    0x00, 0x05, 0x03, 0x00, 0x00, 0x00,
    // ( (40)
    0x00, 0x1C, 0x22, 0x41, 0x00, 0x00,
    // ) (41)
    0x00, 0x41, 0x22, 0x1C, 0x00, 0x00,
    // * (42)
    0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00,
    // + (43)
    0x08, 0x08, 0x3E, 0x08, 0x08, 0x00,
    // , (44)
    0x00, 0x50, 0x30, 0x00, 0x00, 0x00,
    // - (45)
    0x08, 0x08, 0x08, 0x08, 0x08, 0x00,
    // . (46)
    0x00, 0x60, 0x60, 0x00, 0x00, 0x00,
    // / (47)
    0x20, 0x10, 0x08, 0x04, 0x02, 0x00,
    // 0 (48)
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00,
    // 1 (49)
    0x00, 0x42, 0x7F, 0x40, 0x00, 0x00,
    // 2 (50)
    0x42, 0x61, 0x51, 0x49, 0x46, 0x00,
    // 3 (51)
    0x21, 0x41, 0x45, 0x4B, 0x31, 0x00,
    // 4 (52)
    0x18, 0x14, 0x12, 0x7F, 0x10, 0x00,
    // 5 (53)
    0x27, 0x45, 0x45, 0x45, 0x39, 0x00,
    // 6 (54)
    0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00,
    // 7 (55)
    0x01, 0x71, 0x09, 0x05, 0x03, 0x00,
    // 8 (56)
    0x36, 0x49, 0x49, 0x49, 0x36, 0x00,
    // 9 (57)
    0x06, 0x49, 0x49, 0x29, 0x1E, 0x00,
    // : (58)
    0x00, 0x36, 0x36, 0x00, 0x00, 0x00,
    // ; (59)
    0x00, 0x56, 0x36, 0x00, 0x00, 0x00,
    // < (60)
    0x00, 0x08, 0x14, 0x22, 0x41, 0x00,
    // = (61)
    0x14, 0x14, 0x14, 0x14, 0x14, 0x00,
    // > (62)
    0x41, 0x22, 0x14, 0x08, 0x00, 0x00,
    // ? (63)
    0x02, 0x01, 0x51, 0x09, 0x06, 0x00,
    // @ (64)
    0x32, 0x49, 0x79, 0x41, 0x3E, 0x00,
    // A (65)
    0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00,
    // B (66)
    0x7F, 0x49, 0x49, 0x49, 0x36, 0x00,
    // C (67)
    0x3E, 0x41, 0x41, 0x41, 0x22, 0x00,
    // D (68)
    0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00,
    // E (69)
    0x7F, 0x49, 0x49, 0x49, 0x41, 0x00,
    // F (70)
    0x7F, 0x09, 0x09, 0x01, 0x01, 0x00,
    // G (71)
    0x3E, 0x41, 0x41, 0x51, 0x32, 0x00,
    // H (72)
    0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00,
    // I (73)
    0x00, 0x41, 0x7F, 0x41, 0x00, 0x00,
    // J (74)
    0x20, 0x40, 0x41, 0x3F, 0x01, 0x00,
    // K (75)
    0x7F, 0x08, 0x14, 0x22, 0x41, 0x00,
    // L (76)
    0x7F, 0x40, 0x40, 0x40, 0x40, 0x00,
    // M (77)
    0x7F, 0x02, 0x04, 0x02, 0x7F, 0x00,
    // N (78)
    0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00,
    // O (79)
    0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00,
    // P (80)
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x00,
    // Q (81)
    0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00,
    // R (82)
    0x7F, 0x09, 0x19, 0x29, 0x46, 0x00,
    // S (83)
    0x46, 0x49, 0x49, 0x49, 0x31, 0x00,
    // T (84)
    0x01, 0x01, 0x7F, 0x01, 0x01, 0x00,
    // U (85)
    0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00,
    // V (86)
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00,
    // W (87)
    0x7F, 0x20, 0x18, 0x20, 0x7F, 0x00,
    // X (88)
    0x63, 0x14, 0x08, 0x14, 0x63, 0x00,
    // Y (89)
    0x03, 0x04, 0x78, 0x04, 0x03, 0x00,
    // Z (90)
    0x61, 0x51, 0x49, 0x45, 0x43, 0x00,
    // [ (91)
    0x00, 0x00, 0x7F, 0x41, 0x41, 0x00,
    // \ (92)
    0x02, 0x04, 0x08, 0x10, 0x20, 0x00,
    // ] (93)
    0x41, 0x41, 0x7F, 0x00, 0x00, 0x00,
    // ^ (94)
    0x04, 0x02, 0x01, 0x02, 0x04, 0x00,
    // _ (95)
    0x40, 0x40, 0x40, 0x40, 0x40, 0x00,
    // ` (96)
    0x00, 0x01, 0x02, 0x04, 0x00, 0x00,
    // a (97)
    0x20, 0x54, 0x54, 0x54, 0x78, 0x00,
    // b (98)
    0x7F, 0x48, 0x44, 0x44, 0x38, 0x00,
    // c (99)
    0x38, 0x44, 0x44, 0x44, 0x20, 0x00,
    // d (100)
    0x38, 0x44, 0x44, 0x48, 0x7F, 0x00,
    // e (101)
    0x38, 0x54, 0x54, 0x54, 0x18, 0x00,
    // f (102)
    0x08, 0x7E, 0x09, 0x01, 0x02, 0x00,
    // g (103)
    0x08, 0x14, 0x54, 0x54, 0x3C, 0x00,
    // h (104)
    0x7F, 0x08, 0x04, 0x04, 0x78, 0x00,
    // i (105)
    0x00, 0x44, 0x7D, 0x40, 0x00, 0x00,
    // j (106)
    0x20, 0x40, 0x44, 0x3D, 0x00, 0x00,
    // k (107)
    0x00, 0x7F, 0x10, 0x28, 0x44, 0x00,
    // l (108)
    0x00, 0x41, 0x7F, 0x40, 0x00, 0x00,
    // m (109)
    0x7C, 0x04, 0x18, 0x04, 0x78, 0x00,
    // n (110)
    0x7C, 0x08, 0x04, 0x04, 0x78, 0x00,
    // o (111)
    0x38, 0x44, 0x44, 0x44, 0x38, 0x00,
    // p (112)
    0x7C, 0x14, 0x14, 0x14, 0x08, 0x00,
    // q (113)
    0x08, 0x14, 0x14, 0x18, 0x7C, 0x00,
    // r (114)
    0x7C, 0x08, 0x04, 0x04, 0x08, 0x00,
    // s (115)
    0x48, 0x54, 0x54, 0x54, 0x20, 0x00,
    // t (116)
    0x04, 0x3F, 0x44, 0x40, 0x20, 0x00,
    // u (117)
    0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00,
    // v (118)
    0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00,
    // w (119)
    0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00,
    // x (120)
    0x44, 0x28, 0x10, 0x28, 0x44, 0x00,
    // y (121)
    0x0C, 0x50, 0x50, 0x50, 0x3C, 0x00,
    // z (122)
    0x44, 0x64, 0x54, 0x4C, 0x44, 0x00,
    // { (123)
    0x00, 0x08, 0x36, 0x41, 0x00, 0x00,
    // | (124)
    0x00, 0x00, 0x7F, 0x00, 0x00, 0x00,
    // } (125)
    0x00, 0x41, 0x36, 0x08, 0x00, 0x00,
    // ~ (126)
    0x08, 0x08, 0x2A, 0x1C, 0x08, 0x00,
    // DEL (127) - block character for unknown
    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00,
};

// ============================================================================
// Extended Characters (UTF-8 mapped to custom indices)
// ============================================================================
// These characters are outside ASCII but commonly used
// We map them to indices 128+ for lookup

static const uint8_t PROGMEM xtp_font_extended[] = {
    // ° (degree symbol) - index 0
    0x00, 0x06, 0x09, 0x09, 0x06, 0x00,
    // € (euro) - index 1
    0x14, 0x3E, 0x55, 0x55, 0x41, 0x00,
    // č (c with caron) - index 2
    0x38, 0x45, 0x46, 0x45, 0x20, 0x00,
    // š (s with caron) - index 3
    0x48, 0x55, 0x56, 0x55, 0x20, 0x00,
    // ž (z with caron) - index 4
    0x44, 0x65, 0x56, 0x4D, 0x44, 0x00,
    // Č (C with caron) - index 5
    0x3E, 0x41, 0x42, 0x41, 0x22, 0x00,
    // Š (S with caron) - index 6
    0x46, 0x49, 0x4A, 0x49, 0x31, 0x00,
    // Ž (Z with caron) - index 7
    0x61, 0x53, 0x4A, 0x45, 0x43, 0x00,
};

// Map UTF-8 sequences to extended font index
// Returns: 0-127 for ASCII, 128+ for extended, 127 for unknown
// Also returns bytes consumed via byteCount pointer
// Safe: checks for null terminators before reading continuation bytes
uint8_t xtp_map_char(const char* str, uint8_t* byteCount) {
    uint8_t c = (uint8_t)str[0];
    *byteCount = 1;
    
    // Null terminator
    if (c == 0) return ' ';  // Return space for null
    
    // Standard ASCII (including special chars already in font)
    if (c < 128) {
        // Handle control characters
        if (c == '\n' || c == '\r' || c == '\t') {
            return c;  // Return as-is for special handling
        }
        if (c < 32) return 127;  // Unknown control char -> block
        return c;
    }
    
    // Check for valid UTF-8 continuation byte (0x80-0xBF)
    #define IS_CONT(x) (((x) & 0xC0) == 0x80)
    
    // UTF-8 2-byte sequences (0xC0-0xDF lead byte)
    if ((c & 0xE0) == 0xC0) {
        uint8_t c2 = (uint8_t)str[1];
        if (c2 == 0 || !IS_CONT(c2)) {
            // Truncated or invalid - consume only lead byte
            return 127;
        }
        *byteCount = 2;
        
        // Match specific characters
        if (c == 0xC2 && c2 == 0xB0) return 128;  // ° (degree)
        if (c == 0xC4 && c2 == 0x8C) return 133;  // Č
        if (c == 0xC4 && c2 == 0x8D) return 130;  // č
        if (c == 0xC5 && c2 == 0xA0) return 134;  // Š
        if (c == 0xC5 && c2 == 0xA1) return 131;  // š
        if (c == 0xC5 && c2 == 0xBD) return 135;  // Ž
        if (c == 0xC5 && c2 == 0xBE) return 132;  // ž
        return 127;  // Unknown 2-byte sequence
    }
    
    // UTF-8 3-byte sequences (0xE0-0xEF lead byte)
    if ((c & 0xF0) == 0xE0) {
        uint8_t c2 = (uint8_t)str[1];
        if (c2 == 0 || !IS_CONT(c2)) return 127;
        uint8_t c3 = (uint8_t)str[2];
        if (c3 == 0 || !IS_CONT(c3)) {
            *byteCount = 2;  // Consume what we validated
            return 127;
        }
        *byteCount = 3;
        
        // € (euro) = E2 82 AC
        if (c == 0xE2 && c2 == 0x82 && c3 == 0xAC) return 129;
        return 127;
    }
    
    // UTF-8 4-byte sequences (0xF0-0xF7 lead byte) - skip them
    if ((c & 0xF8) == 0xF0) {
        uint8_t c2 = (uint8_t)str[1];
        if (c2 == 0 || !IS_CONT(c2)) return 127;
        uint8_t c3 = (uint8_t)str[2];
        if (c3 == 0 || !IS_CONT(c3)) { *byteCount = 2; return 127; }
        uint8_t c4 = (uint8_t)str[3];
        if (c4 == 0 || !IS_CONT(c4)) { *byteCount = 3; return 127; }
        *byteCount = 4;
        return 127;  // No 4-byte chars supported
    }
    
    #undef IS_CONT
    
    // Invalid lead byte (0x80-0xBF or 0xF8-0xFF) - skip single byte
    return 127;
}

// Get font data for a mapped character
const uint8_t* xtp_get_font_data(uint8_t mapped) {
    if (mapped >= 32 && mapped <= 127) {
        // Standard ASCII
        return &xtp_font_6x8[(mapped - 32) * 6];
    } else if (mapped >= 128 && mapped < 128 + 8) {
        // Extended character
        return &xtp_font_extended[(mapped - 128) * 6];
    }
    // Unknown - return block character
    return &xtp_font_6x8[(127 - 32) * 6];
}

// ============================================================================
// Driver State
// ============================================================================

struct XTP_SSD1306 {
    uint8_t address = 0x3C;
    uint8_t cursorX = 0;       // Column in pixels (0-127)
    uint8_t cursorPage = 0;    // Page/row (0-7)
    uint8_t lineStartX = 0;    // Starting X for current line (for \n handling)
    bool initialized = false;
    I2CDevice* device = nullptr;
    
    // Statistics
    uint32_t writeCount = 0;
    uint32_t errorCount = 0;
    uint32_t lastWriteTime = 0;  // microseconds for last write
};

static XTP_SSD1306 xtp_oled;

// ============================================================================
// Low-level I2C functions
// ============================================================================

// Send a single command byte
bool xtp_ssd1306_command(uint8_t cmd) {
    uint8_t buffer[2] = { SSD1306_COMMAND, cmd };
    bool ok = i2c_write(xtp_oled.address, buffer, 2);
    if (!ok) xtp_oled.errorCount++;
    else xtp_oled.writeCount++;
    return ok;
}

// Send multiple command bytes
bool xtp_ssd1306_commands(const uint8_t* cmds, size_t len) {
    // Send each command with control byte
    for (size_t i = 0; i < len; i++) {
        if (!xtp_ssd1306_command(cmds[i])) {
            return false;
        }
    }
    return true;
}

// Send data bytes (for display RAM)
bool xtp_ssd1306_data(const uint8_t* data, size_t len) {
    if (len == 0) return true;
    
    // I2C buffer limit - send in chunks
    const size_t MAX_CHUNK = 30;  // Leave room for address + control byte
    uint8_t buffer[32];
    
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = min(MAX_CHUNK, len - sent);
        buffer[0] = SSD1306_DATA;
        memcpy(&buffer[1], &data[sent], chunk);
        
        uint32_t start = micros();
        bool ok = i2c_write(xtp_oled.address, buffer, chunk + 1);
        xtp_oled.lastWriteTime = micros() - start;
        
        if (!ok) {
            xtp_oled.errorCount++;
            return false;
        }
        xtp_oled.writeCount++;
        sent += chunk;
    }
    return true;
}

// ============================================================================
// Public API
// ============================================================================

// Check if display is present and responding (uses cached state for efficiency)
bool xtp_ssd1306_present() {
    return i2c_device_present(xtp_oled.address);
}

// Force an actual I2C probe to detect reconnection (ignores cache)
bool xtp_ssd1306_probe() {
    return i2c_device_probe(xtp_oled.address);
}

// Initialize the display
bool xtp_ssd1306_init(uint8_t address = 0x3C) {
    xtp_oled.address = address;
    xtp_oled.initialized = false;
    xtp_oled.cursorX = 0;
    xtp_oled.cursorPage = 0;
    xtp_oled.writeCount = 0;
    xtp_oled.errorCount = 0;
    
    // Register with I2C bus manager (or get existing registration)
    xtp_oled.device = i2cBus.registerDevice(address, "SSD1306", false);
    
    // Check presence with actual probe (not cached state)
    // This is important for reconnection scenarios
    if (!xtp_ssd1306_probe()) {
        Serial.println("[SSD1306] Display not found");
        return false;
    }
    
    // Initialization sequence for 128x64 display
    static const uint8_t init_cmds[] = {
        SSD1306_DISPLAYOFF,
        SSD1306_SETDISPLAYCLOCKDIV, 0x80,  // Default clock
        SSD1306_SETMULTIPLEX, 0x3F,        // 64 lines
        SSD1306_SETDISPLAYOFFSET, 0x00,
        SSD1306_SETSTARTLINE | 0x00,
        SSD1306_CHARGEPUMP, 0x14,          // Enable charge pump
        SSD1306_MEMORYMODE, 0x00,          // Horizontal addressing
        SSD1306_SEGREMAP | 0x01,           // Flip horizontally
        SSD1306_COMSCANDEC,                // Flip vertically
        SSD1306_SETCOMPINS, 0x12,          // For 128x64
        SSD1306_SETCONTRAST, 0xCF,
        SSD1306_SETPRECHARGE, 0xF1,
        SSD1306_SETVCOMDETECT, 0x40,
        SSD1306_DISPLAYALLON_RESUME,
        SSD1306_NORMALDISPLAY,
        SSD1306_DISPLAYON
    };
    
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        if (!xtp_ssd1306_command(init_cmds[i])) {
            Serial.println("[SSD1306] Init failed");
            return false;
        }
    }
    
    xtp_oled.initialized = true;
    Serial.println("[SSD1306] Initialized");
    return true;
}

// Set cursor position (in character coordinates)
void xtp_ssd1306_setCursor(uint8_t col, uint8_t row) {
    // Convert character position to pixel position
    xtp_oled.cursorX = col * 6;  // 6 pixels per character
    xtp_oled.cursorPage = row;   // Each row is one page (8 pixels)
    xtp_oled.lineStartX = xtp_oled.cursorX;  // Remember starting column for \n
    
    // Clamp
    if (xtp_oled.cursorX >= SSD1306_WIDTH) xtp_oled.cursorX = 0;
    if (xtp_oled.cursorPage >= SSD1306_PAGES) xtp_oled.cursorPage = 0;
}

// Set cursor position (in pixel coordinates) - for compatibility
void xtp_ssd1306_setCursorPixel(uint8_t x, uint8_t page) {
    xtp_oled.cursorX = x;
    xtp_oled.cursorPage = page / 8;  // Convert y to page
    xtp_oled.lineStartX = xtp_oled.cursorX;  // Remember starting column for \n
    
    if (xtp_oled.cursorX >= SSD1306_WIDTH) xtp_oled.cursorX = 0;
    if (xtp_oled.cursorPage >= SSD1306_PAGES) xtp_oled.cursorPage = 0;
}

// Set display addressing window and position
bool xtp_ssd1306_setPosition(uint8_t x, uint8_t page) {
    // Set column address
    if (!xtp_ssd1306_command(SSD1306_COLUMNADDR)) return false;
    if (!xtp_ssd1306_command(x)) return false;
    if (!xtp_ssd1306_command(SSD1306_WIDTH - 1)) return false;
    
    // Set page address  
    if (!xtp_ssd1306_command(SSD1306_PAGEADDR)) return false;
    if (!xtp_ssd1306_command(page)) return false;
    if (!xtp_ssd1306_command(SSD1306_PAGES - 1)) return false;
    
    return true;
}

// Print a single character at current position
bool xtp_ssd1306_printChar(char c) {
    if (!xtp_oled.initialized) return false;
    
    // Get font data for character
    uint8_t charData[6];
    if (c < 32 || c > 127) c = 127;  // Use block for unknown
    
    const uint8_t* fontPtr = &xtp_font_6x8[(c - 32) * 6];
    for (int i = 0; i < 6; i++) {
        charData[i] = pgm_read_byte(&fontPtr[i]);
    }
    
    // Set position and write
    if (!xtp_ssd1306_setPosition(xtp_oled.cursorX, xtp_oled.cursorPage)) {
        return false;
    }
    
    if (!xtp_ssd1306_data(charData, 6)) {
        return false;
    }
    
    // Advance cursor
    xtp_oled.cursorX += 6;
    if (xtp_oled.cursorX >= SSD1306_WIDTH) {
        xtp_oled.cursorX = 0;
        xtp_oled.cursorPage++;
        if (xtp_oled.cursorPage >= SSD1306_PAGES) {
            xtp_oled.cursorPage = 0;
        }
    }
    
    return true;
}

// Print a string at current position (handles \n and UTF-8 extended chars)
bool xtp_ssd1306_print(const char* str) {
    if (!xtp_oled.initialized || !str) return false;
    
    size_t len = strlen(str);
    if (len == 0) return true;
    
    size_t i = 0;
    while (i < len) {
        // Build a buffer of characters until newline or end of line
        uint8_t buffer[128];
        size_t bufLen = 0;
        size_t charsInBuffer = 0;
        bool hitNewline = false;
        
        // Set position for this segment
        if (!xtp_ssd1306_setPosition(xtp_oled.cursorX, xtp_oled.cursorPage)) {
            return false;
        }
        
        size_t maxChars = (SSD1306_WIDTH - xtp_oled.cursorX) / 6;
        if (maxChars == 0) {
            // At end of line, wrap first
            xtp_oled.cursorX = xtp_oled.lineStartX;
            xtp_oled.cursorPage++;
            if (xtp_oled.cursorPage >= SSD1306_PAGES) {
                xtp_oled.cursorPage = 0;
            }
            continue;
        }
        
        while (i < len && charsInBuffer < maxChars && bufLen < sizeof(buffer) - 6) {
            uint8_t byteCount = 1;
            uint8_t mapped = xtp_map_char(&str[i], &byteCount);
            
            // Handle control characters
            if (mapped == '\n') {
                i += byteCount;
                hitNewline = true;
                break;  // Process buffer, then handle newline
            } else if (mapped == '\r') {
                i += byteCount;
                continue;  // Skip carriage return
            } else if (mapped == '\t') {
                // Tab = 4 spaces
                for (int t = 0; t < 4 && charsInBuffer < maxChars && bufLen < sizeof(buffer) - 6; t++) {
                    const uint8_t* fontPtr = xtp_get_font_data(' ');
                    for (int j = 0; j < 6; j++) {
                        buffer[bufLen++] = pgm_read_byte(&fontPtr[j]);
                    }
                    charsInBuffer++;
                }
                i += byteCount;
                continue;
            }
            
            // Get font data for this character
            const uint8_t* fontPtr = xtp_get_font_data(mapped);
            for (int j = 0; j < 6; j++) {
                buffer[bufLen++] = pgm_read_byte(&fontPtr[j]);
            }
            charsInBuffer++;
            i += byteCount;
        }
        
        // Send buffer to display
        if (bufLen > 0) {
            if (!xtp_ssd1306_data(buffer, bufLen)) {
                return false;
            }
            xtp_oled.cursorX += bufLen;
        }
        
        // Handle newline - move to next line at starting column
        if (hitNewline) {
            xtp_oled.cursorX = xtp_oled.lineStartX;
            xtp_oled.cursorPage++;
            if (xtp_oled.cursorPage >= SSD1306_PAGES) {
                xtp_oled.cursorPage = 0;
            }
        }
        
        // Handle line wrap
        if (xtp_oled.cursorX >= SSD1306_WIDTH) {
            xtp_oled.cursorX = xtp_oled.lineStartX;
            xtp_oled.cursorPage++;
            if (xtp_oled.cursorPage >= SSD1306_PAGES) {
                xtp_oled.cursorPage = 0;
            }
        }
    }
    
    return true;
}

// Clear the entire display
bool xtp_ssd1306_clear() {
    if (!xtp_oled.initialized) return false;
    
    // Set full window
    if (!xtp_ssd1306_setPosition(0, 0)) return false;
    
    // Send zeros to clear
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    
    // 128 * 8 = 1024 bytes total
    for (int i = 0; i < 1024 / 32; i++) {
        if (!xtp_ssd1306_data(zeros, 32)) {
            return false;
        }
    }
    
    xtp_oled.cursorX = 0;
    xtp_oled.cursorPage = 0;
    
    return true;
}

// Clear a single line
bool xtp_ssd1306_clearLine(uint8_t line) {
    if (!xtp_oled.initialized || line >= SSD1306_PAGES) return false;
    
    if (!xtp_ssd1306_setPosition(0, line)) return false;
    
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    
    // 128 bytes per line
    for (int i = 0; i < 4; i++) {
        if (!xtp_ssd1306_data(zeros, 32)) {
            return false;
        }
    }
    
    return true;
}

// Draw raw bitmap data at position
bool xtp_ssd1306_drawBuffer(uint8_t x, uint8_t page, uint8_t width, uint8_t height, const uint8_t* data) {
    if (!xtp_oled.initialized || !data) return false;
    
    uint8_t pages = (height + 7) / 8;
    
    for (uint8_t p = 0; p < pages && (page + p) < SSD1306_PAGES; p++) {
        if (!xtp_ssd1306_setPosition(x, page + p)) return false;
        if (!xtp_ssd1306_data(&data[p * width], width)) return false;
    }
    
    return true;
}

// Set display contrast
bool xtp_ssd1306_setContrast(uint8_t contrast) {
    if (!xtp_ssd1306_command(SSD1306_SETCONTRAST)) return false;
    return xtp_ssd1306_command(contrast);
}

// Turn display on/off
bool xtp_ssd1306_displayOn(bool on) {
    return xtp_ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
}

// Invert display
bool xtp_ssd1306_invert(bool invert) {
    return xtp_ssd1306_command(invert ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
}

// ============================================================================
// Status API
// ============================================================================

bool xtp_ssd1306_isInitialized() { return xtp_oled.initialized; }
bool xtp_ssd1306_isPresent() { return xtp_oled.device ? xtp_oled.device->isPresent() : false; }
I2CDevice* xtp_ssd1306_getDevice() { return xtp_oled.device; }
uint32_t xtp_ssd1306_getWriteCount() { return xtp_oled.writeCount; }
uint32_t xtp_ssd1306_getErrorCount() { return xtp_oled.errorCount; }
uint32_t xtp_ssd1306_getLastWriteTime() { return xtp_oled.lastWriteTime; }

void xtp_ssd1306_resetStats() {
    xtp_oled.writeCount = 0;
    xtp_oled.errorCount = 0;
}

// Get status as JSON
void xtp_ssd1306_status_json(char* buffer, size_t size) {
    snprintf(buffer, size,
        "{\"initialized\":%s,\"present\":%s,\"writes\":%lu,\"errors\":%lu,\"lastWriteUs\":%lu}",
        xtp_oled.initialized ? "true" : "false",
        xtp_ssd1306_isPresent() ? "true" : "false",
        xtp_oled.writeCount,
        xtp_oled.errorCount,
        xtp_oled.lastWriteTime
    );
}

// ============================================================================
// Compatibility layer for ssd1306.h API
// ============================================================================

// Style constants (ignored, we only have one style)
#define STYLE_NORMAL  0
#define STYLE_BOLD    1
#define STYLE_ITALIC  2

// These provide drop-in compatibility with the original ssd1306 library
#define ssd1306_128x64_i2c_init()      xtp_ssd1306_init(0x3C)
#define ssd1306_clearScreen()          xtp_ssd1306_clear()
#define ssd1306_setCursor(x, y)        xtp_ssd1306_setCursorPixel(x, y)
#define ssd1306_print(s)               xtp_ssd1306_print(s)
#define ssd1306_setFixedFont(f)        /* No-op, we use built-in font */
#define ssd1306_drawBuffer(x,y,w,h,d)  xtp_ssd1306_drawBuffer(x, (y)/8, w, h, d)

// ssd1306_printFixed(x, y, text, style) - prints at pixel position
// We ignore the style parameter since we only support one font
inline bool ssd1306_printFixed(uint8_t x, uint8_t y, const char* text, uint8_t style) {
    (void)style;  // Unused
    xtp_ssd1306_setCursorPixel(x, y);
    return xtp_ssd1306_print(text);
}

#endif // XTP_SSD1306_H
