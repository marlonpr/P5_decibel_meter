#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub75.h"
#include "ds3231.h"
#include "clock_buttons.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*clock_menu_message_cb_t)(const char *msg, uint32_t duration_ms);

typedef struct {
    Hub75Driver *driver;
    ds3231_dev_t *rtc;

    int *brightness_level;
    int *temporal_brightness;

    portMUX_TYPE *data_mux;
    ds3231_time_t *g_now;
    bool *g_rtc_valid;

    clock_menu_message_cb_t show_message;
} clock_menu_context_t;

void clock_menu_init(clock_menu_context_t *ctx);

bool clock_menu_is_active(void);

void clock_menu_enter(void);
void clock_menu_cancel(void);
void clock_menu_check_timeout(void);

void clock_menu_handle_button(button_t btn);

void clock_menu_draw(Hub75Driver *driver);

int clock_menu_calculate_weekday(int day, int month, int year);
int clock_menu_days_in_month(int month, int year);

#ifdef __cplusplus
}
#endif