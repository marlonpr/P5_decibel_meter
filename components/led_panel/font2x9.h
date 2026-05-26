// Auto-generated packed 2x9 font
#pragma once
#include <stdint.h>

#define FONT2x9_CHAR_W   2
#define FONT2x9_CHAR_H   9
#define FONT2x9_SPACING  1
#define FONT2x9_FIRST    0x20
#define FONT2x9_LAST     0x23
#define FONT2x9_COUNT    4

// Each glyph = 2 columns.
// Each column uses uint16_t.
// Bit 0 = top row.
static const uint16_t FONT2x9[][FONT2x9_CHAR_W] = {
    {0x0000,0x0000}, // 0x20 ' '
    {0x00CC,0x00CC}, // 0x21 '!'
    {0x0000,0x0000}, // 0x22 '"'
    {0x0000,0x0001}, // 0x23 '#'
};
