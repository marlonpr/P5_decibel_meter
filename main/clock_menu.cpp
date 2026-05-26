#include "clock_menu.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "led_panel.h"
#include "clock_display.h"
#include "clock_settings.h"

static const char *TAG = "CLOCK_MENU";

#define MENU_TIMEOUT_US (10 * 1000000)

typedef enum {
    MENU_IDLE = 0,
    MENU_BRIGHTNESS,
    MENU_HOUR,
    MENU_MINUTE,
    MENU_DAY,
    MENU_MONTH,
    MENU_YEAR,
} menu_state_t;

static clock_menu_context_t s_ctx = {};

static bool s_menu_active = false;
static menu_state_t s_menu_state = MENU_IDLE;
static int64_t s_menu_last_action_us = 0;

static ds3231_time_t s_tmp_time = {
    .second = 0,
    .minute = 0,
    .hour = 0,
    .day_of_week = 1,
    .day = 1,
    .month = 1,
    .year = 2000,
};

static uint8_t brightness_level_to_hub75(int level)
{
    if (level < 1) {
        level = 1;
    }

    if (level > 10) {
        level = 10;
    }

    return (uint8_t)((level * 255) / 10);
}

static void refresh_menu_timeout(void)
{
    s_menu_last_action_us = esp_timer_get_time();
}

static void restore_saved_brightness(void)
{
    if (!s_ctx.driver || !s_ctx.brightness_level || !s_ctx.temporal_brightness) {
        return;
    }

    *s_ctx.temporal_brightness = *s_ctx.brightness_level;

    s_ctx.driver->set_brightness(
        brightness_level_to_hub75(*s_ctx.brightness_level)
    );
}

static bool is_leap_year(int year)
{
    if ((year % 400) == 0) {
        return true;
    }

    if ((year % 100) == 0) {
        return false;
    }

    return (year % 4) == 0;
}

int clock_menu_days_in_month(int month, int year)
{
    switch (month)
    {
        case 1:  return 31;
        case 2:  return is_leap_year(year) ? 29 : 28;
        case 3:  return 31;
        case 4:  return 30;
        case 5:  return 31;
        case 6:  return 30;
        case 7:  return 31;
        case 8:  return 31;
        case 9:  return 30;
        case 10: return 31;
        case 11: return 30;
        case 12: return 31;

        default:
            return 31;
    }
}

static void clamp_tmp_day_to_month(void)
{
    int max_day = clock_menu_days_in_month(s_tmp_time.month, s_tmp_time.year);

    if (s_tmp_time.day < 1) {
        s_tmp_time.day = 1;
    }

    if (s_tmp_time.day > max_day) {
        s_tmp_time.day = max_day;
    }
}

int clock_menu_calculate_weekday(int day, int month, int year)
{
    if (month < 3) {
        month += 12;
        year--;
    }

    int K = year % 100;
    int J = year / 100;

    int h = (day + 13 * (month + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;

    /*
     * Zeller:
     * 0 = Saturday
     * 1 = Sunday
     * ...
     * 6 = Friday
     *
     * DS3231:
     * 1 = Sunday
     * ...
     * 7 = Saturday
     */
    return ((h + 6) % 7) + 1;
}

static void update_tmp_weekday(void)
{
    s_tmp_time.day_of_week = clock_menu_calculate_weekday(
        s_tmp_time.day,
        s_tmp_time.month,
        s_tmp_time.year
    );
}

void clock_menu_init(clock_menu_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    s_ctx = *ctx;

    s_menu_active = false;
    s_menu_state = MENU_IDLE;
    s_menu_last_action_us = 0;
}

bool clock_menu_is_active(void)
{
    return s_menu_active;
}

void clock_menu_enter(void)
{
    if (!s_ctx.rtc) {
        return;
    }

    if (ds3231_get_time(s_ctx.rtc, &s_tmp_time) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot enter menu: failed to read RTC");
        return;
    }

    if (s_ctx.brightness_level && s_ctx.temporal_brightness) {
        *s_ctx.temporal_brightness = *s_ctx.brightness_level;
    }

    s_menu_active = true;
    s_menu_state = MENU_BRIGHTNESS;

    refresh_menu_timeout();

    scroll_stop();

    if (s_ctx.show_message) {
        s_ctx.show_message("MENU", 500);
    }

    ESP_LOGI(TAG, "Menu entered");
}

static void exit_menu(void)
{
    s_menu_active = false;
    s_menu_state = MENU_IDLE;

    scroll_stop();

    ESP_LOGI(TAG, "Menu exited");
}

void clock_menu_cancel(void)
{
    restore_saved_brightness();

    s_menu_active = false;
    s_menu_state = MENU_IDLE;

    scroll_stop();

    if (s_ctx.show_message) {
        s_ctx.show_message("SALIR", 1000);
    }

    ESP_LOGI(TAG, "Menu cancelled");
}

void clock_menu_check_timeout(void)
{
    if (!s_menu_active) {
        return;
    }

    int64_t now_us = esp_timer_get_time();

    if ((now_us - s_menu_last_action_us) > MENU_TIMEOUT_US) {
        clock_menu_cancel();
        ESP_LOGI(TAG, "Menu timeout -> exit without saving");
    }
}

static void save_menu_values(void)
{
    if (!s_ctx.rtc) {
        return;
    }

    s_tmp_time.second = 0;

    clamp_tmp_day_to_month();
    update_tmp_weekday();

    esp_err_t ret = ds3231_set_time(s_ctx.rtc, &s_tmp_time);

    if (ret == ESP_OK) {
        if (s_ctx.data_mux && s_ctx.g_now && s_ctx.g_rtc_valid) {
            portENTER_CRITICAL(s_ctx.data_mux);
            *s_ctx.g_now = s_tmp_time;
            *s_ctx.g_rtc_valid = true;
            portEXIT_CRITICAL(s_ctx.data_mux);
        }

        if (s_ctx.brightness_level && s_ctx.temporal_brightness && s_ctx.driver) {
            *s_ctx.brightness_level = *s_ctx.temporal_brightness;

            s_ctx.driver->set_brightness(
                brightness_level_to_hub75(*s_ctx.brightness_level)
            );

            clock_settings_save_brightness((uint8_t)(*s_ctx.brightness_level));
        }

        if (s_ctx.show_message) {
            s_ctx.show_message("GUARDADO", 1000);
        }

        ESP_LOGI(TAG, "RTC time and brightness saved");
    } else {
        if (s_ctx.show_message) {
            s_ctx.show_message("ERROR", 1000);
        }

        ESP_LOGE(TAG, "Failed to save RTC time: %s", esp_err_to_name(ret));
    }

    exit_menu();
}

void clock_menu_handle_button(button_t btn)
{
    refresh_menu_timeout();

    switch (s_menu_state)
    {
        case MENU_BRIGHTNESS:
        {
            if (!s_ctx.driver || !s_ctx.temporal_brightness) {
                break;
            }

            if (btn == BTN_UP && *s_ctx.temporal_brightness < 10) {
                (*s_ctx.temporal_brightness)++;

                s_ctx.driver->set_brightness(
                    brightness_level_to_hub75(*s_ctx.temporal_brightness)
                );
            }

            if (btn == BTN_DOWN && *s_ctx.temporal_brightness > 1) {
                (*s_ctx.temporal_brightness)--;

                s_ctx.driver->set_brightness(
                    brightness_level_to_hub75(*s_ctx.temporal_brightness)
                );
            }

            if (btn == BTN_MENU) {
                s_menu_state = MENU_HOUR;
            }

            break;
        }

        case MENU_HOUR:
            if (btn == BTN_UP) {
                s_tmp_time.hour = (s_tmp_time.hour + 1) % 24;
            }

            if (btn == BTN_DOWN) {
                s_tmp_time.hour = (s_tmp_time.hour + 23) % 24;
            }

            if (btn == BTN_MENU) {
                s_menu_state = MENU_MINUTE;
            }

            break;

        case MENU_MINUTE:
            if (btn == BTN_UP) {
                s_tmp_time.minute = (s_tmp_time.minute + 1) % 60;
            }

            if (btn == BTN_DOWN) {
                s_tmp_time.minute = (s_tmp_time.minute + 59) % 60;
            }

            if (btn == BTN_MENU) {
                s_menu_state = MENU_DAY;
            }

            break;

        case MENU_DAY:
        {
            int max_day = clock_menu_days_in_month(s_tmp_time.month, s_tmp_time.year);

            if (btn == BTN_UP) {
                s_tmp_time.day++;

                if (s_tmp_time.day > max_day) {
                    s_tmp_time.day = 1;
                }
            }

            if (btn == BTN_DOWN) {
                s_tmp_time.day--;

                if (s_tmp_time.day < 1) {
                    s_tmp_time.day = max_day;
                }
            }

            update_tmp_weekday();

            if (btn == BTN_MENU) {
                s_menu_state = MENU_MONTH;
            }

            break;
        }

        case MENU_MONTH:
            if (btn == BTN_UP) {
                s_tmp_time.month = (s_tmp_time.month % 12) + 1;
                clamp_tmp_day_to_month();
            }

            if (btn == BTN_DOWN) {
                s_tmp_time.month = ((s_tmp_time.month + 10) % 12) + 1;
                clamp_tmp_day_to_month();
            }

            update_tmp_weekday();

            if (btn == BTN_MENU) {
                s_menu_state = MENU_YEAR;
            }

            break;

        case MENU_YEAR:
            if (btn == BTN_UP) {
                s_tmp_time.year = 2000 + ((s_tmp_time.year - 2000 + 1) % 100);
                clamp_tmp_day_to_month();
            }

            if (btn == BTN_DOWN) {
                s_tmp_time.year = 2000 + ((s_tmp_time.year - 2000 + 99) % 100);
                clamp_tmp_day_to_month();
            }

            update_tmp_weekday();

            if (btn == BTN_MENU) {
                save_menu_values();
            }

            break;

        default:
            break;
    }
}

void clock_menu_draw(Hub75Driver *driver)
{
    if (!driver) {
        return;
    }

    char buf[32];

    switch (s_menu_state)
    {
        case MENU_BRIGHTNESS:
            if (s_ctx.temporal_brightness) {
                snprintf(buf, sizeof(buf), "BRILLO:%d", *s_ctx.temporal_brightness);
            } else {
                snprintf(buf, sizeof(buf), "BRILLO");
            }
            break;

        case MENU_HOUR:
            snprintf(buf, sizeof(buf), "HORA:%02d", s_tmp_time.hour);
            break;

        case MENU_MINUTE:
            snprintf(buf, sizeof(buf), "MIN:%02d", s_tmp_time.minute);
            break;

        case MENU_DAY:
            snprintf(buf, sizeof(buf), "DIA:%02d", s_tmp_time.day);
            break;

        case MENU_MONTH:
            snprintf(buf, sizeof(buf), "MES:%02d", s_tmp_time.month);
            break;

        case MENU_YEAR:
            snprintf(buf, sizeof(buf), "A|O:%02d", s_tmp_time.year - 2000);
            break;

        default:
            snprintf(buf, sizeof(buf), "MENU");
            break;
    }

    draw_string(*driver,
                s_menu_state == MENU_BRIGHTNESS ? 1 : clock_display_center_x_6x9(buf),
                8,
                buf,
                255,
                0,
                0);
}