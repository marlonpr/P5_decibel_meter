#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "ds3231.h"
#include "clock_display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

typedef struct {
    int *brightness_level;

    bool *eth_brightness_pending;
    int *eth_brightness_level;

    bool *eth_format_pending;
    hour_format_t *eth_format;

    bool *eth_time_pending;
    ds3231_time_t *eth_time;

    bool *factory_reset_pending;

    ds3231_time_t *now;
    bool *rtc_valid;

    hour_format_t *clock_format;

    portMUX_TYPE *data_mux;
} clock_protocol_context_t;

bool rtc_time_is_valid(const ds3231_time_t *time);

void clock_protocol_init(const clock_protocol_context_t *ctx);

int clock_protocol_rx_callback(const uint8_t *p,
                               int len,
                               uint8_t *tx,
                               int tx_max);