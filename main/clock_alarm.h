#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "ds3231.h"

#define MAX_ETH_ALARMS 60

typedef enum {
    ALARM_EFFECT_CONTINUO = 0,
    ALARM_EFFECT_INTERMITENTE,
    ALARM_EFFECT_PARPADEO_CONTINUO,
    ALARM_EFFECT_PARPADEO_INTERMITENTE,
} alarm_effect_t;

typedef struct {
    bool configured;
    uint8_t alarm_id;
    uint8_t time_hh;
    uint8_t time_mm;
    uint8_t frequency;
    uint8_t duration_effect;
} ethernet_alarm_t;

static_assert(sizeof(ethernet_alarm_t) == 6,
              "ethernet_alarm_t size changed; NVS alarm blob compatibility affected");

typedef struct {
    bool active;
    int alarm_id;
    alarm_effect_t effect;
} clock_alarm_display_state_t;

esp_err_t clock_alarm_init(gpio_num_t alarm_gpio);

esp_err_t clock_alarm_load(void);
esp_err_t clock_alarm_save(void);

void clock_alarm_process_deferred_save(void);

void clock_alarm_arm_full_replacement(void);

esp_err_t clock_alarm_store_from_ca(uint8_t alarm_id,
                                    uint8_t alarm_hh,
                                    uint8_t alarm_mm,
                                    uint8_t frequency,
                                    uint8_t duration_effect);

bool clock_alarm_read(uint8_t alarm_id, ethernet_alarm_t *out_alarm);

void clock_alarm_clear_all_ram(void);
esp_err_t clock_alarm_clear_all_and_save(void);

void clock_alarm_check_trigger(const ds3231_time_t *now);
void clock_alarm_runtime_update(void);

bool clock_alarm_get_display_state(clock_alarm_display_state_t *state);