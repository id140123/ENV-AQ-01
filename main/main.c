#include "system_config.h"
#include "task_sensors.h"
#include "task_network.h"
#include "task_io.h"
#include "task_storage.h"

static const char *TAG = "ENV-AQ-01";

const float RO_CALIBRATED = 1125.78;
QueueHandle_t sd_queue;              
QueueHandle_t mqtt_queue;            
SemaphoreHandle_t data_mutex;
SemaphoreHandle_t sd_mutex;          
EventGroupHandle_t sys_events;       
SensorData_t current_sensor_data;
TaskHandle_t button_task_handle;     

SSD1306_t oled; 
bme680_t bme; 
uint32_t bme_duration;
adc_oneshot_unit_handle_t adc1_handle; 
FILE *sd_datalog = NULL; 
TimerHandle_t oled_timer;

esp_mqtt_client_handle_t mqtt_client = NULL;
const uint8_t apm_request_cmd[] = {0xFE, 0xA5, 0x00, 0x01, 0xA6};

uint32_t current_record_id = 0;
uint64_t last_sync_unix = 0;
uint32_t backlog_count = 0;

void app_main(void) {
    // 1. Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Đọc thời gian đồng bộ cuối cùng từ NVS (nếu có)
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_get_u64(my_handle, "last_sync", &last_sync_unix);
        nvs_close(my_handle);
    }

    ESP_LOGI(TAG, "==== Khoi dong he thong ENV-AQ-01 ====");

    // 2. Khởi tạo RTOS Primitives
    sys_events = xEventGroupCreate();               
    xEventGroupSetBits(sys_events, OLED_ON_BIT);
    data_mutex = xSemaphoreCreateMutex();           
    sd_mutex = xSemaphoreCreateMutex();             
    sd_queue = xQueueCreate(10, sizeof(SensorData_t)); 
    mqtt_queue = xQueueCreate(20, sizeof(SensorData_t));

    // 3. Khởi tạo các module phần cứng
    io_init();
    storage_init(); // Khởi tạo thẻ nhớ & load ID trước khi chạy cảm biến
    sensors_init();
    network_init();

    // Logic log boot reason
    esp_reset_reason_t reason = esp_reset_reason();
    char boot_msg[32];
    if (reason == ESP_RST_POWERON) {
        strcpy(boot_msg, "BOOT_POWERON_NORMAL");
    } else if (reason == ESP_RST_PANIC) {
        strcpy(boot_msg, "BOOT_EXCEPTION_PANIC"); // Code bị lỗi chia cho 0 hoặc truy cập vùng nhớ cấm
    } else if (reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT) {
        strcpy(boot_msg, "BOOT_WATCHDOG_RESET");  // Mạch bị treo, hệ điều hành tự reset
    } else if (reason == ESP_RST_BROWNOUT) {
        strcpy(boot_msg, "BOOT_BROWNOUT_DROP");   // Sụt áp nguồn
    } else {
        snprintf(boot_msg, sizeof(boot_msg), "BOOT_REASON_CODE_%d", reason);
    }
    log_event_to_sd("SYSTEM", boot_msg);

    // 4. Khởi chạy các Task
    xTaskCreate(vButtonTask, "BtnTask", 2048, NULL, 5, &button_task_handle);
    xTaskCreate(vSensorTask, "SensTask", 4096, NULL, 5, NULL);               
    xTaskCreate(vDisplayTask, "DispTask", 4096, NULL, 4, NULL);              
    xTaskCreate(vBuzzerTask, "BuzzTask", 2048, NULL, 3, NULL);
    xTaskCreate(vSDLoggerTask, "SDTask", 4096, NULL, 3, NULL);               
    xTaskCreate(vMqttTask, "MqttTask", 8192, NULL, 2, NULL);
    
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0); 
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
}