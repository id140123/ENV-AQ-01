#include <stdio.h>
#include "esp_log.h"
#include "bme680.h"
#include "driver/i2c.h"

#define BME_SDA_PIN 4 
#define BME_SCL_PIN 15 
#define BME_ADDR    0x77

static const char *TAG = "TEST_BME680";
bme680_t bme;

void app_main(void) {
    ESP_LOGI(TAG, "--- BAT DAU TEST BME680 ---");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK(i2cdev_init());
    memset(&bme, 0, sizeof(bme680_t));

    if (bme680_init_desc(&bme, BME_ADDR, I2C_NUM_1, BME_SDA_PIN, BME_SCL_PIN) != ESP_OK) {
        ESP_LOGE(TAG, "Loi cau hinh chan I2C BME680");
        return;
    }

    if (bme680_init_sensor(&bme) != ESP_OK) {
        ESP_LOGE(TAG, "KHONG TIM THAY BME680! Kiem tra lai day/han/mach/die tro pull-up.");
        return;
    }

    ESP_LOGI(TAG, "BME680 KET NOI THANH CONG! Dang doc data...");
    bme680_set_oversampling_rates(&bme, BME680_OSR_8X, BME680_OSR_2X, BME680_OSR_4X);
    bme680_set_filter_size(&bme, BME680_IIR_SIZE_3);
    bme680_set_heater_profile(&bme, 0, 320, 150);
    bme680_use_heater_profile(&bme, 0);

    uint32_t duration;
    bme680_get_measurement_duration(&bme, &duration);
    bme680_values_float_t bme_val;

    while (1) {
        if (bme680_force_measurement(&bme) == ESP_OK) {
            vTaskDelay(duration);
            if (bme680_get_results_float(&bme, &bme_val) == ESP_OK) {
                ESP_LOGI(TAG, "Nhiet do: %.2f C | Do am: %.2f %% | Ap suat: %.2f hPa", 
                         bme_val.temperature, bme_val.humidity, bme_val.pressure);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}