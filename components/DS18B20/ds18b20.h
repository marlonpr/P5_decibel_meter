#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t pin;
    bool present;
} ds18b20_t;

esp_err_t ds18b20_init(ds18b20_t *sensor, gpio_num_t pin);

esp_err_t ds18b20_read_temperature(ds18b20_t *sensor, float *temperature);

esp_err_t ds18b20_read_temperature_int(ds18b20_t *sensor, int16_t *temperature);

esp_err_t ds18b20_start_conversion(ds18b20_t *sensor);

esp_err_t ds18b20_read_scratchpad_temp(ds18b20_t *sensor, int16_t *temp_out);

#ifdef __cplusplus
}
#endif