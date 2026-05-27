#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led_panel.h"

#include "font6x9.h"
#include "font5x7.h"
#include "font5x5.h"
#include "font10x15.h"
#include "font2x9.h"
#include "font3x5.h"


Hub75Config make_config()
{
    Hub75Config config{};

    config.panel_width  = 64;
    config.panel_height = 32;	

    config.scan_wiring = Hub75ScanWiring::SCAN_1_8_32PX_FULL;
    config.shift_driver = Hub75ShiftDriver::FM6126A;
	
    config.double_buffer = true;	
	
	// N×M grid — virtual display
	config.layout_rows  = 1;
	config.layout_cols  = 1;

	config.layout = Hub75PanelLayout::TOP_LEFT_DOWN_ZIGZAG;	

//============================ HUB75 DevKitC-1 ================================

	
	// Upper RGB
	config.pins.r1 = 4;
	config.pins.g1 = 5;
	config.pins.b1 = 6;
	
	// Lower RGB
	config.pins.r2 = 7;
	config.pins.g2 = 15;
	config.pins.b2 = 16;
	
	// Address
	config.pins.a = 11;
	config.pins.b = 12;
	config.pins.c = 13;
	config.pins.d = -1;
	config.pins.e = -1;
	
	// Control
	config.pins.lat = 9;
	config.pins.oe  = 10;
	config.pins.clk = 8;


//============================================================================================





/*
//============================ HUB75 ETH Development Board ================================

// Upper RGB
config.pins.r1 = 33;
config.pins.g1 = 34;
config.pins.b1 = 35;

// Lower RGB
config.pins.r2 = 36;
config.pins.g2 = 37;
config.pins.b2 = 38;

// Address
config.pins.a = 1;
config.pins.b = 2;
config.pins.c = 15;
config.pins.d = -1;
config.pins.e = -1;

// Control
config.pins.lat = 16;
config.pins.oe  = 21;
config.pins.clk = 47;

//============================================================================================
*/

    return config;
}

int draw_char(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable,
              uint8_t bg_r,
              uint8_t bg_g,
              uint8_t bg_b)
{
    if (ch < FONT6x9_FIRST || ch > FONT6x9_LAST) {
        ch = '?';
    }

    const uint16_t* glyph = FONT6x9[ch - FONT6x9_FIRST]; // adjust size for different font 8, 16 or 32

    for (int col = 0; col < FONT6x9_CHAR_W; col++) {
        uint16_t bits = glyph[col];						// adjust size for different font 8, 16 or 32

        for (int row = 0; row < FONT6x9_CHAR_H; row++) {
            if (bits & (1U << row)) {   					// adjust size for different font,16 use if (bits & (1U << row)) {,  if uint32 use 		if (bits & (1UL << row)) {
                drv.set_pixel(x + col, y + row, r, g, b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }

    if (bg_enable) {
        for (int row = 0; row < FONT6x9_CHAR_H; row++) {
            drv.set_pixel(x + FONT6x9_CHAR_W, y + row, bg_r, bg_g, bg_b);
        }
    }

    return FONT6x9_CHAR_W + FONT6x9_SPACING;
}

int draw_string(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable,
                uint8_t bg_r,
                uint8_t bg_g,
                uint8_t bg_b)
{
    if (!str) {
        return x;
    }

    while (*str) {

        // Use '#' as custom 1-pixel spacer
        if (*str == '#') {
            if (bg_enable) {
                for (int col = 0; col < 1; col++) {
                    for (int row = 0; row < FONT6x9_CHAR_H; row++) {
                        drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
                    }
                }
            }

            x += 1;
            str++;
            continue;
        }

        x += draw_char(drv,
                       x,
                       y,
                       *str++,
                       r,
                       g,
                       b,
                       bg_enable,
                       bg_r,
                       bg_g,
                       bg_b);
    }

    return x;
}

// ======================================================
// Scroll text
// ======================================================

typedef struct {
    char text[96];
    float x;
    int y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    bool active;
    TickType_t last_tick;
    int speed_px_per_sec;
} scroll_state_t;

static scroll_state_t scroll_state = {};

static int text_pixel_width(const char *text)
{
    if (!text) {
        return 0;
    }

    int len = strlen(text);

    if (len <= 0) {
        return 0;
    }

    return len * (FONT6x9_CHAR_W + FONT6x9_SPACING);  // adjust size for different font
}

void scroll_start(const char *text,
                  int y,
                  uint8_t r,
                  uint8_t g,
                  uint8_t b,
                  int speed_px_per_sec)
{
    if (!text) {
        return;
    }

    strncpy(scroll_state.text, text, sizeof(scroll_state.text) - 1);
    scroll_state.text[sizeof(scroll_state.text) - 1] = '\0';

    scroll_state.x = 64.0f;   // Start from right side of 64x32 panel
    scroll_state.y = y;

    scroll_state.r = r;
    scroll_state.g = g;
    scroll_state.b = b;

    scroll_state.speed_px_per_sec = speed_px_per_sec;
    scroll_state.last_tick = xTaskGetTickCount();
    scroll_state.active = true;
}

void scroll_stop(void)
{
    scroll_state.active = false;
}

bool scroll_is_active(void)
{
    return scroll_state.active;
}

void scroll_update(Hub75Driver& drv)
{
    if (!scroll_state.active) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed_ticks = now - scroll_state.last_tick;

    float dt = (float)elapsed_ticks / (float)configTICK_RATE_HZ;

    scroll_state.last_tick = now;

    scroll_state.x -= scroll_state.speed_px_per_sec * dt;

    draw_string(drv,
                (int)scroll_state.x,
                scroll_state.y,
                scroll_state.text,
                scroll_state.r,
                scroll_state.g,
                scroll_state.b);

    int width = text_pixel_width(scroll_state.text);

    if (scroll_state.x < -width) {
        scroll_state.active = false;
    }
}







void scroll_start_if_needed(const char *text,
                            int y,
                            uint8_t r,
                            uint8_t g,
                            uint8_t b,
                            int speed_px_per_sec)
{
    if (!text) {
        return;
    }

    bool different_text = strcmp(scroll_state.text, text) != 0;
    bool different_y = scroll_state.y != y;
    bool different_color =
        scroll_state.r != r ||
        scroll_state.g != g ||
        scroll_state.b != b;

    bool different_speed = scroll_state.speed_px_per_sec != speed_px_per_sec;

    if (!scroll_state.active ||
        different_text ||
        different_y ||
        different_color ||
        different_speed) {
        scroll_start(text, y, r, g, b, speed_px_per_sec);
    }
}


// ======================================================
// =========================FONT5x7==============================
// ======================================================


int draw_char_5x7(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable,
              uint8_t bg_r,
              uint8_t bg_g,
              uint8_t bg_b)
{
    if (ch < FONT5x7_FIRST || ch > FONT5x7_LAST) { 					// adjust size for different font
        ch = '?';
    }

    const uint8_t* glyph = FONT5x7[ch - FONT5x7_FIRST]; 			// adjust size for different font 8, 16 or 32

    for (int col = 0; col < FONT5x7_CHAR_W; col++) {				// adjust size for different font
        uint8_t bits = glyph[col];									// adjust size for different font 8, 16 or 32

        for (int row = 0; row < FONT5x7_CHAR_H; row++) {			// adjust size for different font
            if (bits & (1 << row)) {								// adjust size for different font,for 8 use if (bits & (1 << row)) { , 16 use if (bits & (1U << row)) {,  if uint32 use 		if (bits & (1UL << row)) {
                drv.set_pixel(x + col, y + row, r, g, b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }

    if (bg_enable) {
        for (int row = 0; row < FONT5x7_CHAR_H; row++) {				// adjust size for different font
            drv.set_pixel(x + FONT5x7_CHAR_W, y + row, bg_r, bg_g, bg_b);	// adjust size for different font
        }
    }

    return FONT5x7_CHAR_W + FONT5x7_SPACING;								// adjust size for different font
	
	
						// !! Also modify draw string and for different scrolling modify ->  text_width return len * (FONT6x9_CHAR_W + FONT6x9_SPACING);  !! 		================
}

int draw_string_5x7(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable,
                uint8_t bg_r,
                uint8_t bg_g,
                uint8_t bg_b)
{
    if (!str) {
        return x;
    }

    while (*str) {
        x += draw_char_5x7(drv,  		// adjust size for different font
                       x,
                       y,
                       *str++,
                       r,
                       g,
                       b,
                       bg_enable,
                       bg_r,
                       bg_g,
                       bg_b);
    }

    return x;
}


// ======================================================
// =========================FONT5X5==============================
// ======================================================


int draw_char_5x5(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable,
              uint8_t bg_r,
              uint8_t bg_g,
              uint8_t bg_b)
{
    if (ch < FONT5x5_FIRST || ch > FONT5x5_LAST) { 					// adjust size for different font
        ch = '?';
    }

    const uint8_t* glyph = FONT5x5[ch - FONT5x5_FIRST]; 			// adjust size for different font 8, 16 or 32

    for (int col = 0; col < FONT5x5_CHAR_W; col++) {				// adjust size for different font
        uint8_t bits = glyph[col];									// adjust size for different font 8, 16 or 32

        for (int row = 0; row < FONT5x5_CHAR_H; row++) {			// adjust size for different font
            if (bits & (1 << row)) {								// adjust size for different font,for 8 use if (bits & (1 << row)) { , 16 use if (bits & (1U << row)) {,  if uint32 use 		if (bits & (1UL << row)) {
                drv.set_pixel(x + col, y + row, r, g, b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }

    if (bg_enable) {
        for (int row = 0; row < FONT5x5_CHAR_H; row++) {				// adjust size for different font
            drv.set_pixel(x + FONT5x5_CHAR_W, y + row, bg_r, bg_g, bg_b);	// adjust size for different font
        }
    }

    return FONT5x5_CHAR_W + FONT5x5_SPACING;								// adjust size for different font
	
	
						// !! Also modify draw string and for different scrolling modify ->  text_width return len * (FONT6x9_CHAR_W + FONT6x9_SPACING);  !! 		================
}

int draw_string_5x5(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable,
                uint8_t bg_r,
                uint8_t bg_g,
                uint8_t bg_b)
{
    if (!str) {
        return x;
    }

    while (*str) {
        x += draw_char_5x5(drv,
                       x,
                       y,
                       *str++,
                       r,
                       g,
                       b,
                       bg_enable,
                       bg_r,
                       bg_g,
                       bg_b);
    }

    return x;
}

// ======================================================
// =========================FONT10X15==============================
// ======================================================

int draw_char_10x15(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable,
              uint8_t bg_r,
              uint8_t bg_g,
              uint8_t bg_b)
{
    if (ch < FONT10x15_FIRST || ch > FONT10x15_LAST) { 					// adjust size for different font
        ch = '?';
    }

    const uint16_t* glyph = FONT10x15[ch - FONT10x15_FIRST]; 			// adjust size for different font 8, 16 or 32

    for (int col = 0; col < FONT10x15_CHAR_W; col++) {				// adjust size for different font
        uint16_t bits = glyph[col];									// adjust size for different font 8, 16 or 32

        for (int row = 0; row < FONT10x15_CHAR_H; row++) {			// adjust size for different font
            if (bits & (1U << row)) {								// adjust size for different font,for 8 use if (bits & (1 << row)) { , 16 use if (bits & (1U << row)) {,  if uint32 use 		if (bits & (1UL << row)) {
                drv.set_pixel(x + col, y + row, r, g, b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }

    if (bg_enable) {
        for (int row = 0; row < FONT10x15_CHAR_H; row++) {				// adjust size for different font
            drv.set_pixel(x + FONT10x15_CHAR_W, y + row, bg_r, bg_g, bg_b);	// adjust size for different font
        }
    }

    return FONT10x15_CHAR_W + FONT10x15_SPACING;								// adjust size for different font
	
	
						// !! Also modify draw string and for different scrolling modify ->  text_width return len * (FONT6x9_CHAR_W + FONT6x9_SPACING);  !! 		================
}



int draw_string_10x15(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable,
                uint8_t bg_r,
                uint8_t bg_g,
                uint8_t bg_b)
{
    if (!str) {
        return x;
    }

    while (*str) {
        x += draw_char_10x15(drv,
                       x,
                       y,
                       *str++,
                       r,
                       g,
                       b,
                       bg_enable,
                       bg_r,
                       bg_g,
                       bg_b);
    }

    return x;
}

// ======================================================
// =========================FONT2X9==============================
// ======================================================

int draw_char_2x9(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable,
              uint8_t bg_r,
              uint8_t bg_g,
              uint8_t bg_b)
{
    if (ch < FONT2x9_FIRST || ch > FONT2x9_LAST) { 					// adjust size for different font
        ch = '?';
    }

    const uint16_t* glyph = FONT2x9[ch - FONT2x9_FIRST]; 			// adjust size for different font 8, 16 or 32

    for (int col = 0; col < FONT2x9_CHAR_W; col++) {				// adjust size for different font
        uint16_t bits = glyph[col];									// adjust size for different font 8, 16 or 32

        for (int row = 0; row < FONT2x9_CHAR_H; row++) {			// adjust size for different font
            if (bits & (1U << row)) {								// adjust size for different font,for 8 use if (bits & (1 << row)) { , 16 use if (bits & (1U << row)) {,  if uint32 use 		if (bits & (1UL << row)) {
                drv.set_pixel(x + col, y + row, r, g, b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }

    if (bg_enable) {
        for (int row = 0; row < FONT2x9_CHAR_H; row++) {				// adjust size for different font
            drv.set_pixel(x + FONT2x9_CHAR_W, y + row, bg_r, bg_g, bg_b);	// adjust size for different font
        }
    }

    return FONT2x9_CHAR_W + FONT2x9_SPACING;								// adjust size for different font
	
	
						// !! Also modify draw string and for different scrolling modify ->  text_width return len * (FONT6x9_CHAR_W + FONT6x9_SPACING);  !! 		================
}



int draw_string_2x9(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable,
                uint8_t bg_r,
                uint8_t bg_g,
                uint8_t bg_b)
{
    if (!str) {
        return x;
    }

    while (*str) {
        x += draw_char_2x9(drv,
                       x,
                       y,
                       *str++,
                       r,
                       g,
                       b,
                       bg_enable,
                       bg_r,
                       bg_g,
                       bg_b);
    }

    return x;
}


// ======================================================
// =========================FONT3X5==============================
// ======================================================

int draw_char_3x5(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable,
              uint8_t bg_r,
              uint8_t bg_g,
              uint8_t bg_b)
{
    if (ch < FONT3x5_FIRST || ch > FONT3x5_LAST) { 					// adjust size for different font
        ch = '?';
    }

    const uint8_t* glyph = FONT3x5[ch - FONT3x5_FIRST]; 			// adjust size for different font 8, 16 or 32

    for (int col = 0; col < FONT3x5_CHAR_W; col++) {				// adjust size for different font
        uint8_t bits = glyph[col];									// adjust size for different font 8, 16 or 32

        for (int row = 0; row < FONT3x5_CHAR_H; row++) {			// adjust size for different font
            if (bits & (1 << row)) {								// adjust size for different font,for 8 use if (bits & (1 << row)) { , 16 use if (bits & (1U << row)) {,  if uint32 use 		if (bits & (1UL << row)) {
                drv.set_pixel(x + col, y + row, r, g, b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }

    if (bg_enable) {
        for (int row = 0; row < FONT3x5_CHAR_H; row++) {				// adjust size for different font
            drv.set_pixel(x + FONT3x5_CHAR_W, y + row, bg_r, bg_g, bg_b);	// adjust size for different font
        }
    }

    return FONT3x5_CHAR_W + FONT3x5_SPACING;								// adjust size for different font
	
	
						// !! Also modify draw string and for different scrolling modify ->  text_width return len * (FONT6x9_CHAR_W + FONT6x9_SPACING);  !! 		================
}

int draw_string_3x5(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable,
                uint8_t bg_r,
                uint8_t bg_g,
                uint8_t bg_b)
{
    if (!str) {
        return x;
    }

    while (*str) {
        x += draw_char_3x5(drv,
                       x,
                       y,
                       *str++,
                       r,
                       g,
                       b,
                       bg_enable,
                       bg_r,
                       bg_g,
                       bg_b);
    }

    return x;
}

//=========================================================//===========================================================================

void draw_bitmap_rgb32(Hub75Driver& drv,
                       int x,
                       int y,
                       const uint32_t *bitmap,
                       int width,
                       int height)
{
    if (!bitmap) {
        return;
    }

    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            uint32_t color = bitmap[py * width + px];

            /*
             * Assumes format: 0x00RRGGBB or 0xRRGGBB
             */
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8)  & 0xFF;
            uint8_t b =  color        & 0xFF;

            drv.set_pixel(x + px, y + py, r, g, b);
        }
    }
}