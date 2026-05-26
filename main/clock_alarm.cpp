#include "clock_alarm.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "clock_settings.h"

static const char *TAG = "CLOCK_ALARM";

static gpio_num_t s_alarm_gpio = GPIO_NUM_NC;

static portMUX_TYPE s_alarm_mux = portMUX_INITIALIZER_UNLOCKED;

static ethernet_alarm_t s_alarms[MAX_ETH_ALARMS] = {};

static bool s_alarms_dirty = false;
static int64_t s_alarms_dirty_until_us = 0;

static bool s_clear_alarms_on_next_ca = false;

static bool s_alarm_active = false;
static int s_alarm_active_id = 0;
static int64_t s_alarm_until_us = 0;
static alarm_effect_t s_alarm_effect = ALARM_EFFECT_CONTINUO;
static bool s_alarm_gpio_state = false;
static int64_t s_alarm_last_toggle_us = 0;

/*
 * Prevent retriggering the same alarm repeatedly during the same minute.
 */
static int s_last_trigger_alarm_id = 0;
static int s_last_trigger_day = 0;
static int s_last_trigger_hour = -1;
static int s_last_trigger_minute = -1;

// =============================== GPIO ===============================

esp_err_t clock_alarm_init(gpio_num_t alarm_gpio)
{
    s_alarm_gpio = alarm_gpio;

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << s_alarm_gpio);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(s_alarm_gpio, 0);

    ESP_LOGI(TAG, "Alarm GPIO initialized on GPIO%d", s_alarm_gpio);

    return ESP_OK;
}

// =============================== NVS LOAD / SAVE ===============================

esp_err_t clock_alarm_load(void)
{
    ethernet_alarm_t alarms_copy[MAX_ETH_ALARMS] = {};

    esp_err_t ret = clock_settings_load_ethernet_alarms(
        alarms_copy,
        sizeof(alarms_copy)
    );

    if (ret == ESP_ERR_NVS_NOT_FOUND ||
        ret == ESP_ERR_INVALID_SIZE) {
        ESP_LOGW(TAG, "No valid saved alarms, using defaults");
        return ESP_OK;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load alarms: %s", esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_alarm_mux);
    memcpy(s_alarms, alarms_copy, sizeof(s_alarms));
    portEXIT_CRITICAL(&s_alarm_mux);

    ESP_LOGI(TAG, "Ethernet alarms loaded");

    return ESP_OK;
}

esp_err_t clock_alarm_save(void)
{
    ethernet_alarm_t alarms_copy[MAX_ETH_ALARMS];

    portENTER_CRITICAL(&s_alarm_mux);
    memcpy(alarms_copy, s_alarms, sizeof(alarms_copy));
    portEXIT_CRITICAL(&s_alarm_mux);

    esp_err_t ret = clock_settings_save_ethernet_alarms(
        alarms_copy,
        sizeof(alarms_copy)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alarms: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Ethernet alarms saved");

    return ESP_OK;
}

void clock_alarm_process_deferred_save(void)
{
    bool save_now = false;

    portENTER_CRITICAL(&s_alarm_mux);

    if (s_alarms_dirty &&
        esp_timer_get_time() >= s_alarms_dirty_until_us) {
        s_alarms_dirty = false;
        save_now = true;
    }

    portEXIT_CRITICAL(&s_alarm_mux);

    if (save_now) {
        esp_err_t ret = clock_alarm_save();

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Ethernet alarms saved after batch update");
        } else {
            ESP_LOGE(TAG,
                     "Failed to save Ethernet alarms after batch update: %s",
                     esp_err_to_name(ret));
        }
    }
}

// =============================== ALARM STORAGE ===============================

void clock_alarm_arm_full_replacement(void)
{
    portENTER_CRITICAL(&s_alarm_mux);
    s_clear_alarms_on_next_ca = true;
    portEXIT_CRITICAL(&s_alarm_mux);

    ESP_LOGI(TAG, "Next CA will start full alarm replacement");
}

esp_err_t clock_alarm_store_from_ca(uint8_t alarm_id,
                                    uint8_t alarm_hh,
                                    uint8_t alarm_mm,
                                    uint8_t frequency,
                                    uint8_t duration_effect)
{
    if (alarm_id < 1 || alarm_id > MAX_ETH_ALARMS) {
        ESP_LOGW(TAG, "Invalid CA alarm_id=%u", alarm_id);
        return ESP_ERR_INVALID_ARG;
    }

    if (alarm_hh > 23 || alarm_mm > 59) {
        ESP_LOGW(TAG, "Invalid CA alarm time: %02u:%02u", alarm_hh, alarm_mm);
        return ESP_ERR_INVALID_ARG;
    }

    bool alarm_enabled = (frequency & 0x80) != 0;
    bool cleared_alarm_table = false;

    portENTER_CRITICAL(&s_alarm_mux);

    if (s_clear_alarms_on_next_ca) {
        memset(s_alarms, 0, sizeof(s_alarms));
        s_clear_alarms_on_next_ca = false;
        cleared_alarm_table = true;
    }

    s_alarms[alarm_id - 1].configured = alarm_enabled;
    s_alarms[alarm_id - 1].alarm_id = alarm_id;
    s_alarms[alarm_id - 1].time_hh = alarm_hh;
    s_alarms[alarm_id - 1].time_mm = alarm_mm;
    s_alarms[alarm_id - 1].frequency = frequency;
    s_alarms[alarm_id - 1].duration_effect = duration_effect;

    s_alarms_dirty = true;
    s_alarms_dirty_until_us = esp_timer_get_time() + 1000000;

    portEXIT_CRITICAL(&s_alarm_mux);

    if (cleared_alarm_table) {
        ESP_LOGW(TAG, "First CA after ES: clearing all 60 alarms first");
    }

    ESP_LOGI(TAG,
             "CA alarm stored: id=%u enabled=%d time=%02u:%02u freq=0x%02X dur_eff=0x%02X",
             alarm_id,
             alarm_enabled,
             alarm_hh,
             alarm_mm,
             frequency,
             duration_effect);

    return ESP_OK;
}

bool clock_alarm_read(uint8_t alarm_id, ethernet_alarm_t *out_alarm)
{
    if (out_alarm == NULL) {
        return false;
    }

    if (alarm_id < 1 || alarm_id > MAX_ETH_ALARMS) {
        return false;
    }

    ethernet_alarm_t alarm_copy;

    portENTER_CRITICAL(&s_alarm_mux);
    alarm_copy = s_alarms[alarm_id - 1];
    portEXIT_CRITICAL(&s_alarm_mux);

    if (!alarm_copy.configured) {
        alarm_copy.configured = false;
        alarm_copy.alarm_id = alarm_id;
        alarm_copy.time_hh = 0;
        alarm_copy.time_mm = 0;
        alarm_copy.frequency = 0x00;
        alarm_copy.duration_effect = 0x00;
    }

    *out_alarm = alarm_copy;

    return true;
}

void clock_alarm_clear_all_ram(void)
{
    portENTER_CRITICAL(&s_alarm_mux);

    memset(s_alarms, 0, sizeof(s_alarms));

    s_alarms_dirty = false;
    s_alarms_dirty_until_us = 0;
    s_clear_alarms_on_next_ca = false;

    s_alarm_active = false;
    s_alarm_active_id = 0;
    s_alarm_gpio_state = false;

    portEXIT_CRITICAL(&s_alarm_mux);

    if (s_alarm_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_alarm_gpio, 0);
    }

    ESP_LOGW(TAG, "All alarms cleared from RAM");
}

esp_err_t clock_alarm_clear_all_and_save(void)
{
    clock_alarm_clear_all_ram();
    return clock_alarm_save();
}

// =============================== DECODERS ===============================

static uint8_t alarm_decode_duration_seconds(uint8_t duration_effect)
{
    uint8_t duration = duration_effect & 0x0F;

    if (duration < 1) {
        duration = 1;
    }

    return duration;
}

static alarm_effect_t alarm_decode_effect(uint8_t duration_effect)
{
    uint8_t effect_code = (duration_effect >> 6) & 0x03;

    switch (effect_code) {
        case 0:
            return ALARM_EFFECT_CONTINUO;

        case 1:
            return ALARM_EFFECT_INTERMITENTE;

        case 2:
            return ALARM_EFFECT_PARPADEO_CONTINUO;

        case 3:
            return ALARM_EFFECT_PARPADEO_INTERMITENTE;

        default:
            return ALARM_EFFECT_CONTINUO;
    }
}

static bool alarm_is_enabled(uint8_t frequency)
{
    return (frequency & 0x80) != 0;
}

static bool alarm_day_matches(uint8_t frequency, int day_of_week)
{
    /*
     * frequency:
     * bit 7 = enabled
     * bits 0..6 = weekday mask
     *
     * Observed from Zeit software:
     * 0x40 = Domingo
     * 0x20 = Lunes
     * 0x10 = Martes
     * 0x08 = Miercoles
     * 0x04 = Jueves
     * 0x02 = Viernes
     * 0x01 = Sabado
     *
     * Internal clock:
     * 1 = Domingo
     * 2 = Lunes
     * 3 = Martes
     * 4 = Miercoles
     * 5 = Jueves
     * 6 = Viernes
     * 7 = Sabado
     */

    uint8_t day_mask = frequency & 0x7F;

    switch (day_of_week) {
        case 1:  // Domingo
            return (day_mask & 0x40) != 0;

        case 2:  // Lunes
            return (day_mask & 0x20) != 0;

        case 3:  // Martes
            return (day_mask & 0x10) != 0;

        case 4:  // Miercoles
            return (day_mask & 0x08) != 0;

        case 5:  // Jueves
            return (day_mask & 0x04) != 0;

        case 6:  // Viernes
            return (day_mask & 0x02) != 0;

        case 7:  // Sabado
            return (day_mask & 0x01) != 0;

        default:
            return false;
    }
}

// =============================== RUNTIME ===============================

static void alarm_start(const ethernet_alarm_t *alarm)
{
    if (alarm == NULL) {
        return;
    }

    uint8_t duration_sec =
        alarm_decode_duration_seconds(alarm->duration_effect);

    alarm_effect_t effect =
        alarm_decode_effect(alarm->duration_effect);

    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_alarm_mux);

    s_alarm_active = true;
    s_alarm_active_id = alarm->alarm_id;
    s_alarm_until_us = now_us + ((int64_t)duration_sec * 1000000);
    s_alarm_effect = effect;
    s_alarm_gpio_state = true;
    s_alarm_last_toggle_us = now_us;

    portEXIT_CRITICAL(&s_alarm_mux);

    if (s_alarm_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_alarm_gpio, 1);
    }

    ESP_LOGW(TAG,
             "Alarm started: id=%u duration=%u sec effect=%d",
             alarm->alarm_id,
             duration_sec,
             effect);
}

static void alarm_stop(void)
{
    portENTER_CRITICAL(&s_alarm_mux);

    s_alarm_active = false;
    s_alarm_active_id = 0;
    s_alarm_gpio_state = false;

    portEXIT_CRITICAL(&s_alarm_mux);

    if (s_alarm_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_alarm_gpio, 0);
    }

    ESP_LOGW(TAG, "Alarm stopped");
}

void clock_alarm_check_trigger(const ds3231_time_t *now)
{
    if (now == NULL) {
        return;
    }

    bool alarm_active_copy = false;

    portENTER_CRITICAL(&s_alarm_mux);
    alarm_active_copy = s_alarm_active;
    portEXIT_CRITICAL(&s_alarm_mux);

    if (alarm_active_copy) {
        return;
    }

    ethernet_alarm_t alarms_copy[MAX_ETH_ALARMS];

    portENTER_CRITICAL(&s_alarm_mux);
    memcpy(alarms_copy, s_alarms, sizeof(alarms_copy));
    portEXIT_CRITICAL(&s_alarm_mux);

    for (int i = 0; i < MAX_ETH_ALARMS; i++) {
        ethernet_alarm_t *alarm = &alarms_copy[i];

        if (!alarm->configured) {
            continue;
        }

        if (!alarm_is_enabled(alarm->frequency)) {
            continue;
        }

        if (!alarm_day_matches(alarm->frequency, now->day_of_week)) {
            continue;
        }

        if (alarm->time_hh != now->hour ||
            alarm->time_mm != now->minute) {
            continue;
        }

        /*
         * Avoid retriggering every frame during the same minute.
         */
        if (s_last_trigger_alarm_id == alarm->alarm_id &&
            s_last_trigger_day == now->day &&
            s_last_trigger_hour == now->hour &&
            s_last_trigger_minute == now->minute) {
            continue;
        }

        s_last_trigger_alarm_id = alarm->alarm_id;
        s_last_trigger_day = now->day;
        s_last_trigger_hour = now->hour;
        s_last_trigger_minute = now->minute;

        alarm_start(alarm);
        break;
    }
}

void clock_alarm_runtime_update(void)
{
    bool active_copy = false;
    int64_t until_copy = 0;
    alarm_effect_t effect_copy = ALARM_EFFECT_CONTINUO;
    bool gpio_state_copy = false;
    int64_t last_toggle_copy = 0;

    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_alarm_mux);

    active_copy = s_alarm_active;
    until_copy = s_alarm_until_us;
    effect_copy = s_alarm_effect;
    gpio_state_copy = s_alarm_gpio_state;
    last_toggle_copy = s_alarm_last_toggle_us;

    portEXIT_CRITICAL(&s_alarm_mux);

    if (!active_copy) {
        return;
    }

    if (now_us >= until_copy) {
        alarm_stop();
        return;
    }

    /*
     * GPIO intermittent behavior.
     */
    if (effect_copy == ALARM_EFFECT_INTERMITENTE ||
        effect_copy == ALARM_EFFECT_PARPADEO_INTERMITENTE) {

        if ((now_us - last_toggle_copy) >= 500000) {
            gpio_state_copy = !gpio_state_copy;

            portENTER_CRITICAL(&s_alarm_mux);
            s_alarm_gpio_state = gpio_state_copy;
            s_alarm_last_toggle_us = now_us;
            portEXIT_CRITICAL(&s_alarm_mux);

            if (s_alarm_gpio != GPIO_NUM_NC) {
                gpio_set_level(s_alarm_gpio, gpio_state_copy ? 1 : 0);
            }
        }
    } else {
        if (s_alarm_gpio != GPIO_NUM_NC) {
            gpio_set_level(s_alarm_gpio, 1);
        }
    }
}

bool clock_alarm_get_display_state(clock_alarm_display_state_t *state)
{
    if (state == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_alarm_mux);

    state->active = s_alarm_active;
    state->alarm_id = s_alarm_active_id;
    state->effect = s_alarm_effect;

    portEXIT_CRITICAL(&s_alarm_mux);

    return state->active;
}