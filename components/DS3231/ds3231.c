#include "ds3231.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include "esp_log.h"
//#include "driver/gpio.h"

#define SDA_PIN GPIO_NUM_17
#define SCL_PIN GPIO_NUM_18

#define DS3231_ADDR 0x68
#define TAG "DS3231"

static inline uint8_t dec2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static inline uint8_t bcd2dec(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

esp_err_t init_ds3231(ds3231_dev_t *out_dev)
{
    if (!out_dev) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_dev, 0, sizeof(ds3231_dev_t));

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .glitch_ignore_cnt = 7,

        /*
         * If your DS3231 module already has pull-up resistors,
         * keep this false.
         *
         * If using a bare DS3231 chip, use external 4.7k pull-ups
         * to 3.3V on SDA and SCL.
         */
        .flags.enable_internal_pullup = false,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &out_dev->bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };

    ret = i2c_master_bus_add_device(out_dev->bus, &dev_cfg, &out_dev->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DS3231 device: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "DS3231 initialized on SDA=%d, SCL=%d", SDA_PIN, SCL_PIN);

    return ESP_OK;
}

esp_err_t ds3231_set_time(ds3231_dev_t *dev, const ds3231_time_t *time)
{
    if (!dev || !time) {
        return ESP_ERR_INVALID_ARG;
    }

    if (time->second > 59 ||
        time->minute > 59 ||
        time->hour > 23 ||
        time->day_of_week < 1 || time->day_of_week > 7 ||
        time->day < 1 || time->day > 31 ||
        time->month < 1 || time->month > 12 ||
        time->year < 2000 || time->year > 2099) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[8];

    buf[0] = 0x00;                         // Start register
    buf[1] = dec2bcd(time->second);
    buf[2] = dec2bcd(time->minute);
    buf[3] = dec2bcd(time->hour);          // 24-hour mode
    buf[4] = dec2bcd(time->day_of_week);   // 1-7
    buf[5] = dec2bcd(time->day);
    buf[6] = dec2bcd(time->month);
    buf[7] = dec2bcd(time->year % 100);

    return i2c_master_transmit(dev->dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

esp_err_t ds3231_get_time(ds3231_dev_t *dev, ds3231_time_t *time)
{
    if (!dev || !time) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = 0x00;
    uint8_t data[7];

    esp_err_t ret = i2c_master_transmit_receive(
        dev->dev,
        &reg,
        1,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read DS3231 time: %s", esp_err_to_name(ret));
        return ret;
    }

    time->second      = bcd2dec(data[0] & 0x7F);
    time->minute      = bcd2dec(data[1] & 0x7F);
    time->hour        = bcd2dec(data[2] & 0x3F);  // 24-hour mode
    time->day_of_week = bcd2dec(data[3] & 0x07);
    time->day         = bcd2dec(data[4] & 0x3F);
    time->month       = bcd2dec(data[5] & 0x1F);
    time->year        = 2000 + bcd2dec(data[6]);

    return ESP_OK;
}