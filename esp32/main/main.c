#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    printf("Tarea 1 IoT - ESP32-C3 test\n");
    printf("Cores: %d\n", chip_info.cores);
    printf("Flash: %lu MB\n", flash_size / (1024 * 1024));

    int counter = 0;
    while (true) {
        printf("ESP32-C3 conectado y ejecutando: %d\n", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
