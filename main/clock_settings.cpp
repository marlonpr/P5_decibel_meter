#include "clock_settings.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CLOCK_SETTINGS";

#define NVS_NAMESPACE      "clock_cfg"
#define NVS_KEY_FORMAT     "format"
#define NVS_KEY_MODE       "mode"
#define NVS_KEY_BRIGHTNESS "brightness"

#define NVS_KEY_ETH_ALARMS "eth_alarms"



esp_err_t clock_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    } else {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void save_u8_value(const char *key, uint8_t value)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed for key '%s': %s",
                 key,
                 esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_u8(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved %s=%u", key, value);
    } else {
        ESP_LOGE(TAG, "Save failed for key '%s': %s",
                 key,
                 esp_err_to_name(ret));
    }
}

static uint8_t load_u8_value(const char *key, uint8_t default_value)
{
    nvs_handle_t handle;
    uint8_t value = default_value;

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No NVS namespace, using default %s=%u",
                 key,
                 default_value);
        return default_value;
    }

    ret = nvs_get_u8(handle, key, &value);
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Key '%s' not found, using default %u",
                 key,
                 default_value);
        return default_value;
    }

    ESP_LOGI(TAG, "Loaded %s=%u", key, value);

    return value;
}



static esp_err_t save_blob_value(const char *key, const void *data, size_t size)
{
    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed for blob '%s': %s",
                 key,
                 esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, key, data, size);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved blob %s, size=%u",
                 key,
                 (unsigned)size);
    } else {
        ESP_LOGE(TAG, "Save blob failed for key '%s': %s",
                 key,
                 esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t load_blob_value(const char *key, void *data, size_t expected_size)
{
    if (data == NULL || expected_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No NVS namespace for blob '%s': %s",
                 key,
                 esp_err_to_name(ret));
        return ret;
    }

    size_t size = expected_size;

    ret = nvs_get_blob(handle, key, data, &size);

    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Blob '%s' not found", key);
        return ret;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Load blob failed for key '%s': %s",
                 key,
                 esp_err_to_name(ret));
        return ret;
    }

    if (size != expected_size) {
        ESP_LOGW(TAG,
                 "Blob '%s' size mismatch: got=%u expected=%u",
                 key,
                 (unsigned)size,
                 (unsigned)expected_size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Loaded blob %s, size=%u",
             key,
             (unsigned)size);

    return ESP_OK;
}






esp_err_t clock_settings_save_ethernet_alarms(const void *alarms, size_t size)
{
    return save_blob_value(NVS_KEY_ETH_ALARMS, alarms, size);
}

esp_err_t clock_settings_load_ethernet_alarms(void *alarms, size_t size)
{
    return load_blob_value(NVS_KEY_ETH_ALARMS, alarms, size);
}















void clock_settings_save_format(uint8_t format)
{
    save_u8_value(NVS_KEY_FORMAT, format);
}

uint8_t clock_settings_load_format(uint8_t default_format)
{
    return load_u8_value(NVS_KEY_FORMAT, default_format);
}

void clock_settings_save_mode(uint8_t mode)
{
    save_u8_value(NVS_KEY_MODE, mode);
}

uint8_t clock_settings_load_mode(uint8_t default_mode)
{
    return load_u8_value(NVS_KEY_MODE, default_mode);
}

void clock_settings_save_brightness(uint8_t brightness_level)
{
    if (brightness_level < 1) {
        brightness_level = 1;
    }

    if (brightness_level > 10) {
        brightness_level = 10;
    }

    save_u8_value(NVS_KEY_BRIGHTNESS, brightness_level);
}

uint8_t clock_settings_load_brightness(uint8_t default_brightness_level)
{
    uint8_t value = load_u8_value(NVS_KEY_BRIGHTNESS, default_brightness_level);

    if (value < 1 || value > 10) {
        value = default_brightness_level;
    }

    return value;
}