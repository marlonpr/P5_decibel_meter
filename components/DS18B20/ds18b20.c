#include "ds18b20.h"

#include "esp_log.h"
#include "esp_rom_sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DS18B20_CMD_CONVERT_T     0x44
#define DS18B20_CMD_READ_SCRATCH  0xBE
#define DS18B20_CMD_SKIP_ROM      0xCC

static const char *TAG = "DS18B20";

/*
 * Optional but useful:
 * protects short 1-Wire timing slots from task switching.
 */
static portMUX_TYPE ds18b20_mux = portMUX_INITIALIZER_UNLOCKED;

/*
 * In open-drain mode:
 * gpio_set_level(pin, 0) = pull bus LOW
 * gpio_set_level(pin, 1) = release bus HIGH through pull-up
 */

static inline void ds18b20_drive_low(ds18b20_t *dev)
{
    gpio_set_level(dev->pin, 0);
}

static inline void ds18b20_release(ds18b20_t *dev)
{
    gpio_set_level(dev->pin, 1);
}

static void ds18b20_write_bit(ds18b20_t *dev, int bit)
{
    portENTER_CRITICAL(&ds18b20_mux);

    if (bit) {
        ds18b20_drive_low(dev);
        esp_rom_delay_us(6);

        ds18b20_release(dev);
        esp_rom_delay_us(64);
    } else {
        ds18b20_drive_low(dev);
        esp_rom_delay_us(60);

        ds18b20_release(dev);
        esp_rom_delay_us(10);
    }

    portEXIT_CRITICAL(&ds18b20_mux);
}

static int ds18b20_read_bit(ds18b20_t *dev)
{
    int bit;

    portENTER_CRITICAL(&ds18b20_mux);

    ds18b20_drive_low(dev);
    esp_rom_delay_us(6);

    ds18b20_release(dev);
    esp_rom_delay_us(9);

    bit = gpio_get_level(dev->pin);

    esp_rom_delay_us(55);

    portEXIT_CRITICAL(&ds18b20_mux);

    return bit;
}

static void ds18b20_write_byte(ds18b20_t *dev, uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(dev, data & 0x01);
        data >>= 1;
    }
}

static uint8_t ds18b20_read_byte(ds18b20_t *dev)
{
    uint8_t data = 0;

    for (int i = 0; i < 8; i++) {
        data >>= 1;

        if (ds18b20_read_bit(dev)) {
            data |= 0x80;
        }
    }

    return data;
}

static bool ds18b20_reset(ds18b20_t *dev)
{
    bool presence;

    portENTER_CRITICAL(&ds18b20_mux);

    ds18b20_drive_low(dev);
    esp_rom_delay_us(480);

    ds18b20_release(dev);
    esp_rom_delay_us(70);

    presence = gpio_get_level(dev->pin) == 0;

    esp_rom_delay_us(410);

    portEXIT_CRITICAL(&ds18b20_mux);

    return presence;
}

esp_err_t ds18b20_init(ds18b20_t *sensor, gpio_num_t pin)
{
    if (!sensor) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor->pin = pin;
    sensor->present = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ds18b20_release(sensor);

    sensor->present = ds18b20_reset(sensor);

    if (!sensor->present) {
        ESP_LOGW(TAG, "DS18B20 not detected on GPIO%d", pin);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "DS18B20 detected on GPIO%d", pin);

    return ESP_OK;
}

esp_err_t ds18b20_read_temperature(ds18b20_t *sensor, float *temperature)
{
    if (!sensor || !temperature) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor->present) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!ds18b20_reset(sensor)) {
        sensor->present = false;
        ESP_LOGW(TAG, "Sensor disappeared");
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(sensor, DS18B20_CMD_CONVERT_T);

    /*
     * 750 ms = max conversion time for 12-bit resolution.
     * This delay does not need critical timing.
     */
    vTaskDelay(pdMS_TO_TICKS(750));

    if (!ds18b20_reset(sensor)) {
        sensor->present = false;
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(sensor, DS18B20_CMD_READ_SCRATCH);

    uint8_t lsb = ds18b20_read_byte(sensor);
    uint8_t msb = ds18b20_read_byte(sensor);

    int16_t raw_temp = (int16_t)((msb << 8) | lsb);

    *temperature = (float)raw_temp / 16.0f;

    return ESP_OK;
}

esp_err_t ds18b20_read_temperature_int(ds18b20_t *sensor, int16_t *temperature)
{
    if (!sensor || !temperature) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor->present) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!ds18b20_reset(sensor)) {
        sensor->present = false;
        ESP_LOGW(TAG, "Sensor disappeared");
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(sensor, DS18B20_CMD_CONVERT_T);

    vTaskDelay(pdMS_TO_TICKS(750));

    if (!ds18b20_reset(sensor)) {
        sensor->present = false;
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(sensor, DS18B20_CMD_READ_SCRATCH);

    uint8_t lsb = ds18b20_read_byte(sensor);
    uint8_t msb = ds18b20_read_byte(sensor);

    int16_t raw_temp = (int16_t)((msb << 8) | lsb);

    /*
     * Integer Celsius.
     * Example:
     * raw_temp = 0x0191 = 401
     * 401 / 16 = 25.0625 C
     * raw_temp >> 4 = 25
     */
    *temperature = raw_temp >> 4;

    return ESP_OK;
}

esp_err_t ds18b20_start_conversion(ds18b20_t *sensor)
{
    if (!sensor) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor->present) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!ds18b20_reset(sensor)) {
        sensor->present = false;
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(sensor, DS18B20_CMD_CONVERT_T);

    return ESP_OK;
}

esp_err_t ds18b20_read_scratchpad_temp(ds18b20_t *sensor, int16_t *temp_out)
{
    if (!sensor || !temp_out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor->present) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!ds18b20_reset(sensor)) {
        sensor->present = false;
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(sensor, DS18B20_CMD_READ_SCRATCH);

    uint8_t lsb = ds18b20_read_byte(sensor);
    uint8_t msb = ds18b20_read_byte(sensor);

    int16_t raw_temp = (int16_t)((msb << 8) | lsb);

    /*
     * Integer Celsius.
     * Handles negative temperatures correctly because raw_temp is signed.
     */
    *temp_out = raw_temp >> 4;

    return ESP_OK;
}