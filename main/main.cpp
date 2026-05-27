#include <stdio.h>
#include "hub75.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ds3231.h"
#include "led_panel.h"
#include "ds18b20.h"

#include "clock_display.h"
#include "clock_settings.h"
#include "clock_buttons.h"
#include "clock_menu.h"
#include "clock_ethernet.h"
#include "clock_alarm.h"
#include "clock_protocol.h"
#include "clock_modes.h"

#include "nvs.h"

#include <stdint.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_check.h"

#include "driver/i2s_std.h"

#define ALARM_GPIO GPIO_NUM_48
#define DS18B20_GPIO GPIO_NUM_39
#define PIN_MENU GPIO_NUM_40
#define PIN_UP   GPIO_NUM_41
#define PIN_DOWN GPIO_NUM_42

#define BUTTON_HOLD_MS     1000
#define BUTTON_DEBOUNCE_MS 500
#define BUTTON_REPEAT_DELAY_MS 500
#define BUTTON_REPEAT_RATE_MS  500

#define TAG2 "INMP441_TEST"

//Assing a diferrent STATIC IP for each device in clock_ethernet.cpp
//ip_info.ip.addr      = ESP_IP4TOADDR(192, 168, 10, 50); 50 for device 1, 51 for device 2 and so on

static const char* TAG = "MAIN";

/*
   ESP32-S3 GPIO connections:

   INMP441 SCK  -> GPIO 17  // I2S BCLK
   INMP441 WS   -> GPIO 18  // I2S LRCLK / Word Select
   INMP441 SD   -> GPIO 16  // I2S data input
   INMP441 VDD  -> 3.3V
   INMP441 GND  -> GND
   INMP441 L/R  -> GND for LEFT channel   
   
   #define I2S_BCLK_IO      GPIO_NUM_17
   #define I2S_WS_IO        GPIO_NUM_18
   #define I2S_DIN_IO       GPIO_NUM_16   
   
   #define I2S_BCLK_IO  GPIO_NUM_35
   #define I2S_WS_IO    GPIO_NUM_36
   #define I2S_DIN_IO   GPIO_NUM_37   
*/

#define I2S_BCLK_IO  GPIO_NUM_35 //SCK
#define I2S_WS_IO    GPIO_NUM_36 //WS
#define I2S_DIN_IO   GPIO_NUM_37 //SD

#define SAMPLE_RATE_HZ   16000
#define READ_FRAMES      2048
#define LOG_INTERVAL_MS  250

#define LOUD_THRESHOLD_DBFS      -32.0
#define SPEECH_THRESHOLD_DBFS    -42.0
#define QUIET_THRESHOLD_DBFS     -48.0

#define MIC_FULL_SCALE_24BIT       8388607.0
#define DBFS_FLOOR                -120.0

/*
   Start with nominal INMP441 calibration.

   INMP441 nominal:
   -26 dBFS peak at 94 dB SPL, 1 kHz.

   For RMS dBFS using sine wave:
   94 dB SPL corresponds approximately to -29.01 dBFS RMS.

   Therefore:
   SPL = dBFS_RMS + 123.01
*/
#define INMP441_NOMINAL_CAL_OFFSET_DB   123.01

/*
   After real calibration, change this value.
   Example:
   If your reference SPL meter says 60.0 dB,
   and your ESP32 says -52.0 dBFS RMS,
   then offset = 60.0 - (-52.0) = 112.0
*/
#define USER_CAL_OFFSET_DB             109.3 //INMP441_NOMINAL_CAL_OFFSET_DB
#define SPL_ATTACK_ALPHA   0.70
#define SPL_RELEASE_ALPHA  0.12

int dB_value = 0;

#define USER_CAL_OFFSET_DBA  105.0

typedef struct {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
    double z1;
    double z2;
} biquad_t;

/*
   A-weighting IIR filter for 16000 Hz sample rate.
   Implemented as 3 cascaded biquad sections.
   Coefficients are normalized for approximately 0 dB gain at 1 kHz.
*/
static biquad_t a_weight_filter[] = {
    {
        .b0 = 0.529094044,
        .b1 = 1.058188088,
        .b2 = 0.529094044,
        .a1 = 0.821563820,
        .a2 = 0.168741780,
        .z1 = 0.0,
        .z2 = 0.0,
    },
    {
        .b0 = 1.000000000,
        .b1 = -2.000000000,
        .b2 = 1.000000000,
        .a1 = -1.705509630,
        .a2 = 0.715987580,
        .z1 = 0.0,
        .z2 = 0.0,
    },
    {
        .b0 = 1.000000000,
        .b1 = -2.000000000,
        .b2 = 1.000000000,
        .a1 = -1.983886760,
        .a2 = 0.983951670,
        .z1 = 0.0,
        .z2 = 0.0,
    },
};

static double biquad_process(biquad_t *s, double x)
{
    double y = s->b0 * x + s->z1;

    s->z1 = s->b1 * x - s->a1 * y + s->z2;
    s->z2 = s->b2 * x - s->a2 * y;

    return y;
}

static double a_weight_process(double x)
{
    double y = x;

    for (int i = 0; i < 3; i++) {
        y = biquad_process(&a_weight_filter[i], y);
    }

    return y;
}

static void test_a_weighting_filter(void)
{
    double sum_sq = 0.0;
    double sum_sq_a = 0.0;

    const double fs = 16000.0;
    const double f = 1000.0;
    const double amp = 100000.0;
    const int n = 16000;

    for (int i = 0; i < n; i++) {
        double x = amp * sin(2.0 * M_PI * f * (double)i / fs);
        double y = a_weight_process(x);

        sum_sq += x * x;
        sum_sq_a += y * y;
    }

    double rms = sqrt(sum_sq / n);
    double rms_a = sqrt(sum_sq_a / n);
    double gain_db = 20.0 * log10(rms_a / rms);

    ESP_LOGI(TAG, "A-weighting 1kHz test: rms=%.1f rmsA=%.1f gain=%.2f dB",
             rms, rms_a, gain_db);
}

#define DB_SMOOTH_ALPHA                 0.10
#define STARTUP_SKIP_SAMPLES            5

static bool spl_initialized = false;
static double spl_smooth = 0.0;
static int spl_startup_skip = STARTUP_SKIP_SAMPLES;

static double rms_to_dbfs(double rms)
{
    if (rms <= 1.0) {
        return DBFS_FLOOR;
    }

    return 20.0 * log10(rms / MIC_FULL_SCALE_24BIT);
}

static double dbfs_to_calibrated_spl(double dbfs)
{
    return dbfs + USER_CAL_OFFSET_DB;
}

static double smooth_spl(double spl)
{
    if (!spl_initialized) {
        spl_smooth = spl;
        spl_initialized = true;
    } else {
        spl_smooth = (1.0 - DB_SMOOTH_ALPHA) * spl_smooth +
                     DB_SMOOTH_ALPHA * spl;
    }

    return spl_smooth;
}


/*
   INMP441 sends 24-bit audio in a 32-bit I2S slot.
   We read stereo frames because the microphone outputs only one selected channel.

   If L/R pin is connected to GND, use left channel.
   If L/R pin is connected to 3V3, change this to 0.
*/
#define MIC_IS_LEFT_CHANNEL  1

static i2s_chan_handle_t rx_chan = NULL;

static esp_err_t i2s_mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, NULL, &rx_chan),
        TAG2,
        "Failed to create I2S RX channel"
    );

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),

        /*
           Philips I2S format is the normal format for INMP441.
           32-bit slots are used because INMP441 provides 24-bit data
           aligned inside a 32-bit frame.
        */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_STEREO
        ),

        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(rx_chan, &std_cfg),
        TAG2,
        "Failed to initialize I2S standard mode"
    );

    ESP_RETURN_ON_ERROR(
        i2s_channel_enable(rx_chan),
        TAG2,
        "Failed to enable I2S RX channel"
    );

    return ESP_OK;
}

static int32_t convert_inmp441_sample(int32_t raw)
{
    /*
       The INMP441 24-bit sample is normally inside the upper bits
       of the 32-bit word. Shifting by 8 gives a signed 24-bit-style value.

       Depending on module/driver alignment, the raw value may already look
       sign-extended. This conversion is good for level testing.
    */
    return raw >> 8;
}









// =============================== HUB75 GLOBAL OBJECTS ===============================

static Hub75Config config = make_config();
static Hub75Driver driver(config);

//================================ GLOBALS =======================================
static bool g_startup_screen_active = true;
static int64_t g_startup_screen_until_us = 0;

static bool g_logo_screen_active = true;
static int64_t g_logo_screen_until_us = 0;

// =============================== SHARED DATA ===============================

static char g_message[32] = {0};
static bool g_message_active = false;
static int64_t g_message_until_us = 0;


static int brightness_level = 5;        // 1 to 10
static int temporal_brightness = 5;     // temporary menu value while editing

// =============================== ETHERNET DATA ===============================

static int g_eth_brightness_level = 5;
static bool g_eth_brightness_pending = false;

static bool g_eth_format_pending = false;
static hour_format_t g_eth_format = FORMAT_12H;

static bool g_eth_time_pending = false;
static ds3231_time_t g_eth_time = {};

#define DEFAULT_BRIGHTNESS_LEVEL  5
#define DEFAULT_CLOCK_FORMAT      FORMAT_12H
#define DEFAULT_DISPLAY_MODE      MODE_ROTATION

static bool g_eth_factory_reset_pending = false;


// ===========================================================================================================================


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



// =============================== DEFAULT RTC TIME ===============================

static const ds3231_time_t default_time = {
    .second = 0,
    .minute = 40,
    .hour = 14,
    .day_of_week = 4,
    .day = 27,
    .month = 5,
    .year = 2026,
};



static ds3231_time_t g_now = {
    .second = 0,
    .minute = 0,
    .hour = 0,
    .day_of_week = 1,
    .day = 1,
    .month = 1,
    .year = 2000,
};
static float g_temp_c = 0.0f;
static bool g_rtc_valid = false;
static bool g_temp_valid = false;

static portMUX_TYPE g_data_mux = portMUX_INITIALIZER_UNLOCKED;

// =============================== CLOCK / TEMP HELPERS ===============================
static hour_format_t clock_format = FORMAT_12H;



// =============================== DISPLAY MODES ===============================

static display_mode_t display_mode = MODE_1;


static void show_temp_message(const char *msg, uint32_t duration_ms)
{
    if (!msg) {
        return;
    }

    portENTER_CRITICAL(&g_data_mux);

    snprintf(g_message, sizeof(g_message), "%s", msg);
    g_message_active = true;
    g_message_until_us = esp_timer_get_time() + ((int64_t)duration_ms * 1000);

    portEXIT_CRITICAL(&g_data_mux);
}





static void start_logo_screen(uint32_t duration_ms)
{
    g_logo_screen_active = true;
    g_logo_screen_until_us = esp_timer_get_time() + ((int64_t)duration_ms * 1000);
}





// =============================== DISPLAY TASK ===============================

void display_update_task(void* pvParameters)
{
    Hub75Driver* driver = (Hub75Driver*)pvParameters;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(33); // ~30 FPS
	
	display_mode_t mode_copy;
	char message_copy[32];
	bool message_active_copy;
	
	bool menu_active_copy;
	
	bool startup_screen_active_copy;
	
	bool logo_screen_active_copy;

    while (true) {
		clock_menu_check_timeout();		
		
		bool factory_reset_pending_copy = false;

		portENTER_CRITICAL(&g_data_mux);

		if (g_eth_factory_reset_pending) {
		    factory_reset_pending_copy = true;
		    g_eth_factory_reset_pending = false;
		}

		portEXIT_CRITICAL(&g_data_mux);

		if (factory_reset_pending_copy) {
		    ESP_LOGW(TAG, "Applying factory reset from Ethernet");

			portENTER_CRITICAL(&g_data_mux);

			brightness_level = DEFAULT_BRIGHTNESS_LEVEL;
			temporal_brightness = DEFAULT_BRIGHTNESS_LEVEL;
			clock_format = DEFAULT_CLOCK_FORMAT;
			display_mode = DEFAULT_DISPLAY_MODE;

			g_eth_brightness_pending = false;
			g_eth_format_pending = false;
			g_eth_time_pending = false;

			portEXIT_CRITICAL(&g_data_mux);

			driver->set_brightness(
			    brightness_level_to_hub75(DEFAULT_BRIGHTNESS_LEVEL)
			);

			clock_settings_save_brightness(DEFAULT_BRIGHTNESS_LEVEL);
			clock_settings_save_format((uint8_t)DEFAULT_CLOCK_FORMAT);
			clock_settings_save_mode((uint8_t)DEFAULT_DISPLAY_MODE);

			esp_err_t alarm_save_ret = clock_alarm_clear_all_and_save();

		    if (alarm_save_ret != ESP_OK) {
		        ESP_LOGE(TAG,
		                 "Failed to save cleared alarms after factory reset: %s",
		                 esp_err_to_name(alarm_save_ret));
		    }

		    ESP_LOGW(TAG, "Factory reset from Ethernet applied");

		    show_temp_message("RESET", 1000);
		}
		

		clock_alarm_process_deferred_save();
		
		bool eth_brightness_pending_copy = false;
		int eth_brightness_level_copy = 5;

		bool eth_format_pending_copy = false;
		hour_format_t eth_format_copy = FORMAT_12H;

		portENTER_CRITICAL(&g_data_mux);

		if (g_eth_brightness_pending) {
		    eth_brightness_pending_copy = true;
		    eth_brightness_level_copy = g_eth_brightness_level;
		    g_eth_brightness_pending = false;
		}

		if (g_eth_format_pending) {
		    eth_format_pending_copy = true;
		    eth_format_copy = g_eth_format;
		    g_eth_format_pending = false;
		}

		portEXIT_CRITICAL(&g_data_mux);

		if (eth_brightness_pending_copy) {
		    uint8_t hub75_brightness =
		        brightness_level_to_hub75(eth_brightness_level_copy);

		    driver->set_brightness(hub75_brightness);

		    portENTER_CRITICAL(&g_data_mux);
		    brightness_level = eth_brightness_level_copy;
		    temporal_brightness = eth_brightness_level_copy;
		    portEXIT_CRITICAL(&g_data_mux);

		    clock_settings_save_brightness(eth_brightness_level_copy);

		    ESP_LOGI(TAG,
		             "Brightness applied from Ethernet: level=%d hub75=%u",
		             eth_brightness_level_copy,
		             hub75_brightness);
		}

		if (eth_format_pending_copy) {
		    portENTER_CRITICAL(&g_data_mux);
		    clock_format = eth_format_copy;
		    portEXIT_CRITICAL(&g_data_mux);

		    clock_settings_save_format((uint8_t)eth_format_copy);

		    ESP_LOGI(TAG,
		             "Clock format applied from Ethernet: %s",
		             eth_format_copy == FORMAT_24H ? "24H" : "12H");
		}

		
		ds3231_time_t now_copy;
		float temp_copy;
		bool rtc_valid_copy;
		bool temp_valid_copy;
		hour_format_t format_copy;

		portENTER_CRITICAL(&g_data_mux);

		now_copy = g_now;
		temp_copy = g_temp_c;
		rtc_valid_copy = g_rtc_valid;
		temp_valid_copy = g_temp_valid;
		format_copy = clock_format;
		mode_copy = display_mode;

		menu_active_copy = clock_menu_is_active();

		startup_screen_active_copy = g_startup_screen_active;

		if (g_startup_screen_active && esp_timer_get_time() > g_startup_screen_until_us) {
		    g_startup_screen_active = false;
		    startup_screen_active_copy = false;
		}

		message_active_copy = g_message_active;
		snprintf(message_copy, sizeof(message_copy), "%s", g_message);

		if (g_message_active && esp_timer_get_time() > g_message_until_us) {
		    g_message_active = false;
		    message_active_copy = false;
		}
		
		logo_screen_active_copy = g_logo_screen_active;

		if (g_logo_screen_active && esp_timer_get_time() > g_logo_screen_until_us) {
		    g_logo_screen_active = false;
		    logo_screen_active_copy = false;

		    /*
		     * Start the settings screen after logo finishes.
		     */
		    g_startup_screen_active = true;
		    g_startup_screen_until_us = esp_timer_get_time() + (3000 * 1000);
		}

		portEXIT_CRITICAL(&g_data_mux);
		
		
		
		if (rtc_valid_copy) {
		    clock_alarm_check_trigger(&now_copy);
		}

		clock_alarm_runtime_update();

		clock_alarm_display_state_t alarm_state = {};

		bool alarm_active_copy =
		    clock_alarm_get_display_state(&alarm_state);

		alarm_effect_t alarm_effect_copy = alarm_state.effect;
		int alarm_id_copy = alarm_state.alarm_id;

		driver->clear();
		
	


		if (alarm_active_copy) {
		    bool show_alarm = true;

			if (alarm_effect_copy == ALARM_EFFECT_INTERMITENTE ||
			    alarm_effect_copy == ALARM_EFFECT_PARPADEO_CONTINUO ||
			    alarm_effect_copy == ALARM_EFFECT_PARPADEO_INTERMITENTE) {
			    show_alarm = ((esp_timer_get_time() / 300000) % 2) == 0;
			}

		    if (show_alarm) {
		        char alarm_msg[16];
		        snprintf(alarm_msg, sizeof(alarm_msg), "AL %02d", alarm_id_copy);

		        draw_string(*driver,
		                    clock_display_center_x_6x9(alarm_msg),
		                    8,
		                    alarm_msg,
		                    255,
		                    0,
		                    0);
		    }

		    driver->flip_buffer();

		    vTaskDelayUntil(&xLastWakeTime, xFrequency);
		    continue;
		}		
		
		if (logo_screen_active_copy) {
		    scroll_stop();

		    clock_display_draw_logo(driver);

		    driver->flip_buffer();

		    vTaskDelayUntil(&xLastWakeTime, xFrequency);
		    continue;
		}
		
		if (startup_screen_active_copy) {
		    scroll_stop();

			clock_display_draw_startup(driver,
			                           display_mode,
			                           brightness_level,
			                           clock_format);

		    driver->flip_buffer();

		    vTaskDelayUntil(&xLastWakeTime, xFrequency);
		    continue;
		}

		if (message_active_copy) {
		    draw_string(*driver, clock_display_center_x_6x9(message_copy), 8, message_copy, 255, 0, 0);
		    driver->flip_buffer();

		    vTaskDelayUntil(&xLastWakeTime, xFrequency);
		    continue;
		}
		
		if (menu_active_copy) {
		    scroll_stop();
		    clock_menu_draw(driver);
		    driver->flip_buffer();

		    vTaskDelayUntil(&xLastWakeTime, xFrequency);
		    continue;
		}
		
		
		
		if (rtc_valid_copy) {
		    //switch (mode_copy) {
				
				
			switch (MODE_TEST) {
				
				
				
				
				
				
				case MODE_TEST:
				{
					/*
					fixed_item_t active_fixed_item = clock_modes_get_fixed_item();
					
					if (active_fixed_item == FIXED_ITEM_LOGO) {
					    scroll_stop();
					    clock_display_draw_logo(driver);
					} else {
						clock_display_draw_mode_test(driver,
						                          &now_copy,
						                          temp_copy,
						                          temp_valid_copy,
						                          format_copy, dB_value);
					}
					*/
					clock_display_draw_mode_test(driver,
					                          &now_copy,
					                          temp_copy,
					                          temp_valid_copy,
					                          format_copy, dB_value);

				    break;
				}				
				
				
				
				
				
				
				
				
				
				
				case MODE_1:
				{
				    fixed_item_t active_fixed_item = clock_modes_get_fixed_item();

				    if (active_fixed_item == FIXED_ITEM_LOGO) {
				        scroll_stop();
				        clock_display_draw_logo(driver);
				    } else {
						clock_display_draw_mode_1(driver,
						                          &now_copy,
						                          temp_copy,
						                          temp_valid_copy,
						                          format_copy);
				    }

				    break;
				}

				case MODE_2:
				{
				    fixed_item_t active_fixed_item = clock_modes_get_fixed_item();

				    if (active_fixed_item == FIXED_ITEM_LOGO) {
				        scroll_stop();
				        clock_display_draw_logo(driver);
				    } else {
				        clock_display_draw_mode_2(driver,
				                                  &now_copy,
				                                  temp_copy,
				                                  temp_valid_copy,
				                                  format_copy);
				    }

				    break;
				}

				case MODE_3:
				{
				    fixed_item_t active_fixed_item = clock_modes_get_fixed_item();

				    if (active_fixed_item == FIXED_ITEM_LOGO) {
				        scroll_stop();
				        clock_display_draw_logo(driver);
				    } else {
				        scroll_stop();
						clock_display_draw_mode_3(driver,
						                          &now_copy,
						                          temp_copy,
						                          temp_valid_copy,
						                          format_copy);
				    }

				    break;
				}

					case MODE_ROTATION:
					{
					    rotation_item_t active_rotation_item = clock_modes_get_rotation_item();

					    switch (active_rotation_item)
					    {
					        case ROT_ITEM_LOGO:
					            scroll_stop();
					            clock_display_draw_logo(driver);
					            break;

					        case ROT_ITEM_MODE_1:
							clock_display_draw_mode_1(driver,
							                          &now_copy,
							                          temp_copy,
							                          temp_valid_copy,
							                          format_copy);
					            break;

							case ROT_ITEM_MODE_2:
							    clock_display_draw_mode_2(driver,
							                              &now_copy,
							                              temp_copy,
							                              temp_valid_copy,
							                              format_copy);
							    break;

					        case ROT_ITEM_MODE_3:
					        default:
					            scroll_stop();
								clock_display_draw_mode_3(driver,
								                          &now_copy,
								                          temp_copy,
								                          temp_valid_copy,
								                          format_copy);
					            break;
					    }

					    break;
					}

					default:
					clock_display_draw_mode_1(driver,
					                          &now_copy,
					                          temp_copy,
					                          temp_valid_copy,
					                          format_copy);
					    break;
		    }
		} else {
		    scroll_stop();
		    draw_string(*driver, 2, 2, "NO RTC", 255, 0, 0);
		}

		driver->flip_buffer();

		vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// =============================== RTC TASK ===============================

void rtc_task(void* pvParameters)
{
    ds3231_dev_t* rtc = (ds3231_dev_t*)pvParameters;

    while (true) {
		
		
		bool eth_time_pending_copy = false;
		ds3231_time_t eth_time_copy = {};

		portENTER_CRITICAL(&g_data_mux);

		if (g_eth_time_pending) {
		    eth_time_pending_copy = true;
		    eth_time_copy = g_eth_time;
		    g_eth_time_pending = false;
		}

		portEXIT_CRITICAL(&g_data_mux);

		if (eth_time_pending_copy) {
		    esp_err_t set_ret = ds3231_set_time(rtc, &eth_time_copy);

		    if (set_ret == ESP_OK) {
		        portENTER_CRITICAL(&g_data_mux);
		        g_now = eth_time_copy;
		        g_rtc_valid = true;
		        portEXIT_CRITICAL(&g_data_mux);

		        ESP_LOGI(TAG,
		                 "RTC updated from Ethernet: %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
		                 eth_time_copy.year,
		                 eth_time_copy.month,
		                 eth_time_copy.day,
		                 eth_time_copy.hour,
		                 eth_time_copy.minute,
		                 eth_time_copy.second,
		                 eth_time_copy.day_of_week);
		    } else {
		        ESP_LOGE(TAG,
		                 "Failed to update RTC from Ethernet: %s",
		                 esp_err_to_name(set_ret));
		    }
		}
		
		
        ds3231_time_t now;

        if (ds3231_get_time(rtc, &now) == ESP_OK) {
            portENTER_CRITICAL(&g_data_mux);
            g_now = now;
            g_rtc_valid = true;
            portEXIT_CRITICAL(&g_data_mux);
        } else {
            ESP_LOGE(TAG, "Failed to read DS3231");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// =============================== DS18B20 TASK ===============================

void ds18b20_task(void* pvParameters)
{
    ds18b20_t* sensor = (ds18b20_t*)pvParameters;

    while (true) {
        float temp_c = 0.0f;

        esp_err_t ret = ds18b20_read_temperature(sensor, &temp_c);

        if (ret == ESP_OK) {
            portENTER_CRITICAL(&g_data_mux);
            g_temp_c = temp_c;
            g_temp_valid = true;
            portEXIT_CRITICAL(&g_data_mux);			
        } else if (ret == ESP_ERR_NOT_FOUND) {
            portENTER_CRITICAL(&g_data_mux);
            g_temp_valid = false;
            portEXIT_CRITICAL(&g_data_mux);

            //ESP_LOGW(TAG, "DS18B20 not connected");
        } else {
            ESP_LOGE(TAG, "DS18B20 read failed: %s", esp_err_to_name(ret));
        }

        /*
         * ds18b20_read_temperature() already waits 750 ms.
         * 4250 ms + 750 ms = about 5000 ms total period.
         */
        vTaskDelay(pdMS_TO_TICKS(4250));
    }
}

static void handle_normal_button(button_t btn, ds3231_dev_t *rtc)
{
    switch (btn)
    {
        case BTN_MENU:
            clock_menu_enter();
            break;

        case BTN_UP:
        {
            display_mode_t new_mode;

            portENTER_CRITICAL(&g_data_mux);

            if (display_mode >= MODE_ROTATION) {
                display_mode = MODE_1;
            } else {
                display_mode = (display_mode_t)(display_mode + 1);
            }

            new_mode = display_mode;

			clock_modes_reset_sequences();

            portEXIT_CRITICAL(&g_data_mux);

            clock_settings_save_mode((uint8_t)new_mode);

            scroll_stop();

            char msg[16];
            snprintf(msg, sizeof(msg), "MODO:%d", new_mode);
            show_temp_message(msg, 1000);

            ESP_LOGI(TAG, "Display mode changed to %d", new_mode);

            break;
        }

        case BTN_DOWN:
        {
            hour_format_t new_format;

            portENTER_CRITICAL(&g_data_mux);

            if (clock_format == FORMAT_12H) {
                clock_format = FORMAT_24H;
            } else {
                clock_format = FORMAT_12H;
            }

            new_format = clock_format;

            portEXIT_CRITICAL(&g_data_mux);

            clock_settings_save_format((uint8_t)new_format);

            show_temp_message(new_format == FORMAT_24H ? "24HRS:ON" : "24HRS:OFF",
                              1000);

            ESP_LOGI(TAG,
                     "Clock format changed to %s",
                     new_format == FORMAT_24H ? "24H" : "12H");

            break;
        }

        default:
            break;
    }
}

void button_task(void *arg)
{
    ds3231_dev_t *rtc = (ds3231_dev_t *)arg;
	QueueHandle_t button_queue = clock_buttons_get_queue();


    TickType_t last_press_time[3] = {
        0,
        0,
        0
    };

    button_t pending_hold_btn = BTN_NONE;
    TickType_t pending_hold_start = 0;

    bool ignore_until_release = false;
	
	button_t menu_repeat_btn = BTN_NONE;
	TickType_t menu_repeat_start = 0;
	TickType_t menu_last_repeat = 0;

    while (true)
    {
        button_t btn;
        TickType_t now = xTaskGetTickCount();

        /*
         * Use short timeout instead of portMAX_DELAY so the task can check
         * whether a pending button has been held long enough.
         */
		 if (xQueueReceive(button_queue, &btn, pdMS_TO_TICKS(10)))
		 {
		     if (btn < BTN_MENU || btn > BTN_DOWN) {
		         continue;
		     }

		     /*
		      * Important:
		      * After a hold action outside the menu, ignore any queued/bounce events
		      * until all buttons are released.
		      */
		     if (ignore_until_release) {
		         continue;
		     }

		     if ((now - last_press_time[btn]) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
		         continue;
		     }

		     last_press_time[btn] = now;

		     /*
		      * Inside menu:
		      * Buttons act immediately.
		      */
			  if (clock_menu_is_active()) 
			  {			  
				clock_menu_handle_button(btn);

			      /*
			       * Inside menu, only UP and DOWN repeat.
			       * MENU should not repeat, because it changes fields.
			       */
			      if (btn == BTN_UP || btn == BTN_DOWN) {
			          menu_repeat_btn = btn;
			          menu_repeat_start = now;
			          menu_last_repeat = now;
			      }

			      continue;
			  }

		     /*
		      * Outside menu:
		      * Do not execute immediately.
		      * Start hold detection.
		      */
		     pending_hold_btn = btn;
		     pending_hold_start = now;

		     ESP_LOGI(TAG, "Button %d pressed, waiting for hold", btn);
		 }	 		 
		 
		 /*
		  * Inside menu:
		  * Auto-repeat UP/DOWN while held.
		  */
		  if (clock_menu_is_active() &&
		      menu_repeat_btn != BTN_NONE)
		 {
		     if (clock_button_is_pressed(menu_repeat_btn))
		     {
		         now = xTaskGetTickCount();

		         if ((now - menu_repeat_start) >= pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS))
		         {
		             if ((now - menu_last_repeat) >= pdMS_TO_TICKS(BUTTON_REPEAT_RATE_MS))
		             {
						clock_menu_handle_button(btn);
		                 menu_last_repeat = now;
		             }
		         }
		     }
		     else
		     {
		         menu_repeat_btn = BTN_NONE;
		     }
		 } 		 

        /*
         * Outside menu only:
         * Execute action after the button stays pressed for BUTTON_HOLD_MS.
         */
		 if (!clock_menu_is_active() &&
		     pending_hold_btn != BTN_NONE &&
		     !ignore_until_release)
        {
            if (clock_button_is_pressed(pending_hold_btn))
            {
                now = xTaskGetTickCount();

				if ((now - pending_hold_start) >= pdMS_TO_TICKS(BUTTON_HOLD_MS))
				{
				    ESP_LOGI(TAG, "Button %d hold accepted", pending_hold_btn);

				    handle_normal_button(pending_hold_btn, rtc);

				    /*
				     * Remove any queued bounce/repeat events generated during the hold.
				     */
				    xQueueReset(button_queue);

				    /*
				     * Prevent repeated triggers while the button remains held.
				     * Also prevents MENU from immediately advancing from BRILLO to HORA.
				     */
				    pending_hold_btn = BTN_NONE;
				    ignore_until_release = true;
				}
            }
            else
            {
                /*
                 * Button was released before hold time.
                 * Cancel action.
                 */
                ESP_LOGI(TAG, "Button hold cancelled");

                pending_hold_btn = BTN_NONE;
            }
        }

        /*
         * Re-arm buttons only after all are released.
         */
		 if (clock_buttons_all_released())
		 {
		     pending_hold_btn = BTN_NONE;
		     menu_repeat_btn = BTN_NONE;
		     ignore_until_release = false;
		 }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void check_or_set_default_rtc(ds3231_dev_t *rtc)
{
    if (!rtc) {
        return;
    }

    ds3231_time_t now;

    esp_err_t ret = ds3231_get_time(rtc, &now);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC during startup check: %s",
                 esp_err_to_name(ret));
        return;
    }

    //if (rtc_time_is_valid(&now)) {
	if (!rtc_time_is_valid(&now)) {
        ESP_LOGW(TAG,
                 "RTC time invalid: %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
                 now.year,
                 now.month,
                 now.day,
                 now.hour,
                 now.minute,
                 now.second,
                 now.day_of_week);

        ds3231_time_t fixed_time = default_time;

        fixed_time.day_of_week = clock_menu_calculate_weekday(
            fixed_time.day,
            fixed_time.month,
            fixed_time.year
        );

        ret = ds3231_set_time(rtc, &fixed_time);

        if (ret == ESP_OK) {
            ESP_LOGW(TAG,
                     "RTC set to default: %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
                     fixed_time.year,
                     fixed_time.month,
                     fixed_time.day,
                     fixed_time.hour,
                     fixed_time.minute,
                     fixed_time.second,
                     fixed_time.day_of_week);

            portENTER_CRITICAL(&g_data_mux);
            g_now = fixed_time;
            g_rtc_valid = true;
            portEXIT_CRITICAL(&g_data_mux);
        } else {
            ESP_LOGE(TAG, "Failed to set default RTC time: %s",
                     esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG,
                 "RTC startup time valid: %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
                 now.year,
                 now.month,
                 now.day,
                 now.hour,
                 now.minute,
                 now.second,
                 now.day_of_week);
    }
}



static int32_t i2s_buffer[READ_FRAMES * 2];

static bool level_initialized = false;
static double dbfs_smooth = -120.0;

static int startup_skip = 5;


#define LEQ_WINDOW_BLOCKS  20

static double leq_accum_energy = 0.0;
static int leq_count = 0;

/*
static bool leq_add_sample(double spl_db, double *leq_out)
{
    leq_accum_energy += pow(10.0, spl_db / 10.0);
    leq_count++;

    if (leq_count >= LEQ_WINDOW_BLOCKS) {
        *leq_out = 10.0 * log10(leq_accum_energy / (double)leq_count);
        leq_accum_energy = 0.0;
        leq_count = 0;
        return true;
    }

    return false;
}
*/



// =============================== APP MAIN ===============================

extern "C" void app_main(void)
{	
	//============== Initialize Interrupt service routine if no Ethernet present ======================= //	
		
	esp_err_t isr_ret = gpio_install_isr_service(0);

	if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
	    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_ret));
	    return;
	}
			
	//================================================================================================================ //	
	
	ESP_LOGI(TAG, "Starting HUB75");

    if (!driver.begin()) {
        ESP_LOGE(TAG, "Driver start failed");
        return;
    }
	
	ESP_ERROR_CHECK(clock_settings_init());	
	
	ESP_ERROR_CHECK(clock_alarm_init(ALARM_GPIO));		
	
	ESP_ERROR_CHECK(clock_alarm_load());	
	

	uint8_t saved_format = clock_settings_load_format((uint8_t)FORMAT_12H);
	if (saved_format > FORMAT_24H) {
	    saved_format = FORMAT_12H;
	}
	clock_format = (hour_format_t)saved_format;

	uint8_t saved_mode = clock_settings_load_mode((uint8_t)MODE_1);
	if (saved_mode < MODE_1 || saved_mode > MODE_ROTATION) {
	    saved_mode = MODE_1;
	}
	display_mode = (display_mode_t)saved_mode;

	brightness_level = clock_settings_load_brightness(5);

	if (brightness_level < 1) {
	    brightness_level = 1;
	}

	if (brightness_level > 10) {
	    brightness_level = 10;
	}

	temporal_brightness = brightness_level;

	driver.set_brightness(brightness_level_to_hub75(brightness_level));	
	
	start_logo_screen(3000);	

    static ds18b20_t ambient_sensor;
    static ds3231_dev_t rtc;

    ESP_ERROR_CHECK(ds18b20_init(&ambient_sensor, DS18B20_GPIO));
    ESP_ERROR_CHECK(init_ds3231(&rtc));
	
	check_or_set_default_rtc(&rtc);
	
	
	clock_menu_context_t menu_ctx = {
	    .driver = &driver,
	    .rtc = &rtc,

	    .brightness_level = &brightness_level,
	    .temporal_brightness = &temporal_brightness,

	    .data_mux = &g_data_mux,
	    .g_now = &g_now,
	    .g_rtc_valid = &g_rtc_valid,

	    .show_message = show_temp_message,
	};

	clock_menu_init(&menu_ctx);
	
	ESP_ERROR_CHECK(clock_buttons_init(PIN_MENU, PIN_UP, PIN_DOWN));
	
	xTaskCreatePinnedToCore(
	    button_task,
	    "ButtonTask",
	    4096,
	    &rtc,
	    2,
	    NULL,
	    0
	);

    xTaskCreatePinnedToCore(
        display_update_task,
        "DisplayTask",
        8192,
        &driver,
        2,
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        rtc_task,
        "RtcTask",
        4096,
        &rtc,
        1,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        ds18b20_task,
        "DS18B20Task",
        4096,
        &ambient_sensor,
        1,
        NULL,
        0
    );
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	 ESP_ERROR_CHECK(i2s_mic_init());

	    ESP_LOGI(TAG2, "INMP441 test started");
	    ESP_LOGI(TAG2, "Sample rate: %d Hz", SAMPLE_RATE_HZ);
	    ESP_LOGI(TAG2, "BCLK GPIO: %d, WS GPIO: %d, DIN GPIO: %d",
	             I2S_BCLK_IO, I2S_WS_IO, I2S_DIN_IO);

	#if MIC_IS_LEFT_CHANNEL
	    ESP_LOGI(TAG2, "Reading LEFT channel. Connect INMP441 L/R pin to GND.");
	#else
	    ESP_LOGI(TAG2, "Reading RIGHT channel. Connect INMP441 L/R pin to 3V3.");
	#endif

	    /*
	       Stereo frame:
	       buffer[0] = left sample
	       buffer[1] = right sample
	       buffer[2] = left sample
	       buffer[3] = right sample
	       ...
		   
		   int64_t sum_sq = 0;
		   double sum_sq_weighted = 0.0;
	    */	
		
		test_a_weighting_filter();	

	    while (1) {
	        size_t bytes_read = 0;

	        esp_err_t ret = i2s_channel_read(
	            rx_chan,
	            i2s_buffer,
	            sizeof(i2s_buffer),
	            &bytes_read,
	            pdMS_TO_TICKS(1000)
	        );

	        if (ret != ESP_OK || bytes_read == 0) {
	            ESP_LOGW(TAG2, "I2S read failed or timeout: %s", esp_err_to_name(ret));
	            continue;
	        }

	        int frames_read = bytes_read / (sizeof(int32_t) * 2);

			int64_t sum = 0;

			for (int i = 0; i < frames_read; i++) {
			#if MIC_IS_LEFT_CHANNEL
			    int32_t raw = i2s_buffer[i * 2 + 0];
			#else
			    int32_t raw = i2s_buffer[i * 2 + 1];
			#endif

			    int32_t sample = convert_inmp441_sample(raw);
			    sum += sample;
			}

			int32_t dc_offset = frames_read > 0 ? (int32_t)(sum / frames_read) : 0;

			/* Reset these every audio block */
			int64_t sum_sq = 0;
			double sum_sq_weighted = 0.0;
			int32_t peak = 0;

			for (int i = 0; i < frames_read; i++) {
			#if MIC_IS_LEFT_CHANNEL
			    int32_t raw = i2s_buffer[i * 2 + 0];
			#else
			    int32_t raw = i2s_buffer[i * 2 + 1];
			#endif

			    int32_t sample = convert_inmp441_sample(raw);
			    int32_t centered = sample - dc_offset;

			    int32_t abs_sample = centered >= 0 ? centered : -centered;
			    if (abs_sample > peak) {
			        peak = abs_sample;
			    }

			    /* Unweighted RMS */
			    sum_sq += (int64_t)centered * (int64_t)centered;

			    /* A-weighted RMS */
			    double weighted = a_weight_process((double)centered);
			    sum_sq_weighted += weighted * weighted;
			}

			double rms = 0.0;
			double rms_a = 0.0;

			if (frames_read > 0) {
			    rms = sqrt((double)sum_sq / (double)frames_read);
			    rms_a = sqrt(sum_sq_weighted / (double)frames_read);
			}

			double dbfs = rms_to_dbfs(rms);
			double dbfs_a = rms_to_dbfs(rms_a);

			//si double spl_db = dbfs + USER_CAL_OFFSET_DB;       // unweighted dB SPL
			double spl_dba = dbfs_a + USER_CAL_OFFSET_DBA;   // A-weighted dBA

			if (spl_startup_skip > 0) {
			    spl_startup_skip--;
			    ESP_LOGI(TAG,
			             "Skipping startup SPL sample: raw=%.1f dBFS, A=%.1f dBFS, SPL_A=%.1f dBA",
			             dbfs,
			             dbfs_a,
			             spl_dba);
			    continue;
			}
					 
			dB_value = (int)(spl_dba + 0.5);
			
	        vTaskDelay(pdMS_TO_TICKS(LOG_INTERVAL_MS));		

	    }	
}

//Sine Wave 1000 Hz / 1 kHz for 1 hour - Test Tone





/*
double spl_dba_smooth = smooth_spl(spl_dba);

double leq_dba = 0.0;
if (leq_add_sample(spl_dba, &leq_dba)) {
    ESP_LOGI(TAG, "Leq=%.1f dBA", leq_dba);
}

ESP_LOGI(TAG,
         "dc=%" PRId32 ", peak=%" PRId32
         ", rms=%.1f, rmsA=%.1f"
         ", raw=%.1f dBFS, A=%.1f dBFS"
         ", SPL=%.1f dB, SPL_A=%.1f dBA, smooth=%.1f dBA",
         dc_offset,
         peak,
         rms,
         rms_a,
         dbfs,
         dbfs_a,
         spl_db,
         spl_dba,
         spl_dba_smooth);
		 
		 
		 double spl_dba_smooth = smooth_spl(spl_dba);
		 ESP_LOGI(TAG,
		          "SPL_A=%.1f dBA, meter=%.1f dBA",
		          spl_dba,
		          spl_dba_smooth);

	int spl_display = (int)(spl_db_smooth + 0.5);
	int leq_display = (int)(leq_db + 0.5);
*/


//=============================================================== ETHERNET SECTION ===========================================================//	
/*
ESP_LOGI(TAG, "Starting Ethernet");
ESP_ERROR_CHECK(clock_ethernet_init_static());

clock_protocol_context_t protocol_ctx = {
    .brightness_level = &brightness_level,
    .eth_brightness_pending = &g_eth_brightness_pending,
    .eth_brightness_level = &g_eth_brightness_level,
    .eth_format_pending = &g_eth_format_pending,
    .eth_format = &g_eth_format,
    .eth_time_pending = &g_eth_time_pending,
    .eth_time = &g_eth_time,
    .factory_reset_pending = &g_eth_factory_reset_pending,
    .now = &g_now,
    .rtc_valid = &g_rtc_valid,
    .clock_format = &clock_format,
    .data_mux = &g_data_mux,
};

clock_protocol_init(&protocol_ctx);

ESP_ERROR_CHECK(clock_ethernet_start_tcp_server(clock_protocol_rx_callback));
*/
//====================================================================================================================================================//

