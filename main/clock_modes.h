#pragma once

#include <stdint.h>

typedef enum {
	MODE_TEST = 0,
    MODE_1 = 1,
    MODE_2,
    MODE_3,
    MODE_ROTATION,
} display_mode_t;

typedef enum {
    ROT_ITEM_LOGO = 0,
    ROT_ITEM_MODE_1,
    ROT_ITEM_MODE_2,
    ROT_ITEM_MODE_3,
} rotation_item_t;

typedef enum {
    FIXED_ITEM_LOGO = 0,
    FIXED_ITEM_SCREEN,
} fixed_item_t;

rotation_item_t clock_modes_get_rotation_item(void);
fixed_item_t clock_modes_get_fixed_item(void);
void clock_modes_reset_sequences(void);