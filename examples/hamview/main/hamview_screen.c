#include "hamview_screen.h"

#include <stdbool.h>

#include "bsp_board.h"
#include "bsp_lcd.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hamview_screen";

static bool s_initialized = false;
static bool s_sleeping = false;
static bool s_backlight_on = false;
static uint8_t s_applied_brightness = 0;
static uint8_t s_brightness_percent = 100;
static uint64_t s_timeout_us = 0;
static uint64_t s_last_activity_us = 0;
static bool s_pwm_initialized = false;
static bool s_pwm_supported = false;

#define BACKLIGHT_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER    LEDC_TIMER_1
#define BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_3
#define BACKLIGHT_LEDC_FREQ_HZ  (5000)
#define BACKLIGHT_LEDC_RES      LEDC_TIMER_13_BIT
#define BACKLIGHT_DUTY_MAX      ((1U << BACKLIGHT_LEDC_RES) - 1U)

static void set_backlight(bool enable);
static esp_err_t ensure_backlight_pwm(void);
static uint32_t duty_for_percent(uint8_t percent, bool active_high);
static void apply_backlight(bool enable);

static esp_err_t ensure_backlight_pwm(void)
{
    if (s_pwm_initialized) {
        return s_pwm_supported ? ESP_OK : ESP_FAIL;
    }

    const board_res_desc_t *brd = bsp_board_get_description();
    if (!brd || brd->GPIO_LCD_BL == GPIO_NUM_NC) {
        s_pwm_initialized = true;
        s_pwm_supported = false;
        return ESP_ERR_NOT_SUPPORTED;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_RES,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        s_pwm_initialized = true;
        s_pwm_supported = false;
        ESP_LOGW(TAG, "ledc timer init failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t chan_cfg = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .gpio_num = brd->GPIO_LCD_BL,
        .duty = 0,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE,
    };

    err = ledc_channel_config(&chan_cfg);
    if (err == ESP_OK) {
        s_pwm_supported = true;
    } else {
        s_pwm_supported = false;
        ESP_LOGW(TAG, "ledc channel init failed: %s", esp_err_to_name(err));
    }
    s_pwm_initialized = true;
    return err;
}

static uint32_t duty_for_percent(uint8_t percent, bool active_high)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = ((uint32_t)percent * BACKLIGHT_DUTY_MAX) / 100U;
    if (!active_high) {
        duty = BACKLIGHT_DUTY_MAX - duty;
    }
    return duty;
}

static void apply_backlight(bool enable)
{
    uint8_t target_percent = enable ? s_brightness_percent : 0;
    if (target_percent > 100) {
        target_percent = 100;
    }
    bool target_on = target_percent > 0;

    if (s_backlight_on == target_on && s_applied_brightness == target_percent) {
        return;
    }

    const board_res_desc_t *brd = bsp_board_get_description();
    bool active_high = !brd || brd->GPIO_LCD_BL_ON != 0;

    if (ensure_backlight_pwm() == ESP_OK && s_pwm_supported) {
        uint32_t duty = target_on ? duty_for_percent(target_percent, active_high)
                                  : (active_high ? 0U : BACKLIGHT_DUTY_MAX);
        esp_err_t err = ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
        if (err == ESP_OK) {
            err = ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ledc duty set failed: %s", esp_err_to_name(err));
        } else {
            s_backlight_on = target_on;
            s_applied_brightness = target_percent;
            ESP_LOGI(TAG, "screen %s (brightness %u%%)", s_backlight_on ? "on" : "off", target_percent);
        }
        return;
    }

    esp_err_t err = bsp_lcd_set_backlight(target_on);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight set failed: %s", esp_err_to_name(err));
        return;
    }

    s_backlight_on = target_on;
    s_applied_brightness = target_percent;
    ESP_LOGI(TAG, "screen %s%s", s_backlight_on ? "on" : "off",
             s_backlight_on ? " (digital only)" : "");
}

static void set_backlight(bool enable)
{
    apply_backlight(enable);
}

static void screen_wake(void)
{
    if (!s_initialized) {
        return;
    }
    set_backlight(true);
    s_sleeping = false;
    s_last_activity_us = esp_timer_get_time();
}

static void screen_sleep(void)
{
    if (!s_initialized) {
        return;
    }
    set_backlight(false);
    s_sleeping = true;
}

void hamview_screen_init(void)
{
    if (!s_initialized) {
        s_initialized = true;
    }
    s_sleeping = false;
    s_timeout_us = 0;
    s_last_activity_us = esp_timer_get_time();
    s_backlight_on = false;
    s_applied_brightness = 0;
    set_backlight(true);
}

void hamview_screen_configure(uint16_t timeout_minutes)
{
    if (!s_initialized) {
        hamview_screen_init();
    }
    if (timeout_minutes > 720) {
        timeout_minutes = 720;
    }
    s_timeout_us = (uint64_t)timeout_minutes * 60ULL * 1000000ULL;
    s_last_activity_us = esp_timer_get_time();
    if (s_timeout_us == 0) {
        screen_wake();
    }
}

void hamview_screen_record_activity(void)
{
    if (!s_initialized) {
        hamview_screen_init();
    }
    s_last_activity_us = esp_timer_get_time();
    if (s_sleeping) {
        screen_wake();
    }
}

void hamview_screen_handle_button(uint32_t key, lv_indev_state_t state)
{
    if (!s_initialized) {
        hamview_screen_init();
    }
    bool was_sleeping = s_sleeping;
    if (state == LV_INDEV_STATE_PRESSED) {
        hamview_screen_record_activity();
        if (was_sleeping) {
            return;
        }
        if (key == LV_KEY_ENTER) {
            screen_sleep();
        }
    }
}

void hamview_screen_timer_tick(void)
{
    if (!s_initialized) {
        return;
    }
    if (s_sleeping || s_timeout_us == 0) {
        return;
    }
    uint64_t now = esp_timer_get_time();
    if (now >= s_last_activity_us && (now - s_last_activity_us) >= s_timeout_us) {
        screen_sleep();
    }
}

void hamview_screen_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_brightness_percent = percent;
    if (!s_initialized) {
        return;
    }
    if (!s_sleeping) {
        set_backlight(true);
    }
}
