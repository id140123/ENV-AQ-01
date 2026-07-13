#include <stdio.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#define MQ135_PIN      34

static const char *TAG = "TEST_MQ135";

void app_main(void) {
    ESP_LOGI(TAG, "--- BAT DAU TEST MQ135 ---");
    vTaskDelay(pdMS_TO_TICKS(1000));

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = { 
        .unit_id = ADC_UNIT_1, 
    }; 
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    
    adc_oneshot_chan_cfg_t config = { 
        .bitwidth = ADC_BITWIDTH_DEFAULT, 
        .atten = ADC_ATTEN_DB_12, 
    }; 
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));

    int mq135_raw = 0;

    while(1) {
        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &mq135_raw) == ESP_OK) {
            ESP_LOGI(TAG, "Gia tri tho cua MQ135 (Raw ADC): %d", mq135_raw);
        } else {
            ESP_LOGE(TAG, "Loi doc ADC tu MQ135!");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}