#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdbool.h>

#include "bsp_board.h"
#include "lv_port.h"

#include "indicator/config.h"
#include "indicator/indicator_wifi.h"
#include "indicator/view_data.h"
#include "hamview_backend.h"
#include "hamview_settings.h"
#include "hamview_ui.h"
#include "hamview_weather.h"
#include "wifi_ui.h"

static const char *TAG = "hamview_main";

ESP_EVENT_DEFINE_BASE(VIEW_EVENT_BASE);
esp_event_loop_handle_t view_event_handle = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "SenseCAP HamView booting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(bsp_board_init());
    lv_port_init();

    esp_event_loop_args_t view_event_task_args = {
        .queue_size = 10,
        .task_name = "view_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 10240,
        .task_core_id = tskNO_AFFINITY,
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&view_event_task_args, &view_event_handle));

    ESP_ERROR_CHECK(hamview_settings_init());

    bool wifi_ready = false;
    if (indicator_wifi_init() != 0) {
        ESP_LOGE(TAG, "indicator_wifi_init failed");
    } else {
        wifi_ready = true;
        ESP_ERROR_CHECK(hamview_backend_init());
        ESP_ERROR_CHECK(hamview_weather_init());
    }

    hamview_wifi_ui_init();
    hamview_ui_init();

    if (wifi_ready) {
        indicator_wifi_notify_status();
    }

    if (wifi_ready) {
        ESP_ERROR_CHECK(esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, portMAX_DELAY));
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
