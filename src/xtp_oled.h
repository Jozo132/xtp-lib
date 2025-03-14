#pragma once

#include <Arduino.h>
#include <ssd1306.h>
#include "xtp_i2c.h"

#define OLED_COLS (128 / 6)
#define OLED_ROWS (64 / 8)
#define OLED_CHARS (OLED_COLS * OLED_ROWS)

char _oled_data_active[OLED_CHARS + 1] = { 0 };
char _oled_data_new[OLED_CHARS + 1] = { 0 };
char _oled_toDraw[OLED_CHARS + 1] = { 0 };

void oled_setup() {
#ifndef DISABLE_OLED
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_128x64_i2c_init();
    ssd1306_clearScreen();
    // ssd1306_printFixed (0,  8, "Line 1. Normal text", STYLE_NORMAL);
    // ssd1306_printFixed (0, 16, "Line 2. Bold text", STYLE_BOLD);
    // ssd1306_printFixed (0, 24, "Line 3. Italic text", STYLE_ITALIC);
    // ssd1306_printFixedN (0, 32, "Line 4. Double size", STYLE_BOLD, FONT_SIZE_2X);

#endif
}
bool oled_force_redraw = false;
void oled_draw() {
#ifndef DISABLE_OLED
    uint32_t t = micros();
    if (oled_force_redraw) {
        oled_force_redraw = false;
        for (int i = 0; i < OLED_CHARS; i++) {
            _oled_data_active[i] = '~';
        }
    }
    for (int i = 0; i < OLED_CHARS; i++) {
        char a = _oled_data_active[i];
        char b = _oled_data_new[i];
        if (a == b) continue;
        _oled_data_active[i] = b;
        int x = i % OLED_COLS;
        int y = i / OLED_COLS;

        // Use internal for loop to find how many consecutive characters are different, so we can draw them all at once
        int j = i;
        int unchanged = 0;
        while (j < OLED_CHARS) {
            if (_oled_data_active[j] == _oled_data_new[j]) unchanged++;
            else unchanged = 0;
            if (unchanged >= 2) break;

            int new_y = j / OLED_COLS;
            if (new_y != y) break; // If we are on a new line, stop
            _oled_toDraw[j - i] = _oled_data_new[j];
            _oled_data_active[j] = _oled_data_new[j];
            j++;
        }
        _oled_toDraw[j - i] = 0;
        i = j - 1;

        // Serial.printf("Drawing message [ %d, %d ]: \"%s\"\n", x, y, _oled_toDraw);
        ssd1306_setCursor(x * 6, y * 8);
        ssd1306_print(_oled_toDraw);
    }
    uint32_t elapsed = micros() - t;
    // Serial.printf("OLED sync took %d us\n", elapsed);
#endif
}

void displayMsg(const char* msg) {
#ifndef DISABLE_OLED
    int len = strlen(msg);
    for (int i = 0; i < len; i++) {
        if (i >= OLED_CHARS) break;
        _oled_data_new[i] = msg[i];
    }
    oled_draw();
#endif
}



void oled_print(const char* msg, int x, int y) {
#ifndef DISABLE_OLED
    int len = strlen(msg);
    int index = x + y * OLED_COLS;
    for (int i = 0; i < len; i++) {
        if (index >= OLED_CHARS) break;
        _oled_data_new[index++] = msg[i];
    }
    oled_draw();
#endif
}