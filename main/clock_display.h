#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub75.h"
#include "ds3231.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FORMAT_12H = 0,
    FORMAT_24H = 1,
} hour_format_t;


void clock_display_draw_mode_test(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format, int dB_value);



void clock_display_draw_mode_1(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format);

void clock_display_draw_mode_2(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format);

void clock_display_draw_mode_3(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format);

void clock_display_draw_logo(Hub75Driver *driver);

void clock_display_draw_startup(Hub75Driver *driver,
                                int display_mode,
                                int brightness_level,
                                hour_format_t format);

void clock_display_make_date_scroll_text(const ds3231_time_t *time,
                                         char *buf,
                                         size_t size);

int clock_display_center_x_5x7(const char *text);

int clock_display_center_x_6x9(const char *text);

#ifdef __cplusplus
}
#endif