#include "wifi_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lvgl.h"

#include "indicator/config.h"
#include "indicator/view_data.h"
#include "lv_port.h"

static const char *TAG = "wifi_ui";

static lv_obj_t *wifi_screen;
static lv_obj_t *status_label;
static lv_obj_t *message_label;
static lv_obj_t *network_dropdown;
static lv_obj_t *password_area;
static lv_obj_t *keyboard;
static lv_obj_t *connect_btn;
static lv_obj_t *forget_btn;

static struct view_data_wifi_list current_list;
static struct view_data_wifi_st last_status;
static int selected_index = -1;

static void update_connect_button_state(void);
static void show_message(const char *fmt, ...);
static void update_network_dropdown(void);
static void update_status_label(const struct view_data_wifi_st *st);
static void request_wifi_scan(void);

static void scan_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    show_message("Scanning for networks...");
    request_wifi_scan();
}

static void connect_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (selected_index < 0 || selected_index >= (int)current_list.cnt) {
        show_message("Select a network first");
        return;
    }

    struct view_data_wifi_config cfg = {0};
    strlcpy((char *)cfg.ssid, current_list.aps[selected_index].ssid, sizeof(cfg.ssid));

    const char *password = lv_textarea_get_text(password_area);
    bool needs_password = current_list.aps[selected_index].auth_mode;

    if (needs_password) {
        if (password == NULL || strlen(password) == 0) {
            show_message("Enter password for %s", cfg.ssid);
            return;
        }
        cfg.have_password = true;
        strlcpy((char *)cfg.password, password, sizeof(cfg.password));
    } else {
        if (password != NULL && strlen(password) > 0) {
            cfg.have_password = true;
            strlcpy((char *)cfg.password, password, sizeof(cfg.password));
        }
    }

    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, &cfg, sizeof(cfg), portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send connect event: %s", esp_err_to_name(err));
        show_message("Failed to send connect event");
        return;
    }

    show_message("Connecting to %s...", cfg.ssid);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard, NULL);
}

static void forget_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE, NULL, 0, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send forget event: %s", esp_err_to_name(err));
        show_message("Failed to clear credentials");
        return;
    }
    show_message("Clearing saved credentials...");
    lv_textarea_set_text(password_area, "");
}

static void dropdown_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    selected_index = lv_dropdown_get_selected(network_dropdown);
    if (selected_index >= (int)current_list.cnt) {
        selected_index = -1;
    }
    update_connect_button_state();

    if (selected_index >= 0) {
        const struct view_data_wifi_item *item = &current_list.aps[selected_index];
        if (!item->auth_mode) {
            lv_textarea_set_text(password_area, "");
        }
        show_message("Ready to connect to %s", item->ssid);
    }
}

static void password_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, password_area);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_label_text_fmt(lv_obj_t *label, const char *fmt, ...)
{
    if (!label || !fmt) {
        return;
    }
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    lv_label_set_text(label, buffer);
}

static void show_message(const char *fmt, ...)
{
    if (!message_label || !fmt) {
        return;
    }
    char buffer[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    lv_label_set_text(message_label, buffer);
}

static void update_connect_button_state(void)
{
    if (!connect_btn) {
        return;
    }
    if (selected_index >= 0) {
        lv_obj_clear_state(connect_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(connect_btn, LV_STATE_DISABLED);
    }
}

static void update_status_label(const struct view_data_wifi_st *st)
{
    if (!st || !status_label) {
        return;
    }
    last_status = *st;
    if (st->is_connected) {
        const char *net_state = st->is_network ? "online" : "no internet";
        set_label_text_fmt(status_label, "Connected: %s (%s)", st->ssid, net_state);
    } else if (st->is_connecting) {
        set_label_text_fmt(status_label, "Connecting...");
    } else {
        set_label_text_fmt(status_label, "Not connected");
    }
}

static void update_network_dropdown(void)
{
    if (!network_dropdown) {
        return;
    }

    if (current_list.cnt == 0) {
        lv_dropdown_set_options(network_dropdown, "No networks found");
        lv_dropdown_set_selected(network_dropdown, 0);
        selected_index = -1;
        update_connect_button_state();
        return;
    }

    char options[WIFI_SCAN_LIST_SIZE * 48];
    size_t pos = 0;
    for (uint16_t i = 0; i < current_list.cnt; ++i) {
        const struct view_data_wifi_item *item = &current_list.aps[i];
        int written = snprintf(&options[pos], sizeof(options) - pos,
                               "%s (%ddBm)%s", item->ssid, item->rssi,
                               item->auth_mode ? " *" : "");
        if (written < 0) {
            written = 0;
        }
        pos += (size_t)written;
        if (pos >= sizeof(options) - 1) {
            break;
        }
        if (i + 1 < current_list.cnt) {
            options[pos++] = '\n';
        }
    }
    options[pos] = '\0';

    lv_dropdown_set_options(network_dropdown, options);

    selected_index = -1;
    if (current_list.is_connect) {
        for (uint16_t i = 0; i < current_list.cnt; ++i) {
            if (strcmp(current_list.connect.ssid, current_list.aps[i].ssid) == 0) {
                selected_index = (int)i;
                break;
            }
        }
    }

    if (selected_index >= 0) {
        lv_dropdown_set_selected(network_dropdown, (uint16_t)selected_index);
        show_message("Connected to %s", current_list.aps[selected_index].ssid);
    }

    update_connect_button_state();
}

static void request_wifi_scan(void)
{
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send scan event: %s", esp_err_to_name(err));
        show_message("Scan request failed");
    }
}

static void wifi_view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    lv_port_sem_take();
    switch (id) {
    case VIEW_EVENT_SCREEN_START: {
        if (event_data) {
            uint8_t screen = *(uint8_t *)event_data;
            if (screen == SCREEN_WIFI_CONFIG && wifi_screen) {
                lv_scr_load(wifi_screen);
                show_message("Tap Scan to list Wi-Fi networks");
            }
        }
        break;
    }
    case VIEW_EVENT_WIFI_ST: {
        if (event_data) {
            update_status_label((const struct view_data_wifi_st *)event_data);
        }
        break;
    }
    case VIEW_EVENT_WIFI_LIST: {
        if (event_data) {
            memcpy(&current_list, event_data, sizeof(current_list));
            update_network_dropdown();
        }
        break;
    }
    case VIEW_EVENT_WIFI_CONNECT_RET: {
        if (event_data) {
            const struct view_data_wifi_connet_ret_msg *msg = (const struct view_data_wifi_connet_ret_msg *)event_data;
            show_message("%s", msg->msg);
        }
        break;
    }
    default:
        break;
    }
    lv_port_sem_give();
}

void hamview_wifi_ui_init(void)
{
    memset(&current_list, 0, sizeof(current_list));
    memset(&last_status, 0, sizeof(last_status));
    selected_index = -1;

    lv_port_sem_take();

    wifi_screen = lv_obj_create(NULL);
    lv_obj_set_size(wifi_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(wifi_screen, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(wifi_screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(wifi_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(wifi_screen, 20, 0);
    lv_obj_set_style_pad_row(wifi_screen, 12, 0);

    lv_obj_t *title = lv_label_create(wifi_screen);
    lv_label_set_text(title, "SenseCAP HamView – Wi-Fi Setup");
    lv_obj_set_style_text_color(title, lv_color_hex(0x50FA7B), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    status_label = lv_label_create(wifi_screen);
    set_label_text_fmt(status_label, "Not connected");

    message_label = lv_label_create(wifi_screen);
    lv_label_set_text(message_label, "Tap Scan to list Wi-Fi networks");
    lv_obj_set_width(message_label, LV_PCT(100));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *scan_btn = lv_btn_create(wifi_screen);
    lv_obj_set_width(scan_btn, LV_PCT(100));
    lv_obj_add_event_cb(scan_btn, scan_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan Networks");
    lv_obj_center(scan_label);

    network_dropdown = lv_dropdown_create(wifi_screen);
    lv_obj_set_width(network_dropdown, LV_PCT(100));
    lv_dropdown_set_options(network_dropdown, "No networks scanned yet");
    lv_obj_add_event_cb(network_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    password_area = lv_textarea_create(wifi_screen);
    lv_obj_set_width(password_area, LV_PCT(100));
    lv_textarea_set_password_mode(password_area, true);
    lv_textarea_set_placeholder_text(password_area, "Password (leave empty for open networks)");
    lv_textarea_set_one_line(password_area, true);
    lv_obj_add_event_cb(password_area, password_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_row = lv_obj_create(wifi_screen);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);

    connect_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(connect_btn, 1);
    lv_obj_add_state(connect_btn, LV_STATE_DISABLED);
    lv_obj_add_event_cb(connect_btn, connect_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_center(connect_label);

    forget_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(forget_btn, 1);
    lv_obj_add_event_cb(forget_btn, forget_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t *forget_label = lv_label_create(forget_btn);
    lv_label_set_text(forget_label, "Forget Wi-Fi");
    lv_obj_center(forget_label);

    keyboard = lv_keyboard_create(wifi_screen);
    lv_obj_set_width(keyboard, LV_PCT(100));
    lv_keyboard_set_textarea(keyboard, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);

    lv_scr_load(wifi_screen);

    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, wifi_view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, wifi_view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, wifi_view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, wifi_view_event_handler, NULL, NULL));
}

void hamview_wifi_ui_show(void)
{
    lv_port_sem_take();
    if (wifi_screen) {
        lv_scr_load(wifi_screen);
        show_message("Tap Scan to list Wi-Fi networks");
    }
    lv_port_sem_give();
}
