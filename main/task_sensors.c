#include "system_config.h"
#include "task_sensors.h"

static const char *TAG = "SENSORS";

// Cờ trạng thái bảo vệ hệ thống khỏi lỗi chia cho 0 của BME680
static bool is_bme680_ok = false; 

// Hàm tính toán AQI dựa trên giá trị PM2.5
int get_aqi_pm25(int pm25) {
    if (pm25 <= 12) return (50 * pm25) / 12;
    if (pm25 <= 35) return ((100 - 51) * (pm25 - 13) / (35 - 13)) + 51;
    if (pm25 <= 55) return ((150 - 101) * (pm25 - 36) / (55 - 36)) + 101;
    if (pm25 <= 150) return ((200 - 151) * (pm25 - 56) / (150 - 56)) + 151;
    if (pm25 <= 250) return ((300 - 201) * (pm25 - 151) / (250 - 151)) + 201;
    if (pm25 <= 350) return ((400 - 301) * (pm25 - 251) / (350 - 251)) + 301;
    if (pm25 <= 500) return ((500 - 401) * (pm25 - 351) / (500 - 351)) + 401;
    return 500;
}

// Hàm tạo giá trị VOC giả
int generate_dummy_voc() { 
    return 20 + ((esp_timer_get_time() / 1000000ULL) % 16);
}

// Hàm tính toán nồng độ CO2 từ cảm biến MQ135 với bù nhiệt độ và độ ẩm
int get_mq135_co2_compensated(int adc_raw, float current_temp, float current_hum) {
    if (adc_raw == 0) return 400;
    float v_out = ((float)adc_raw / 4095.0) * 3.3; if (v_out <= 0.0) v_out = 0.001;
    float Rs_raw = ((5.0 - v_out) / v_out) * RL_VALUE;
    float CF = 1.0 - 0.00267 * (current_temp - 20.0) - 0.00154 * (current_hum - 33.0);
    if (CF <= 0.1) CF = 0.1; 
    float ppm = CO2_A * pow(Rs_raw / CF / RO_CALIBRATED, CO2_B);
    return (ppm < 0.0) ? 0 : (int)ppm;
}

void sensors_init(void) {
    ESP_ERROR_CHECK(i2cdev_init()); 
    memset(&bme, 0, sizeof(bme680_t));
    
    if (bme680_init_desc(&bme, BME_ADDR, I2C_NUM_1, BME_SDA_PIN, BME_SCL_PIN) == ESP_OK) {
        
        bme.i2c_dev.cfg.master.clk_speed = 100000;
        
        if (bme680_init_sensor(&bme) == ESP_OK) {
            
            // Khởi tạo thành công, thiết lập thông số đo đạc
            bme680_set_oversampling_rates(&bme, BME680_OSR_8X, BME680_OSR_2X, BME680_OSR_4X);
            bme680_set_filter_size(&bme, BME680_IIR_SIZE_3); 
            bme680_set_heater_profile(&bme, 0, 320, 150); 
            bme680_use_heater_profile(&bme, 0); 
            bme680_get_measurement_duration(&bme, &bme_duration);
            
            is_bme680_ok = true; 
            ESP_LOGI(TAG, "Khoi tao BME680 THANH CONG!");
        } else {
            ESP_LOGE(TAG, "Loi init_sensor BME680! Mach van chay tiep.");
        }
    } else {
        ESP_LOGE(TAG, "Khong tim thay BME680 hoac day bi nhieu! Mach van chay tiep.");
    }

    uart_config_t uart_config = { 
        .baud_rate = 1200, 
        .data_bits = UART_DATA_8_BITS, 
        .parity = UART_PARITY_DISABLE, 
        .stop_bits = UART_STOP_BITS_1, 
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, 
        .source_clk = UART_SCLK_DEFAULT 
    };
    uart_param_config(UART_NUM, &uart_config); 
    uart_set_pin(UART_NUM, APM_TX_PIN, APM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); 
    uart_driver_install(UART_NUM, 256, 0, 0, NULL, 0);
    
    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1, }; 
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    adc_oneshot_chan_cfg_t config = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12, }; 
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));
}

void vSensorTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    bme680_values_float_t bme_val;
    int pm1_raw = 0; float pm1_ema = 0;
    int pm25_raw = 0; float pm25_ema = 0;
    int pm10_raw = 0; float pm10_ema = 0;
    bool first_pm = true;
    float eco2_ema = 0; bool first_eco2 = true;
    
    // Khai báo theo dõi sự kiện
    bool was_time_synced = false;
    
    while(1) {
        esp_task_wdt_reset(); 
        SensorData_t new_data = {0};
        
        // Bắt đầu thuật toán Hybrid Clock
        time_t now; time(&now);
        if (now > 1704067200) { // Nếu giờ chuẩn > năm 2024
            new_data.timestamp = now;
            new_data.time_valid = 1;
            
            // Chỉ ghi NVS đúng 1 lần khi kết nối thành công để bảo vệ bộ nhớ
            if (!was_time_synced) {
                last_sync_unix = now;
                nvs_handle_t my_handle;
                if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                    nvs_set_u64(my_handle, "last_sync", last_sync_unix);
                    nvs_commit(my_handle);
                    nvs_close(my_handle);
                }
                was_time_synced = true;
            }
        } else {
            // Tính toán giờ ước lượng khi mất mạng (offline)
            new_data.timestamp = last_sync_unix + (esp_timer_get_time() / 1000000ULL);
            new_data.time_valid = 0;
            was_time_synced = false;
        }

        current_record_id++;
        new_data.record_id = current_record_id;
        // Kết thúc thuật toán Hybrid Clock

        new_data.voc_dummy = generate_dummy_voc();
        
        // Chỉ số mặc định khi BME680 chưa đo được
        new_data.temp = -99.9;
        new_data.hum = -1.0; 
        new_data.pres = -1.0; 

        // Nếu BME680 hoạt động bình thường, tiến hành đo đạc
        if (is_bme680_ok) {
            if (bme680_force_measurement(&bme) == ESP_OK) {
                vTaskDelay(bme_duration);
                if (bme680_get_results_float(&bme, &bme_val) == ESP_OK) {
                    new_data.temp = bme_val.temperature - 3.5;
                    new_data.hum = bme_val.humidity; 
                    new_data.pres = bme_val.pressure;
                }
            }
        }

        int mq135_raw = 0;
        int eco2_instant = 0;
        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &mq135_raw) == ESP_OK) {
            eco2_instant = get_mq135_co2_compensated(mq135_raw, new_data.temp, new_data.hum);
            if(first_eco2) { eco2_ema = eco2_instant; first_eco2 = false; } 
            else { eco2_ema = (0.2 * eco2_instant) + (0.8 * eco2_ema);
            }
            new_data.eco2 = (int)eco2_ema;
        }

        uart_flush(UART_NUM);
        uart_write_bytes(UART_NUM, (const char*)apm_request_cmd, sizeof(apm_request_cmd));
        vTaskDelay(pdMS_TO_TICKS(150)); 
        uint8_t rx_data[16];
        if (uart_read_bytes(UART_NUM, rx_data, 11, pdMS_TO_TICKS(100)) == 11 && rx_data[0] == 0xFE && rx_data[1] == 0xA5) {
            uint8_t checksum = 0;
            for(int i = 1; i <= 9; i++) checksum += rx_data[i];
            if (checksum == rx_data[10]) {
                pm1_raw  = (rx_data[4] << 8) | rx_data[5];
                pm25_raw = (rx_data[6] << 8) | rx_data[7];
                pm10_raw = (rx_data[8] << 8) | rx_data[9];
                if(first_pm) { 
                    pm1_ema = pm1_raw;
                    pm25_ema = pm25_raw; 
                    pm10_ema = pm10_raw;
                    first_pm = false; 
                } else { 
                    pm1_ema = (0.2 * pm1_raw) + (0.8 * pm1_ema);
                    pm25_ema = (0.2 * pm25_raw) + (0.8 * pm25_ema);
                    pm10_ema = (0.2 * pm10_raw) + (0.8 * pm10_ema);
                }
            }
        }
        
        new_data.pm1_filtered = (int)pm1_ema;
        new_data.pm25_filtered = (int)pm25_ema;
        new_data.pm10_filtered = (int)pm10_ema;
        new_data.aqi = get_aqi_pm25(new_data.pm25_filtered);

        static bool pm25_alarm_state = false;
        static bool eco2_alarm_state = false;
        char alarm_detail[40];

        if (new_data.pm25_filtered > 50 && !pm25_alarm_state) {
            pm25_alarm_state = true;
            snprintf(alarm_detail, sizeof(alarm_detail), "PM2.5_HIGH: %d ug/m3", new_data.pm25_filtered);
            log_event_to_sd("ALARM", alarm_detail);
        } else if (new_data.pm25_filtered < 45 && pm25_alarm_state) {
            pm25_alarm_state = false;
        }

        if (new_data.eco2 > 1000 && !eco2_alarm_state) {
            eco2_alarm_state = true;
            snprintf(alarm_detail, sizeof(alarm_detail), "CO2_HIGH: %d ppm", new_data.eco2);
            log_event_to_sd("ALARM", alarm_detail);
        } else if (new_data.eco2 < 900 && eco2_alarm_state) {
            eco2_alarm_state = false;
        }

        new_data.alarm_triggered = (pm25_alarm_state || eco2_alarm_state);

        char time_str[32];
        if (new_data.time_valid == 1) {
            time_t t = (time_t)new_data.timestamp;
            struct tm ti;
            localtime_r(&t, &ti);
            snprintf(time_str, sizeof(time_str), "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
        } else {
            snprintf(time_str, sizeof(time_str), "[OFFLINE]");
        }

        ESP_LOGI(TAG, "%s ID:%u | T:%.1f C | H:%.1f %% | P:%.1f hPa | VOC:%d | PM1:%d | PM2.5:%d | PM10:%d | CO2:%d | AQI:%d", 
                 time_str, new_data.record_id, new_data.temp, new_data.hum, new_data.pres, new_data.voc_dummy, 
                 new_data.pm1_filtered, new_data.pm25_filtered, new_data.pm10_filtered, new_data.eco2, new_data.aqi);
                 
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            current_sensor_data = new_data;
            xSemaphoreGive(data_mutex);     
        }
        
        xQueueSend(sd_queue, &new_data, pdMS_TO_TICKS(200));
        
        if (xQueueSend(mqtt_queue, &new_data, pdMS_TO_TICKS(200)) != pdPASS) {
            
            static SensorData_t fallback_buffer[15]; 
            static uint8_t fallback_idx = 0;

            // Nhét dữ liệu vào đệm RAM
            fallback_buffer[fallback_idx] = new_data;
            fallback_idx++;
            
            // Đúng 15 dòng (30 giây) mới mở thẻ nhớ lưu 1 lần
            if (fallback_idx >= 15) {
                if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    FILE *bf = fopen("/sdcard/backlog.csv", "a");
                    if (bf != NULL) {
                        for (int i = 0; i < 15; i++) {
                            fprintf(bf, "%lu,%llu,%d,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%d\n",
                                    (unsigned long)fallback_buffer[i].record_id, fallback_buffer[i].timestamp, fallback_buffer[i].time_valid,
                                    fallback_buffer[i].temp, fallback_buffer[i].hum, fallback_buffer[i].pres, fallback_buffer[i].voc_dummy,
                                    fallback_buffer[i].pm1_filtered, fallback_buffer[i].pm25_filtered, fallback_buffer[i].pm10_filtered, fallback_buffer[i].eco2, fallback_buffer[i].aqi);
                        }
                        fclose(bf);
                        backlog_count += 15; // Cập nhật đồng bộ biến đếm hiển thị
                    }
                    xSemaphoreGive(sd_mutex);
                }
                fallback_idx = 0; // Đổ rác xong, reset đệm
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}