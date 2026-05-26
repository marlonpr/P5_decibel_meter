#include "clock_modes.h"

#include "esp_timer.h"
#include "led_panel.h"

static const int64_t ROTATION_LOGO_INTERVAL_US = 3 * 1000000;
static const int64_t ROTATION_MODE_INTERVAL_US = 21 * 1000000;

static const int64_t FIXED_LOGO_INTERVAL_US = 3 * 1000000;
static const int64_t FIXED_MODE_INTERVAL_US = 21 * 1000000;

static rotation_item_t s_rotation_item = ROT_ITEM_LOGO;
static int64_t s_rotation_last_change_us = 0;

static fixed_item_t s_fixed_item = FIXED_ITEM_LOGO;
static int64_t s_fixed_last_change_us = 0;

rotation_item_t clock_modes_get_rotation_item(void)
{
    int64_t now_us = esp_timer_get_time();

    if (s_rotation_last_change_us == 0) {
        s_rotation_last_change_us = now_us;
        s_rotation_item = ROT_ITEM_LOGO;
        return s_rotation_item;
    }

    int64_t interval_us;

    if (s_rotation_item == ROT_ITEM_LOGO) {
        interval_us = ROTATION_LOGO_INTERVAL_US;
    } else {
        interval_us = ROTATION_MODE_INTERVAL_US;
    }

    if ((now_us - s_rotation_last_change_us) >= interval_us) {
        s_rotation_last_change_us = now_us;

        switch (s_rotation_item)
        {
            case ROT_ITEM_LOGO:
                s_rotation_item = ROT_ITEM_MODE_1;
                break;

            case ROT_ITEM_MODE_1:
                s_rotation_item = ROT_ITEM_MODE_2;
                break;

            case ROT_ITEM_MODE_2:
                s_rotation_item = ROT_ITEM_MODE_3;
                break;

            case ROT_ITEM_MODE_3:
            default:
                s_rotation_item = ROT_ITEM_LOGO;
                break;
        }

        scroll_stop();
    }

    return s_rotation_item;
}

fixed_item_t clock_modes_get_fixed_item(void)
{
    int64_t now_us = esp_timer_get_time();

    if (s_fixed_last_change_us == 0) {
        s_fixed_last_change_us = now_us;
        s_fixed_item = FIXED_ITEM_LOGO;
        return s_fixed_item;
    }

    int64_t interval_us;

    if (s_fixed_item == FIXED_ITEM_LOGO) {
        interval_us = FIXED_LOGO_INTERVAL_US;
    } else {
        interval_us = FIXED_MODE_INTERVAL_US;
    }

    if ((now_us - s_fixed_last_change_us) >= interval_us) {
        s_fixed_last_change_us = now_us;

        if (s_fixed_item == FIXED_ITEM_LOGO) {
            s_fixed_item = FIXED_ITEM_SCREEN;
        } else {
            s_fixed_item = FIXED_ITEM_LOGO;
        }

        scroll_stop();
    }

    return s_fixed_item;
}

void clock_modes_reset_sequences(void)
{
    s_fixed_last_change_us = 0;
    s_fixed_item = FIXED_ITEM_LOGO;

    s_rotation_last_change_us = 0;
    s_rotation_item = ROT_ITEM_LOGO;

    scroll_stop();
}