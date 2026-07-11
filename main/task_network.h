#ifndef TASK_NETWORK_H
#define TASK_NETWORK_H
#include "esp_event.h"
void network_init(void);
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void vMqttTask(void *pvParameters);
#endif