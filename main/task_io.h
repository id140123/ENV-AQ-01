#ifndef TASK_IO_H
#define TASK_IO_H
#include "freertos/timers.h"
void io_init(void);
void oled_timeout_callback(TimerHandle_t xTimer);
void button_isr_handler(void* arg);
void vButtonTask(void *pvParameters);
void vDisplayTask(void *pvParameters);
void vBuzzerTask(void *pvParameters);
#endif