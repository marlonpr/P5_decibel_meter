#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_MENU = 0,
    BTN_UP,
    BTN_DOWN
} button_t;

#define BTN_NONE ((button_t)-1)

esp_err_t clock_buttons_init(gpio_num_t menu_pin,
                             gpio_num_t up_pin,
                             gpio_num_t down_pin);

QueueHandle_t clock_buttons_get_queue(void);

bool clock_button_is_pressed(button_t btn);
bool clock_buttons_all_released(void);

#ifdef __cplusplus
}
#endif