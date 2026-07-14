#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdio.h>
#include <string.h>
#include <math.h> 
#include <unistd.h>
#include <time.h>             
#include "esp_sntp.h"       
#include "nvs_flash.h"         
#include "nvs.h"            

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h" 
#include "esp_task_wdt.h"          
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "ssd1306.h"  
#include "bme680.h"   
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include <i2cdev.h>    

#define WIFI_SSID      "Penhouse_2.4G" 
#define WIFI_PASS      "3.141592654" 
#define BLYNK_TOKEN    "yJEFzHSpHivECrE1WVyMObiM_zEi5FtN"

#define OLED_SDA_PIN 14
#define OLED_SCL_PIN 13
#define BME_SDA_PIN 4 
#define BME_SCL_PIN 15 
#define BME_ADDR    0x77
#define UART_NUM       UART_NUM_2
#define APM_TX_PIN     27
#define APM_RX_PIN     26
#define PIN_NUM_MISO   19
#define PIN_NUM_MOSI   23
#define PIN_NUM_CLK    18
#define PIN_NUM_CS     5
#define MQ135_PIN      34 
#define BUTTON_PIN     33
#define BUZZER_PIN     16

#define RL_VALUE       10.0   
#define CO2_A          110.47 
#define CO2_B          -2.862 

#define OLED_ON_BIT          BIT0
#define WIFI_CONNECTED_BIT   BIT1      
#define MQTT_CONNECTED_BIT   BIT2

typedef struct {
    uint32_t record_id;      // Số thứ tự chống trùng trong nhật ký
    uint64_t timestamp;      // Thời gian UNIX (giây)
    uint8_t  time_valid;     // 1 = Online, 0 = Offline
    
    float temp; float hum; float pres;
    int voc_dummy;
    int pm1_filtered; int pm25_filtered; int pm10_filtered; 
    int eco2; int aqi;
    bool alarm_triggered;
} SensorData_t;

extern const float RO_CALIBRATED;
extern QueueHandle_t sd_queue;              
extern QueueHandle_t mqtt_queue;            
extern SemaphoreHandle_t data_mutex;
extern SemaphoreHandle_t sd_mutex;          
extern EventGroupHandle_t sys_events;       
extern SensorData_t current_sensor_data;
extern TaskHandle_t button_task_handle;     

extern SSD1306_t oled; 
extern bme680_t bme; 
extern uint32_t bme_duration;
extern adc_oneshot_unit_handle_t adc1_handle; 
extern FILE *sd_datalog; 
extern TimerHandle_t oled_timer;
extern esp_mqtt_client_handle_t mqtt_client;
extern const uint8_t apm_request_cmd[5];

extern uint32_t current_record_id;
extern uint64_t last_sync_unix;
extern uint32_t backlog_count;

// Hàm ghi sự kiện vào thẻ SD
extern void log_event_to_sd(const char* event_type, const char* detail);

#endif