#include <stdio.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "CALIBRATION";

#define RL_VALUE      10.0  // Điện trở tải trên mạch (Con số 103)
#define RO_CLEAN_AIR  3.6   // Tỷ lệ Rs/Ro trong không khí sạch (Từ Datasheet)

adc_oneshot_unit_handle_t adc1_handle;

void vCalibrateRoTask(void *pvParameters) {
    ESP_LOGI(TAG, "=== BAT DAU CHE DO HIEU CHUAN MQ135 (CAM THANG) ===");
    ESP_LOGI(TAG, "Vui long de mach ngoai troi thoang gio va cho it nhat 10 phut...");
    
    float sum_Ro = 0;
    int sample_count = 0;

    while(1) {
        int adc_raw = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &adc_raw);
        
        // 1. Tính điện áp chân ESP32 không cầu phân áp (Max 3.3V ở ngưỡng 4095)
        float v_out = ((float)adc_raw / 4095.0) * 3.3;
        
        if (v_out <= 0.0) v_out = 0.001; // Chống lỗi chia cho 0
        
        // 2. Tính điện trở Rs thực tế (Mạch MQ135 được cấp nguồn 5V)
        float Rs = ((5.0 - v_out) / v_out) * RL_VALUE;
        
        // 3. Suy ra điện trở cơ sở Ro
        float Ro_current = Rs / RO_CLEAN_AIR;
        
        // 4. Cộng dồn để tính trung bình làm mượt số liệu
        sum_Ro += Ro_current;
        sample_count++;
        
        if (sample_count >= 10) {
            float Ro_average = sum_Ro / 10.0;
            
            ESP_LOGI(TAG, "-------------------------------------------------");
            ESP_LOGI(TAG, "ADC Raw       : %d", adc_raw);
            ESP_LOGI(TAG, "Dien ap V_out : %.2f V", v_out);
            ESP_LOGI(TAG, "Dien tro Rs   : %.2f kOhm", Rs);
            ESP_LOGI(TAG, "=> SO Ro CHUAN  : %.3f", Ro_average);
            ESP_LOGI(TAG, "-------------------------------------------------");
            
            sum_Ro = 0;
            sample_count = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Quét 1 giây 1 lần
    }
}

void app_main(void) {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));

    xTaskCreate(vCalibrateRoTask, "CalibrateTask", 4096, NULL, 5, NULL);
}