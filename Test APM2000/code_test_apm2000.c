#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"

#define UART_NUM       UART_NUM_2
#define APM_TX_PIN     27
#define APM_RX_PIN     26

static const char *TAG = "APM2000";

static const uint8_t apm_request_cmd[5] =
{
    0xFE, 0xA5, 0x00, 0x01, 0xA6
};

void app_main(void)
{
    ESP_LOGI(TAG, "===== TEST APM2000 =====");

    uart_config_t uart_config = {
        .baud_rate = 1200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));

    ESP_ERROR_CHECK(
        uart_set_pin(UART_NUM,
                     APM_TX_PIN,
                     APM_RX_PIN,
                     UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(
        uart_driver_install(UART_NUM,
                            256,
                            0,
                            0,
                            NULL,
                            0));

    uint8_t rx_data[16];

    while (1)
    {
        uart_flush(UART_NUM);

        uart_write_bytes(UART_NUM,
                         (const char *)apm_request_cmd,
                         sizeof(apm_request_cmd));

        vTaskDelay(pdMS_TO_TICKS(150));

        int len = uart_read_bytes(UART_NUM,
                                  rx_data,
                                  11,
                                  pdMS_TO_TICKS(100));

        ESP_LOGI(TAG, "Nhan %d byte", len);

        if (len > 0)
        {
            printf("RAW : ");
            for (int i = 0; i < len; i++)
                printf("%02X ", rx_data[i]);
            printf("\n");
        }

        if (len == 11 &&
            rx_data[0] == 0xFE &&
            rx_data[1] == 0xA5)
        {
            uint8_t checksum = 0;

            for (int i = 1; i <= 9; i++)
                checksum += rx_data[i];

            if (checksum == rx_data[10])
            {
                int pm1  = (rx_data[4] << 8) | rx_data[5];
                int pm25 = (rx_data[6] << 8) | rx_data[7];
                int pm10 = (rx_data[8] << 8) | rx_data[9];

                ESP_LOGI(TAG,
                         "PM1.0=%d  PM2.5=%d  PM10=%d",
                         pm1,
                         pm25,
                         pm10);
            }
            else
            {
                ESP_LOGE(TAG,
                         "Checksum sai (%02X != %02X)",
                         checksum,
                         rx_data[10]);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Khong nhan du du lieu hop le");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}