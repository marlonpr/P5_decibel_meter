#include "clock_buttons.h"

#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "CLOCK_BUTTONS";

static gpio_num_t s_pin_menu = GPIO_NUM_NC;
static gpio_num_t s_pin_up = GPIO_NUM_NC;
static gpio_num_t s_pin_down = GPIO_NUM_NC;

static QueueHandle_t s_button_queue = NULL;

static int button_pin(button_t btn)
{
    switch (btn)
    {
        case BTN_MENU:
            return s_pin_menu;

        case BTN_UP:
            return s_pin_up;

        case BTN_DOWN:
            return s_pin_down;

        default:
            return -1;
    }
}

static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_t btn = (button_t)(uintptr_t)arg;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (s_button_queue != NULL) {
        xQueueSendFromISR(s_button_queue, &btn, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t clock_buttons_init(gpio_num_t menu_pin,
                             gpio_num_t up_pin,
                             gpio_num_t down_pin)
{
    s_pin_menu = menu_pin;
    s_pin_up = up_pin;
    s_pin_down = down_pin;

    gpio_config_t io_conf = {};

    io_conf.pin_bit_mask =
        (1ULL << s_pin_menu) |
        (1ULL << s_pin_up)   |
        (1ULL << s_pin_down);

    io_conf.mode = GPIO_MODE_INPUT;

    /*
     * External pull-ups:
     * GPIO ---- button ---- GND
     * GPIO ---- 10k pull-up ---- 3.3V
     */
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    io_conf.intr_type = GPIO_INTR_NEGEDGE;

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_button_queue = xQueueCreate(10, sizeof(button_t));

    if (s_button_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button queue");
        return ESP_ERR_NO_MEM;
    }

	//============ THIS IS ALREADY INSTALLED IN ETHERNET INIT =====================
	/*
	ret = gpio_install_isr_service(0);
	
	if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
	    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
	    return ret;
	}
	*/
	//=========================================================================

    ret = gpio_isr_handler_add(s_pin_menu,
                               button_isr_handler,
                               (void *)(uintptr_t)BTN_MENU);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_isr_handler_add(s_pin_up,
                               button_isr_handler,
                               (void *)(uintptr_t)BTN_UP);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_isr_handler_add(s_pin_down,
                               button_isr_handler,
                               (void *)(uintptr_t)BTN_DOWN);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG,
             "Buttons initialized: MENU=%d UP=%d DOWN=%d",
             s_pin_menu,
             s_pin_up,
             s_pin_down);

    return ESP_OK;
}

QueueHandle_t clock_buttons_get_queue(void)
{
    return s_button_queue;
}

bool clock_button_is_pressed(button_t btn)
{
    int pin = button_pin(btn);

    if (pin < 0) {
        return false;
    }

    /*
     * released = HIGH
     * pressed  = LOW
     */
    return gpio_get_level((gpio_num_t)pin) == 0;
}

bool clock_buttons_all_released(void)
{
    return gpio_get_level(s_pin_menu) &&
           gpio_get_level(s_pin_up)   &&
           gpio_get_level(s_pin_down);
}