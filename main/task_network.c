#include "system_config.h"
#include "task_network.h"
#include <time.h>

static const char *TAG = "NETWORK";

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        log_event_to_sd("NETWORK", "WIFI_LOST");
        xEventGroupClearBits(sys_events, WIFI_CONNECTED_BIT); 
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(sys_events, WIFI_CONNECTED_BIT);
    }
}

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(sys_events, MQTT_CONNECTED_BIT);
        ESP_LOGI(TAG, "Blynk MQTT ket noi thanh cong!");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(sys_events, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "Mat ket noi toi server Blynk MQTT!");
    }
}

void network_init(void) {
    esp_netif_init(); 
    esp_event_loop_create_default(); 
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); 
    esp_wifi_init(&cfg);
    
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = { 
        .sta = { 
            .ssid = WIFI_SSID, 
            .password = WIFI_PASS 
        }, 
    };
    esp_wifi_set_mode(WIFI_MODE_STA); 
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config); 
    esp_wifi_start();

    // Khởi tạo SNTP (Đồng bộ thời gian quốc tế)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "UTC-7", 1);
    tzset();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://sgp1.blynk.cloud:1883", 
        .credentials.username = "device",
        .credentials.authentication.password = BLYNK_TOKEN,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void vMqttTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    SensorData_t mqtt_data;
    FILE *backlog_fp = NULL;
    uint8_t backlog_flush_counter = 0;
    uint8_t throttle = 0;
    char payload[300];
    
    while(1) {
        esp_task_wdt_reset();
        EventBits_t uxBits = xEventGroupGetBits(sys_events);
        bool is_online = ((uxBits & WIFI_CONNECTED_BIT) && (uxBits & MQTT_CONNECTED_BIT));
        
        // Logic xử lý đồng bộ dữ liệu backlog khi có kết nối mạng
        if (is_online) {
            
            // Phục hồi temp.csv nếu mất điện giữa chừng
            if (access("/sdcard/temp.csv", F_OK) == 0) {
                if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                     rename("/sdcard/temp.csv", "/sdcard/backlog_recovered.csv");
                    rename("/sdcard/backlog_recovered.csv", "/sdcard/backlog.csv");
                    xSemaphoreGive(sd_mutex);
                    ESP_LOGI(TAG, "=> Phuc hoi du lieu bi khet do mat dien truoc do!");
                }
            }

            if (access("/sdcard/backlog.csv", F_OK) == 0) { 
                
                // ATOMIC LOCK 1: Đổi tên file cực nhanh rồi nhả khóa
                if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    if (backlog_fp != NULL) { 
                        fclose(backlog_fp);
                        backlog_fp = NULL; 
                        backlog_flush_counter = 0;
                    }
                    rename("/sdcard/backlog.csv", "/sdcard/temp.csv");
                    xSemaphoreGive(sd_mutex);
                }

                // Mở file (Cần lấy khóa an toàn)
                FILE *temp_file = NULL;
                if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    temp_file = fopen("/sdcard/temp.csv", "r");
                    xSemaphoreGive(sd_mutex);
                }
                    
                if (temp_file != NULL) {
                    ESP_LOGI(TAG, "=> Phat hien Backlog, dang day bu len Blynk...");
                    log_event_to_sd("SYNC", "BACKLOG_SYNC_START");
                    
                    char line[128]; 
                    unsigned long rid; uint64_t ts; uint8_t tv; float t, h, p;
                    int voc, pm1, pm25, pm10, co2, aqi;
                    bool has_data = true;
                    
                    while (has_data) {
                        has_data = false;
                        // ATOMIC LOCK 2: Lấy khóa chỉ để đọc 1 dòng duy nhất
                        if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                            if (fgets(line, sizeof(line), temp_file)) {
                                has_data = true;
                            }
                            xSemaphoreGive(sd_mutex);
                        }

                        if (has_data) {
                            if (sscanf(line, "%lu,%llu,%hhu,%f,%f,%f,%d,%d,%d,%d,%d,%d", 
                                &rid, &ts, &tv, &t, &h, &p, &voc, &pm1, &pm25, &pm10, &co2, &aqi) == 12) {
                                
                                snprintf(payload, sizeof(payload), 
                                     "{\"Nhiet do\":%.1f, \"Do am\":%.1f, \"Ap suat\":%.1f, \"Bui PM25\":%d, \"Bui PM10\":%d, \"Chi so CO2\":%d, \"Khi VOC\":%d, \"Chi so AQI tong hop\":%d, \"timestamp\":%llu, \"time_valid\":%d}", 
                                    t, h, p, pm25, pm10, co2, voc, aqi, ts * 1000, tv);
                                esp_mqtt_client_publish(mqtt_client, "batch_ds", payload, strlen(payload), 0, 0);

                                char time_str[32];
                                if (tv == 1) {
                                    time_t time_val = (time_t)ts;
                                    struct tm ti;
                                    localtime_r(&time_val, &ti);
                                    snprintf(time_str, sizeof(time_str), "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
                                } else {
                                    snprintf(time_str, sizeof(time_str), "[OFFLINE]");
                                }
                                ESP_LOGI(TAG, "---> [SYNCING] Dang day bu goi tin %s ID:%lu | T:%.1f C | H:%.1f %% | P:%.1f hPa | VOC:%d | PM1:%d | PM2.5:%d | PM10:%d | CO2:%d | AQI:%d", 
                                     time_str, rid, t, h, p, voc, pm1, pm25, pm10, co2, aqi);
                                // Trừ dồn thay vì set về 0
                                if (backlog_count > 0) backlog_count--;
                            }
                            esp_task_wdt_reset();
                            // Nghỉ 1 giây để Blynk nuốt data, Sensor ghi thẻ SD
                            vTaskDelay(pdMS_TO_TICKS(1000));
                        }
                    }
                    
                    // ATOMIC LOCK 3: Lấy khóa để đóng và xóa file 
                    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                        fclose(temp_file);
                        remove("/sdcard/temp.csv"); 
                        xSemaphoreGive(sd_mutex);
                    }

                    ESP_LOGI(TAG, "=> Da day thanh cong Backlog!");
                    log_event_to_sd("SYNC", "BACKLOG_SYNC_DONE");
                }
            }
        }

        // Logic xử lý gửi dữ liệu cảm biến hiện tại lên Blynk MQTT hoặc lưu vào SD khi offline
        if (xQueueReceive(mqtt_queue, &mqtt_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            // Bóc tách thời gian chung để in Terminal
            char time_str[32];
            if (mqtt_data.time_valid == 1) {
                time_t t_val = (time_t)mqtt_data.timestamp;
                struct tm ti;
                localtime_r(&t_val, &ti);
                snprintf(time_str, sizeof(time_str), "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
            } else {
                snprintf(time_str, sizeof(time_str), "[OFFLINE]");
            }

            if (is_online) {
                throttle++;
                
                if (throttle >= 60 || mqtt_data.alarm_triggered) {
                    snprintf(payload, sizeof(payload), 
                        "{\"Nhiet do\":%.1f, \"Do am\":%.1f, \"Ap suat\":%.1f, \"Bui PM25\":%d, \"Bui PM10\":%d, \"Chi so CO2\":%d, \"Khi VOC\":%d, \"Chi so AQI tong hop\":%d, \"timestamp\":%llu, \"time_valid\":%d}", 
                        mqtt_data.temp, mqtt_data.hum, mqtt_data.pres, mqtt_data.pm25_filtered, 
                        mqtt_data.pm10_filtered, mqtt_data.eco2, mqtt_data.voc_dummy, mqtt_data.aqi, mqtt_data.timestamp * 1000, mqtt_data.time_valid);
                    esp_mqtt_client_publish(mqtt_client, "batch_ds", payload, strlen(payload), 0, 0);
                    
                    // In log ra Terminal để kiểm tra dữ liệu đã gửi
                    ESP_LOGI(TAG, "---> [BLYNK PUSH] %s ID:%lu | T:%.1f C | H:%.1f %% | P:%.1f hPa | VOC:%d | PM1:%d | PM2.5:%d | PM10:%d | CO2:%d | AQI:%d", 
                             time_str, (unsigned long)mqtt_data.record_id, mqtt_data.temp, mqtt_data.hum, mqtt_data.pres, mqtt_data.voc_dummy, 
                             mqtt_data.pm1_filtered, mqtt_data.pm25_filtered, mqtt_data.pm10_filtered, mqtt_data.eco2, mqtt_data.aqi);
                             
                    throttle = 0; // Reset đếm sau khi đã gửi lên MQTT
                }
                
            } else {
                if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    if (backlog_fp == NULL) backlog_fp = fopen("/sdcard/backlog.csv", "a");
                    if (backlog_fp != NULL) {
                        // Gắn kết quả của lệnh ghi vào biến
                        int bytes_written = fprintf(backlog_fp, "%lu,%llu,%d,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%d\n", 
                                (unsigned long)mqtt_data.record_id, mqtt_data.timestamp, 
                                mqtt_data.time_valid,
                                mqtt_data.temp, mqtt_data.hum, mqtt_data.pres, mqtt_data.voc_dummy,
                                mqtt_data.pm1_filtered, 
                                mqtt_data.pm25_filtered, mqtt_data.pm10_filtered, mqtt_data.eco2, mqtt_data.aqi);
                        if (bytes_written > 0) {
                            backlog_count++;           // Tăng hiển thị màn hình
                            backlog_flush_counter++;   // Tăng đếm nhịp lưu
                            
                            // In log ra Terminal để kiểm tra dữ liệu đã lưu vào SD
                            ESP_LOGI(TAG, "---> [SD SAVED] %s ID:%lu | T:%.1f C | H:%.1f %% | P:%.1f hPa | VOC:%d | PM1:%d | PM2.5:%d | PM10:%d | CO2:%d | AQI:%d | So luong dang ket: %lu", 
                                     time_str, (unsigned long)mqtt_data.record_id, mqtt_data.temp, mqtt_data.hum, mqtt_data.pres, mqtt_data.voc_dummy, 
                                     mqtt_data.pm1_filtered, mqtt_data.pm25_filtered, mqtt_data.pm10_filtered, mqtt_data.eco2, mqtt_data.aqi, (unsigned long)backlog_count);
                            
                            if (backlog_flush_counter >= 15) { 
                                fflush(backlog_fp);
                                fsync(fileno(backlog_fp)); // fsync giúp lưu chắc chắn vào flash
                                backlog_flush_counter = 0;
                            }
                        } else {
                            ESP_LOGE(TAG, "Loi! Khong the ghi du lieu xuong the SD.");
                        }
                    }
                    xSemaphoreGive(sd_mutex);
                }
            }
        }
    }
}