// draw_bitmap.h
#pragma once
#include "hub75.h"
#include "hub75_types.h"
#include <stdint.h>
#include <string.h>  // memcpy

// ── 1bpp sprite (e.g. a 16×16 icon stored as 32 bytes) ──────────────────────
static const uint8_t ICON_WIFI[] = {
    // 16 px wide → 2 bytes per row → 32 bytes total
    // generate with: python3 -c "from PIL import Image; ..."
    // or with the LVGL font/image converter in 1bpp mode
    0x00, 0x00,
    0x3C, 0x3C,
    0x42, 0x42,
    0x81, 0x81,
    0x00, 0x00,
    0x3C, 0x3C,
    0x42, 0x42,
    0x00, 0x00,
    0x18, 0x18,
    0x24, 0x24,
    0x00, 0x00,
    0x18, 0x18,
    0x00, 0x00,
    0x18, 0x18,
    0x18, 0x18,
    0x00, 0x00,
};

// ─────────────────────────────────────────────────────────────────────────────
//  draw_bitmap_1bpp
//
//  Renders a monochrome 1-bit-per-pixel bitmap at (x, y).
//
//  Layout: row-major, LSB of each byte = leftmost pixel of that group of 8.
//  Stride (bytes per row) is computed as ⌈w / 8⌉ — same as Adafruit GFX.
//
//  fg_*  : color for '1' bits
//  bg_*  : color for '0' bits  (only written when bg_enable = true)
//
//  Clipping: pixels that fall outside the virtual display are silently skipped.
// ─────────────────────────────────────────────────────────────────────────────
inline void draw_bitmap_1bpp(Hub75Driver& drv,
                              int x, int y,
                              int w, int h,
                              const uint8_t* data,
                              uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
                              bool    bg_enable = false,
                              uint8_t bg_r = 0, uint8_t bg_g = 0, uint8_t bg_b = 0)
{
    const int stride = (w + 7) >> 3;  // bytes per row

    for (int row = 0; row < h; row++) {
        const uint8_t* row_ptr = data + row * stride;
        for (int col = 0; col < w; col++) {
            bool set = (row_ptr[col >> 3] >> (col & 7)) & 1;
            if (set) {
                drv.set_pixel(x + col, y + row, fg_r, fg_g, fg_b);
            } else if (bg_enable) {
                drv.set_pixel(x + col, y + row, bg_r, bg_g, bg_b);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  draw_bitmap_color
//
//  Renders a color bitmap at (x, y) using the driver's draw_pixels() API.
//  Supports RGB888 (3 B/px), RGB888_32 (4 B/px), and RGB565 (2 B/px).
//
//  Clipping: rows/columns that fall outside the display are cropped.
//  For partially-visible bitmaps a temporary row buffer is built on the stack
//  (max 512 px × 4 B = 2 KB) — keep bitmaps ≤512 px wide or allocate the
//  buffer from the heap for very wide sprites.
//
//  Parameters mirror Hub75Driver::draw_pixels() exactly so you can pass the
//  same format/color_order/big_endian args through.
// ─────────────────────────────────────────────────────────────────────────────
inline void draw_bitmap_color(Hub75Driver& drv,
                               int x, int y,
                               int w, int h,
                               const uint8_t* data,
                               Hub75PixelFormat   format      = Hub75PixelFormat::RGB888,
                               Hub75ColorOrder    color_order = Hub75ColorOrder::RGB,
                               bool          big_endian  = false)
{
    // bytes per pixel for each format
    int bpp;
    switch (format) {
        case Hub75PixelFormat::RGB565:    bpp = 2; break;
        case Hub75PixelFormat::RGB888_32: bpp = 4; break;
        default:                     bpp = 3; break;  // RGB888
    }

    const int src_stride = w * bpp;

    // ── clip destination rect ──────────────────────────────────────────────
    // virtual display extents (fill in your actual width/height if different)
    // We read them from the driver config so this stays in sync automatically.
    // Fallback to 192×64 if the driver doesn't expose them.
    const int disp_w = 192;  // adjust to match your config.panel_width  * config.layout_cols
    const int disp_h = 64;   // adjust to match your config.panel_height * config.layout_rows

    int dst_x0 = x,        dst_y0 = y;
    int dst_x1 = x + w,    dst_y1 = y + h;

    // clamp to display bounds
    int clip_x0 = (dst_x0 < 0)      ? 0      : dst_x0;
    int clip_y0 = (dst_y0 < 0)      ? 0      : dst_y0;
    int clip_x1 = (dst_x1 > disp_w) ? disp_w : dst_x1;
    int clip_y1 = (dst_y1 > disp_h) ? disp_h : dst_y1;

    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return;  // fully off-screen

    // how many pixels to skip at the left edge of the source row
    int src_skip_cols = clip_x0 - dst_x0;   // ≥0
    int src_skip_rows = clip_y0 - dst_y0;   // ≥0
    int vis_w         = clip_x1 - clip_x0;  // visible columns
    int vis_h         = clip_y1 - clip_y0;  // visible rows

    bool full_width = (vis_w == w);         // no horizontal clipping needed

    if (full_width) {
        // ── fast path: entire row is visible, hand directly to driver ────────
        for (int row = 0; row < vis_h; row++) {
            const uint8_t* src_row = data
                + (src_skip_rows + row) * src_stride
                + src_skip_cols * bpp;
            drv.draw_pixels(clip_x0, clip_y0 + row,
                            vis_w, 1,
                            src_row,
                            format, color_order, big_endian);
        }
    } else {
        // ── slow path: copy cropped row into temp buffer then blit ───────────
        // Stack buffer: 512 px × 4 B = 2 KB max.
        // If you need wider sprites, replace with a heap allocation.
        static uint8_t row_buf[512 * 4];

        const int copy_bytes = vis_w * bpp;

        for (int row = 0; row < vis_h; row++) {
            const uint8_t* src_row = data
                + (src_skip_rows + row) * src_stride
                + src_skip_cols * bpp;
            memcpy(row_buf, src_row, copy_bytes);
            drv.draw_pixels(clip_x0, clip_y0 + row,
                            vis_w, 1,
                            row_buf,
                            format, color_order, big_endian);
        }
    }
}