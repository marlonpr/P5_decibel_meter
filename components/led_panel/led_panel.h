// led_panel.h
#pragma once
#include "hub75.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C++" {
#endif

Hub75Config make_config(void);

int draw_char(Hub75Driver& drv,
              int x, int y,
              char ch,
              uint8_t r, uint8_t g, uint8_t b,
              bool bg_enable = false,
              uint8_t bg_r = 0,
              uint8_t bg_g = 0,
              uint8_t bg_b = 0);

int draw_string(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable = false,
                uint8_t bg_r = 0,
                uint8_t bg_g = 0,
                uint8_t bg_b = 0);
				
//==================== FONT5X7 ===================================================				
int draw_string_5x7(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable = false,
                uint8_t bg_r = 0,
                uint8_t bg_g = 0,
                uint8_t bg_b = 0);
				
				
				
//==================== FONT5X5 ===================================================				
int draw_string_5x5(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable = false,
                uint8_t bg_r = 0,
                uint8_t bg_g = 0,
                uint8_t bg_b = 0);
				
//==================== FONT10X15 ===================================================								
int draw_string_10x15(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable = false,
                uint8_t bg_r = 0,
                uint8_t bg_g = 0,
                uint8_t bg_b = 0);
				
//==================== FONT2X9 ===================================================												
int draw_string_2x9(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable = false,
                uint8_t bg_r = 0,
                uint8_t bg_g = 0,
                uint8_t bg_b = 0);
				
//==================== FONT3X5 ===================================================															
int draw_string_3x5(Hub75Driver& drv,
                int x, int y,
                const char* str,
                uint8_t r, uint8_t g, uint8_t b,
                bool bg_enable = false,
                uint8_t bg_r = 0,
                uint8_t bg_g = 0,
                uint8_t bg_b = 0);
				
// ================= SCROLL TEXT =================

void scroll_start(const char *text,
                  int y,
                  uint8_t r,
                  uint8_t g,
                  uint8_t b,
                  int speed_px_per_sec);

void scroll_stop(void);

bool scroll_is_active(void);

void scroll_update(Hub75Driver& drv);




void scroll_start_if_needed(const char *text,
                            int y,
                            uint8_t r,
                            uint8_t g,
                            uint8_t b,
                            int speed_px_per_sec);




void draw_bitmap_rgb32(Hub75Driver& drv,
                       int x,
                       int y,
                       const uint32_t *bitmap,
                       int width,
                       int height);

#ifdef __cplusplus
}
#endif


// Structure to pass multiple arguments to the task if needed
struct DisplayTaskConfig {
    Hub75Driver* driver;
};