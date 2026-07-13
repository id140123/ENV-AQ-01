#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define PIN_NUM_MISO   19
#define PIN_NUM_MOSI   23
#define PIN_NUM_CLK    18
#define PIN_NUM_CS     5

static const char *TAG = "TEST_SD";

void app_main(void) {
    ESP_LOGI(TAG, "--- BAT DAU TEST THE SD ---");
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); 
    host.slot = SPI3_HOST;
    
    host.max_freq_khz = 4000; 
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI, 
        .miso_io_num = PIN_NUM_MISO, 
        .sclk_io_num = PIN_NUM_CLK, 
        .quadwp_io_num = -1, 
        .quadhd_io_num = -1, 
        .max_transfer_sz = 4000
    };
    spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO); 
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT(); 
    slot_config.gpio_cs = PIN_NUM_CS; 
    slot_config.host_id = host.slot;

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LOI THE SD! Kiem tra lai day/chan hoac the nho chua cam chat.");
        return;
    }
    ESP_LOGI(TAG, "MOUNT THE SD THANH CONG! Dang thu ghi file...");

    FILE *f = fopen("/sdcard/test.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Khong the tao file test.txt");
    } else {
        fprintf(f, "The SD hoat dong tot o toc do 4MHz!\n");
        fclose(f);
        ESP_LOGI(TAG, "=> Da ghi file /sdcard/test.txt thanh cong!");
    }

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}