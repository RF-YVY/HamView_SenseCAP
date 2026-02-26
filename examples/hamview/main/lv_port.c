#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "lv_port.h"
#include "bsp_board.h"
#include "bsp_lcd.h"
#include "indev/indev.h"
#include "sdkconfig.h"
#include "hamview_screen.h"

#define LV_PORT_BUFFER_HEIGHT           (brd->LCD_HEIGHT)
#define LV_PORT_BUFFER_MALLOC           (MALLOC_CAP_SPIRAM)
#define LV_PORT_TASK_DELAY_MS           (5)

static const char *TAG = "lvgl_port";
static lv_disp_drv_t disp_drv;
static lv_indev_t *indev_touchpad = NULL;
static lv_indev_t *indev_button = NULL;
static SemaphoreHandle_t lvgl_mutex = NULL;
static TaskHandle_t lvgl_task_handle;

#ifndef CONFIG_LCD_TASK_PRIORITY
#define CONFIG_LCD_TASK_PRIORITY    5
#endif

static void lv_port_disp_init(void);
static void lv_port_indev_init(void);
static bool lv_port_flush_ready(void);
static bool lv_port_flush_is_last(void);
static IRAM_ATTR void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static esp_err_t lv_port_tick_init(void);
static void lvgl_task(void *args);
static void lv_port_direct_mode_copy(void);
static void button_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

void lv_port_init(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    lv_port_tick_init();

    lvgl_mutex = xSemaphoreCreateMutex();
    xTaskCreate(lvgl_task, "lvgl_task", 4096 * 4, NULL, CONFIG_LCD_TASK_PRIORITY, &lvgl_task_handle);
}

void lv_port_sem_take(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    if (lvgl_task_handle != task) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    }
}

void lv_port_sem_give(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    if (lvgl_task_handle != task) {
        xSemaphoreGive(lvgl_mutex);
    }
}

bool lv_port_is_in_lvgl_task(void)
{
    return xTaskGetCurrentTaskHandle() == lvgl_task_handle;
}

static bool lv_port_flush_ready(void)
{
    lv_disp_flush_ready(&disp_drv);
    return false;
}

static bool lv_port_flush_is_last(void)
{
    return lv_disp_flush_is_last(&disp_drv);
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)disp_drv;
    bsp_lcd_flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint8_t *)color_p);
}

static void button_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    static uint32_t last_key = 0;
    indev_data_t indev_data;
    if (ESP_OK != indev_get_major_value(&indev_data)) {
        ESP_LOGE(TAG, "Failed read input device value");
        return;
    }

    if (indev_data.btn_val & 0x02) {
        last_key = LV_KEY_ENTER;
        data->state = LV_INDEV_STATE_PRESSED;
    } else if (indev_data.btn_val & 0x04) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = LV_KEY_PREV;
    } else if (indev_data.btn_val & 0x01) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = LV_KEY_NEXT;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->key = last_key;
    hamview_screen_handle_button(data->key, data->state);
}

static IRAM_ATTR void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    static uint16_t x = 0;
    static uint16_t y = 0;
    indev_data_t indev_data;
    if (ESP_OK != indev_get_major_value(&indev_data)) {
        return;
    }

    if (indev_data.pressed) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = CONFIG_LCD_EVB_SCREEN_WIDTH - indev_data.x;
        data->point.y = CONFIG_LCD_EVB_SCREEN_HEIGHT - indev_data.y;
        x = data->point.x;
        y = data->point.y;
        hamview_screen_record_activity();
    } else {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = x;
        data->point.y = y;
    }
}

static void lv_port_disp_init(void)
{
    static lv_disp_draw_buf_t disp_buf;
    const board_res_desc_t *brd = bsp_board_get_description();

    void *buf1 = NULL;
    void *buf2 = NULL;
    int buffer_size;
#if CONFIG_LCD_AVOID_TEAR
    buffer_size = brd->LCD_WIDTH * brd->LCD_HEIGHT;
    bsp_lcd_get_frame_buffer(&buf1, &buf2);
#else
    buffer_size = brd->LCD_WIDTH * LV_PORT_BUFFER_HEIGHT;
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LV_PORT_BUFFER_MALLOC);
    assert(buf1);
#endif
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buffer_size);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = brd->LCD_WIDTH;
    disp_drv.ver_res = brd->LCD_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &disp_buf;
#if CONFIG_LCD_LVGL_FULL_REFRESH
    disp_drv.full_refresh = 1;
#elif CONFIG_LCD_LVGL_DIRECT_MODE
    disp_drv.direct_mode = 1;
#endif

    bsp_lcd_set_cb(lv_port_flush_ready, NULL);

#if CONFIG_LCD_LVGL_DIRECT_MODE
    bsp_lcd_flush_is_last_register(lv_port_flush_is_last);
    bsp_lcd_direct_mode_register(lv_port_direct_mode_copy);
#endif

    lv_disp_drv_register(&disp_drv);
}

static void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv_tp;
    static lv_indev_drv_t indev_drv_btn;

    const board_res_desc_t *brd = bsp_board_get_description();
    if (brd->BSP_INDEV_IS_TP) {
        ESP_LOGI(TAG, "Add TP input device to LVGL");
        lv_indev_drv_init(&indev_drv_tp);
        indev_drv_tp.type = LV_INDEV_TYPE_POINTER;
        indev_drv_tp.read_cb = touchpad_read;
        indev_touchpad = lv_indev_drv_register(&indev_drv_tp);
        (void)indev_touchpad;
    } else {
        ESP_LOGI(TAG, "Add KEYPAD input device to LVGL");
        lv_indev_drv_init(&indev_drv_btn);
        indev_drv_btn.type = LV_INDEV_TYPE_KEYPAD;
        indev_drv_btn.read_cb = button_read;
        indev_button = lv_indev_drv_register(&indev_drv_btn);
        (void)indev_button;
    }
}

static void lv_tick_inc_cb(void *data)
{
    uint32_t tick_inc_period_ms = *((uint32_t *)data);
    lv_tick_inc(tick_inc_period_ms);
}

static esp_err_t lv_port_tick_init(void)
{
    static const uint32_t tick_inc_period_ms = 2;
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = lv_tick_inc_cb,
        .name = "",
        .arg = &tick_inc_period_ms,
        .dispatch_method = ESP_TIMER_TASK,
        .skip_unhandled_events = true,
    };

    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, tick_inc_period_ms * 1000));

    return ESP_OK;
}

#if CONFIG_LCD_LVGL_DIRECT_MODE
static void lv_port_direct_mode_copy(void)
{
    lv_disp_t *disp_refr = _lv_refr_get_disp_refreshing();

    uint8_t *buf_act = disp_refr->driver->draw_buf->buf_act;
    uint8_t *buf1 = disp_refr->driver->draw_buf->buf1;
    uint8_t *buf2 = disp_refr->driver->draw_buf->buf2;
    int h_res = disp_refr->driver->hor_res;
    int v_res = disp_refr->driver->ver_res;

    uint8_t *fb_from = buf_act;
    uint8_t *fb_to = (fb_from == buf1) ? buf2 : buf1;

    int32_t i;
    lv_coord_t x_start;
    lv_coord_t x_end;
    lv_coord_t y_start;
    lv_coord_t y_end;
    uint32_t copy_bytes_per_line;
    uint32_t bytes_to_flush;
    uint32_t bytes_per_line = h_res * 2;
    uint8_t *from;
    uint8_t *to;
    uint8_t *flush_ptr;
    for (i = 0; i < disp_refr->inv_p; i++) {
        if (disp_refr->inv_area_joined[i] == 0) {
            x_start = disp_refr->inv_areas[i].x1;
            x_end = disp_refr->inv_areas[i].x2 + 1;
            y_start = disp_refr->inv_areas[i].y1;
            y_end = disp_refr->inv_areas[i].y2 + 1;

            copy_bytes_per_line = (x_end - x_start) * 2;
            from = fb_from + (y_start * h_res + x_start) * 2;
            to = fb_to + (y_start * h_res + x_start) * 2;
            for (int y = y_start; y < y_end; y++) {
                memcpy(to, from, copy_bytes_per_line);
                from += bytes_per_line;
                to += bytes_per_line;
            }
            bytes_to_flush = (y_end - y_start) * bytes_per_line;
            flush_ptr = fb_to + y_start * bytes_per_line;

            Cache_WriteBack_Addr((uint32_t)flush_ptr, bytes_to_flush);
        }
    }
}
#else
static void lv_port_direct_mode_copy(void)
{
}
#endif

static void lvgl_task(void *args)
{
    (void)args;
    for (;;) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        lv_task_handler();
        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(LV_PORT_TASK_DELAY_MS));
    }
}
