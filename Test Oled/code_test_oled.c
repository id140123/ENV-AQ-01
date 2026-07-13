#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "ssd1306.h"

#define OLED_SDA_PIN 14
#define OLED_SCL_PIN 13

static const char *TAG = "TEST_OLED";

SSD1306_t oled; 

void app_main(void) {
    ESP_LOGI(TAG, "--- BAT DAU TEST OLED DOC LAP ---");
    vTaskDelay(pdMS_TO_TICKS(500));

    // 1. Khởi tạo I2C và OLED
    i2c_master_init(&oled, OLED_SDA_PIN, OLED_SCL_PIN, -1);
    ssd1306_init(&oled, 128, 64); 
    ssd1306_clear_screen(&oled, false);

    // 2. In chữ ra màn hình
    char oled_buf[64];
    
    snprintf(oled_buf, sizeof(oled_buf), "   TEST OLED    "); 
    ssd1306_display_text(&oled, 2, oled_buf, strlen(oled_buf), false);
    
    snprintf(oled_buf, sizeof(oled_buf), "  HOAT DONG OK  "); 
    ssd1306_display_text(&oled, 4, oled_buf, strlen(oled_buf), false);

    ESP_LOGI(TAG, "=> Da ra lenh ve chu len man hinh OLED!");

    // Vòng lặp vô tận để giữ màn hình không bị tắt
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}