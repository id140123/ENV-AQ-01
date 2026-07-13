#include "system_config.h"
#include "task_io.h"
#include <sys/unistd.h>
#include <time.h> 

void oled_timeout_callback(TimerHandle_t xTimer) {
    xEventGroupClearBits(sys_events, OLED_ON_BIT);
}

void IRAM_ATTR button_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
    if(xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void io_init(void) {
    // 1. Khởi tạo GPIO cho Còi và Nút nhấn
    gpio_reset_pin(BUZZER_PIN); 
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT); 
    gpio_set_level(BUZZER_PIN, 0);
    
    gpio_reset_pin(BUTTON_PIN); 
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT); 
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    // 2. Khởi tạo Timer tắt màn hình (15 giây)
    oled_timer = xTimerCreate("OLED_Timer", pdMS_TO_TICKS(15000), pdFALSE, (void *)0, oled_timeout_callback);
    xTimerStart(oled_timer, 0);

    // 3. Khởi tạo và thiết lập màn hình Oled
    i2c_master_init(&oled, OLED_SDA_PIN, OLED_SCL_PIN, -1); 
    ssd1306_init(&oled, 128, 64); 
    ssd1306_clear_screen(&oled, false);

    // Boot Screen
    char oled_buf[64]; 
    snprintf(oled_buf, sizeof(oled_buf), "   ENV-AQ-01   "); 
    ssd1306_display_text(&oled, 2, oled_buf, strlen(oled_buf), false);
    
    snprintf(oled_buf, sizeof(oled_buf), "  Loading...   "); 
    ssd1306_display_text(&oled, 4, oled_buf, strlen(oled_buf), false);
    
    // Giữ màn hình khởi động trong 2 giây
    vTaskDelay(pdMS_TO_TICKS(2000)); 
}

void vButtonTask(void *pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(50));
        
        if (gpio_get_level(BUTTON_PIN) == 0) {
            xEventGroupSetBits(sys_events, OLED_ON_BIT); // Bật màn hình
            xTimerReset(oled_timer, 0);                  // Reset bộ đếm 15s
        }
    }
}

void vDisplayTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    SensorData_t local_data; 
    char buf[64]; 
    bool is_cleared = false;

    while(1) {
        esp_task_wdt_reset();
        EventBits_t uxBits = xEventGroupGetBits(sys_events);

        if (uxBits & OLED_ON_BIT) {
            // Lấy dữ liệu với Mutex Timeout 100ms
            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                local_data = current_sensor_data;
                xSemaphoreGive(data_mutex);
            }
            
            ssd1306_clear_screen(&oled, false);

            snprintf(buf, sizeof(buf), "Temp : %.1f C", local_data.temp);        
            ssd1306_display_text(&oled, 0, buf, strlen(buf), false);
            
            snprintf(buf, sizeof(buf), "Hum  : %.1f %%", local_data.hum);
            ssd1306_display_text(&oled, 1, buf, strlen(buf), false);
            
            snprintf(buf, sizeof(buf), "PM2.5: %d ug/m3", local_data.pm25_filtered); 
            ssd1306_display_text(&oled, 2, buf, strlen(buf), false);
            
            snprintf(buf, sizeof(buf), "eCO2 : %d ppm", local_data.eco2);        
            ssd1306_display_text(&oled, 3, buf, strlen(buf), false);

            // Hiển thị giờ thực tế hoặc offline
            if (local_data.time_valid == 1) {
                time_t t = (time_t)local_data.timestamp;
                struct tm ti;
                localtime_r(&t, &ti);
                // In ra định dạng Giờ:Phút:Giây
                snprintf(buf, sizeof(buf), "Time : %02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
            } else {
                snprintf(buf, sizeof(buf), "Time : OFFLINE  ");
            }
            ssd1306_display_text(&oled, 4, buf, strlen(buf), false);

            if (uxBits & MQTT_CONNECTED_BIT) {
                if (backlog_count > 0) {
                    snprintf(buf, sizeof(buf), "SYNCING...      ");
                    ssd1306_display_text(&oled, 5, buf, strlen(buf), false);
                    snprintf(buf, sizeof(buf), "BUF: %lu         ", (unsigned long)backlog_count);
                    ssd1306_display_text(&oled, 6, buf, strlen(buf), false);
                } else {
                    snprintf(buf, sizeof(buf), "Blynk: ONLINE   ");
                    ssd1306_display_text(&oled, 5, buf, strlen(buf), false);
                    snprintf(buf, sizeof(buf), "                ");
                    ssd1306_display_text(&oled, 6, buf, strlen(buf), false);
                }
            }
            else if (uxBits & WIFI_CONNECTED_BIT) {
                snprintf(buf, sizeof(buf), "Blynk: WAIT...  ");
                ssd1306_display_text(&oled, 5, buf, strlen(buf), false);
                // Giữ nguyên hiển thị backlog (nếu có) khi đang chờ MQTT
                if (backlog_count > 0) {
                    snprintf(buf, sizeof(buf), "BUF: %lu         ", (unsigned long)backlog_count);
                    ssd1306_display_text(&oled, 6, buf, strlen(buf), false);
                } else {
                    snprintf(buf, sizeof(buf), "                ");
                    ssd1306_display_text(&oled, 6, buf, strlen(buf), false);
                }
            }
            else { // Rớt WiFi hoàn toàn
                snprintf(buf, sizeof(buf), "WiFi: OFFLINE   ");
                ssd1306_display_text(&oled, 5, buf, strlen(buf), false);
                snprintf(buf, sizeof(buf), "BUF: %lu         ", (unsigned long)backlog_count);
                ssd1306_display_text(&oled, 6, buf, strlen(buf), false);
            }

            is_cleared = false;
        } else if (!is_cleared) {
            // Tắt màn hình để tiết kiệm năng lượng
            ssd1306_clear_screen(&oled, false);
            is_cleared = true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // Tốc độ làm tươi màn hình 2 Hz
    }
}

void vBuzzerTask(void *pvParameters) {
    bool is_alarm = false;

    while(1) {
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            is_alarm = current_sensor_data.alarm_triggered;
            xSemaphoreGive(data_mutex);
        }

        // Logic còi báo động: Kêu tít tít nếu có lỗi
        if (is_alarm) {
            gpio_set_level(BUZZER_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));     // Kêu 100ms
            gpio_set_level(BUZZER_PIN, 0); 
            vTaskDelay(pdMS_TO_TICKS(1900));    // Nghỉ 1900ms (tổng chu kỳ 2s)
        } else {
            gpio_set_level(BUZZER_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(500));     // Kiểm tra lại sau mỗi 0.5s
        }
    }
}