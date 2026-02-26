#include "hamview_ui.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "radio.h"
#include "sdkconfig.h"
#if defined(__has_include)
#if __has_include("esp_bt.h") && defined(CONFIG_BT_BLE_ENABLED) && CONFIG_BT_BLE_ENABLED && defined(CONFIG_BT_BLUEDROID_ENABLED) && CONFIG_BT_BLUEDROID_ENABLED && defined(CONFIG_BT_BLE_42_FEATURES_SUPPORTED) && CONFIG_BT_BLE_42_FEATURES_SUPPORTED
#define HAMVIEW_HAS_BLE 1
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_device.h"
#else
#define HAMVIEW_HAS_BLE 0
#endif

#if __has_include("esp_gap_bt_api.h") && defined(CONFIG_BT_CLASSIC_ENABLED) && CONFIG_BT_CLASSIC_ENABLED
#define HAMVIEW_HAS_BT_CLASSIC 1
#include "esp_gap_bt_api.h"
#else
#define HAMVIEW_HAS_BT_CLASSIC 0
#endif
#else
#define HAMVIEW_HAS_BLE 0
#endif
#include "lvgl.h"

#include "hamview_backend.h"
#include "hamview_settings.h"
#include "hamview_weather.h"
#include "hamview_screen.h"
#include "hamview_alert.h"
#include "hamview_event_log.h"
#include "hamview_icom.h"
#include "indicator/config.h"
#include "indicator/view_data.h"
#include "lv_port.h"
#include "wifi_ui.h"

static const char *TAG = "hamview_ui";
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static lv_obj_t *dashboard_screen = NULL;
static lv_obj_t *tabview = NULL;
static lv_obj_t *settings_tab = NULL;
static lv_obj_t *radar_tab = NULL;
static lv_obj_t *ic705_tab = NULL;
static lv_obj_t *table = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *ham_status_label = NULL;
static lv_obj_t *wifi_signal_label = NULL;
static lv_obj_t *error_label = NULL;
static lv_obj_t *message_label = NULL;
static lv_obj_t *settings_modal = NULL;
static lv_obj_t *username_field = NULL;
static lv_obj_t *password_field = NULL;
static lv_obj_t *callsign_field = NULL;
static lv_obj_t *band_field = NULL;
static lv_obj_t *weather_zip_field = NULL;
static lv_obj_t *icom_wifi_ip_field = NULL;
static lv_obj_t *icom_wifi_user_field = NULL;
static lv_obj_t *icom_wifi_pass_field = NULL;
static lv_obj_t *icom_wifi_port_field = NULL;
static lv_obj_t *icom_wifi_switch = NULL;
static lv_obj_t *spot_ttl_dropdown = NULL;
static lv_obj_t *screen_timeout_dropdown = NULL;
static lv_obj_t *brightness_dropdown = NULL;
static lv_obj_t *alert_callsigns_field = NULL;
static lv_obj_t *alert_states_field = NULL;
static lv_obj_t *alert_countries_field = NULL;
static lv_obj_t *settings_keyboard = NULL;
static lv_obj_t *settings_save_btn = NULL;
static lv_obj_t *settings_cancel_btn = NULL;
static lv_obj_t *time_format_switch = NULL;
static lv_obj_t *temp_unit_switch = NULL;
static lv_obj_t *spot_age_dropdown = NULL;
static lv_timer_t *refresh_timer = NULL;
static bool dashboard_loaded = false;
static lv_obj_t *mode_filter_dropdown = NULL;
static lv_obj_t *band_filter_dropdown = NULL;
static lv_obj_t *activity_chart = NULL;
static lv_obj_t *mode_chart = NULL;
static lv_chart_series_t *activity_series = NULL;
static lv_chart_series_t *mode_series = NULL;
static lv_obj_t *activity_summary_label = NULL;
static lv_obj_t *mode_summary_label = NULL;
static lv_obj_t *daily_chart = NULL;
static lv_chart_series_t *daily_series = NULL;
static lv_obj_t *daily_summary_label = NULL;

typedef enum {
    SPOT_FILTER_MODE_ALL = 0,
    SPOT_FILTER_MODE_CW,
    SPOT_FILTER_MODE_DIGITAL,
    SPOT_FILTER_MODE_VOICE,
    SPOT_FILTER_MODE_OTHER,
} spot_filter_mode_t;

typedef enum {
    SPOT_FILTER_BAND_ALL = 0,
    SPOT_FILTER_BAND_160M,
    SPOT_FILTER_BAND_80M,
    SPOT_FILTER_BAND_60M,
    SPOT_FILTER_BAND_40M,
    SPOT_FILTER_BAND_30M,
    SPOT_FILTER_BAND_20M,
    SPOT_FILTER_BAND_17M,
    SPOT_FILTER_BAND_15M,
    SPOT_FILTER_BAND_12M,
    SPOT_FILTER_BAND_10M,
    SPOT_FILTER_BAND_6M,
    SPOT_FILTER_BAND_2M,
    SPOT_FILTER_BAND_70CM,
} spot_filter_band_t;

typedef struct {
    spot_filter_mode_t mode_category;
    spot_filter_band_t band_category;
    bool is_new;
    bool is_priority;
    bool occupied;
} spot_row_info_t;

static spot_filter_mode_t current_mode_filter = SPOT_FILTER_MODE_ALL;
static spot_filter_band_t current_band_filter = SPOT_FILTER_BAND_ALL;
static spot_row_info_t current_row_info[HAMVIEW_MAX_SPOTS];

static lv_obj_t *weather_location_label = NULL;
static lv_obj_t *weather_condition_label = NULL;
static lv_obj_t *weather_temp_label = NULL;
static lv_obj_t *weather_extra_label = NULL;
static lv_obj_t *weather_updated_label = NULL;
static lv_obj_t *weather_sun_label = NULL;
static lv_obj_t *weather_alerts_container = NULL;
static lv_obj_t *weather_warning_label = NULL;
static lv_obj_t *weather_forecast_label = NULL;
static lv_obj_t *weather_action_row = NULL;
static lv_obj_t *weather_refresh_btn = NULL;
static lv_obj_t *weather_units_btn = NULL;
static lv_obj_t *weather_mute_btn = NULL;
static lv_obj_t *weather_refresh_label = NULL;
static lv_obj_t *weather_units_label = NULL;
static lv_obj_t *weather_mute_label = NULL;
static lv_obj_t *header_container = NULL;
static lv_obj_t *status_row = NULL;
static lv_obj_t *clock_row = NULL;
static lv_obj_t *spots_container = NULL;
static lv_obj_t *spots_table_card = NULL;
static lv_obj_t *activity_container = NULL;
static lv_obj_t *weather_container = NULL;
static lv_obj_t *log_container = NULL;
static lv_obj_t *settings_btn = NULL;
static lv_obj_t *theme_toggle_btn = NULL;
static lv_obj_t *theme_toggle_label = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *ham_local_time_label = NULL;
static lv_obj_t *ham_utc_time_label = NULL;
static lv_obj_t *weather_local_time_label = NULL;
static lv_obj_t *weather_utc_time_label = NULL;
static lv_obj_t *log_label = NULL;
static lv_obj_t *rf_radar_label = NULL;
static lv_obj_t *rf_wifi_switch = NULL;
static lv_obj_t *rf_ble_switch = NULL;
static lv_obj_t *rf_lora_switch = NULL;
static lv_obj_t *settings_wifi_scan_btn = NULL;
static lv_obj_t *ic705_status_label = NULL;
static lv_obj_t *ic705_link_label = NULL;
static lv_obj_t *ic705_target_label = NULL;
static lv_obj_t *ic705_device_label = NULL;
static lv_obj_t *ic705_freq_label = NULL;
static lv_obj_t *ic705_mode_label = NULL;
static lv_obj_t *ic705_smeter_label = NULL;
static lv_obj_t *ic705_smeter_bar = NULL;
static lv_obj_t *ic705_last_update_label = NULL;
static lv_obj_t *ic705_connect_btn = NULL;
static lv_obj_t *ic705_select_modal = NULL;
static lv_obj_t *ic705_select_card = NULL;
static lv_obj_t *ic705_select_title = NULL;
static lv_obj_t *ic705_select_hint = NULL;
static lv_obj_t *ic705_select_list = NULL;
static lv_obj_t *ic705_rescan_btn = NULL;
static lv_obj_t *ic705_close_btn = NULL;

#define HAMVIEW_THEME_MAX_CARDS 32
typedef struct {
    lv_obj_t *obj;
    bool alternate;
} theme_card_entry_t;

static theme_card_entry_t theme_card_registry[HAMVIEW_THEME_MAX_CARDS];
static size_t theme_card_count = 0;

static void update_spots_table(void);
static void update_spots_table_internal(void);
static void update_spots_table_async(void *param);
static void update_status_labels(void);
static void update_weather_panel(void);
static void update_clock_labels(void);
static void open_settings_modal(void);
static void close_settings_modal(void);
static void settings_textarea_event_cb(lv_event_t *e);
static void settings_keyboard_event_cb(lv_event_t *e);
static void settings_time_format_event_cb(lv_event_t *e);
static void settings_temp_unit_event_cb(lv_event_t *e);
static void settings_wifi_scan_btn_cb(lv_event_t *e);
static void filter_dropdown_event_cb(lv_event_t *e);
static bool spot_passes_filters(const hamview_spot_t *spot, spot_row_info_t *info_out);
static void spots_table_draw_event(lv_event_t *e);
static void update_activity_charts(void);
static void update_activity_charts_internal(void);
static void update_activity_charts_async(void *param);
static void daily_chart_draw_event(lv_event_t *e);
static void update_event_log_panel(void);
static void update_weather_action_buttons(void);
static void weather_action_btn_event_cb(lv_event_t *e);
static void apply_theme(void);
static void theme_toggle_btn_event_cb(lv_event_t *e);
static void animate_button_feedback(lv_obj_t *btn);
static void anim_exec_btn_scale(void *obj, int32_t v);
static lv_obj_t *create_card(lv_obj_t *parent, bool alternate);
static void register_card(lv_obj_t *obj, bool alternate);
static void update_rf_radar_panel(void);
static void update_ic705_panel(void);
static void request_wifi_scan(void);
static void radar_wifi_toggle_event_cb(lv_event_t *e);
static void radar_ble_toggle_event_cb(lv_event_t *e);
static void radar_lora_toggle_event_cb(lv_event_t *e);
static void ic705_connect_btn_event_cb(lv_event_t *e);
static void ic705_select_open(void);
static void ic705_select_close(void);
static void ic705_select_refresh(void);
static void ic705_select_item_event_cb(lv_event_t *e);
static void ic705_rescan_btn_event_cb(lv_event_t *e);
static void ic705_close_btn_event_cb(lv_event_t *e);
static void rf_ble_start_scan(void);
static void rf_ble_stop_scan(void);
#if HAMVIEW_HAS_BT_CLASSIC
static void rf_bt_start_scan(void);
static void rf_bt_stop_scan(void);
#endif
static void rf_lora_start_scan(void);
static void rf_lora_stop_scan(void);

typedef struct {
    bool valid;
    bool is_day[HAMVIEW_ACTIVITY_HOURLY_COUNT];
} daily_shading_state_t;

static daily_shading_state_t daily_shading_state = {0};
static bool weather_show_celsius = false;
static bool time_use_24h = true;
static uint16_t spot_age_filter_minutes = 0;
static int8_t last_wifi_rssi = 0;
static bool last_wifi_connected = false;
static struct view_data_wifi_list rf_wifi_list;
static bool rf_wifi_list_valid = false;
static bool rf_wifi_scan_enabled = false;
static bool rf_ble_enabled = false;
static bool rf_lora_enabled = false;

typedef struct {
    char addr[18];
    char name[32];
    int rssi;
} rf_ble_device_t;

static rf_ble_device_t rf_ble_devices[6];
static size_t rf_ble_device_count = 0;
static bool rf_ble_list_dirty = false;
#if HAMVIEW_HAS_BT_CLASSIC
typedef struct {
    char addr[18];
    char name[32];
    int rssi;
} rf_bt_device_t;

static rf_bt_device_t rf_bt_devices[6];
static size_t rf_bt_device_count = 0;
static bool rf_bt_list_dirty = false;
static bool rf_bt_initialized = false;
static bool rf_bt_scanning = false;
#endif
#if HAMVIEW_HAS_BLE
static bool rf_ble_initialized = false;
static bool rf_ble_scanning = false;
#endif

static bool rf_lora_initialized = false;
static uint32_t rf_lora_rx_count = 0;
static int16_t rf_lora_last_rssi = 0;
static int8_t rf_lora_last_snr = 0;
typedef enum {
    HAMVIEW_THEME_DARK = 0,
    HAMVIEW_THEME_LIGHT,
    HAMVIEW_THEME_COUNT
} hamview_theme_t;

typedef struct {
    uint32_t background;
    uint32_t surface;
    uint32_t surface_alt;
    uint32_t shadow;
    uint32_t primary_text;
    uint32_t secondary_text;
    uint32_t muted_text;
    uint32_t accent_primary;
    uint32_t accent_secondary;
    uint32_t warning;
    uint32_t error;
    uint32_t border;
    uint32_t chart_line;
    uint32_t chart_bar;
    uint32_t chart_day_fill;
    uint32_t chart_night_fill;
} hamview_theme_palette_t;

static const hamview_theme_palette_t g_theme_palettes[HAMVIEW_THEME_COUNT] = {
    [HAMVIEW_THEME_DARK] = {
        .background = 0x0D1117,
        .surface = 0x161B22,
        .surface_alt = 0x1F2630,
        .shadow = 0x0A0D12,
        .primary_text = 0xF8FAFC,
        .secondary_text = 0xC9D1D9,
        .muted_text = 0x8B949E,
        .accent_primary = 0x58A6FF,
        .accent_secondary = 0x7EE787,
        .warning = 0xF2CC60,
        .error = 0xFF6B6B,
        .border = 0x2D333B,
        .chart_line = 0x7EE787,
        .chart_bar = 0x79C0FF,
        .chart_day_fill = 0x283018,
        .chart_night_fill = 0x121821
    },
    [HAMVIEW_THEME_LIGHT] = {
        .background = 0xF6F8FA,
        .surface = 0xFFFFFF,
        .surface_alt = 0xE7ECF2,
        .shadow = 0xD0D7DE,
        .primary_text = 0x24292F,
        .secondary_text = 0x57606A,
        .muted_text = 0x6E7781,
        .accent_primary = 0x0969DA,
        .accent_secondary = 0x1A7F37,
        .warning = 0xBF8700,
        .error = 0xD12424,
        .border = 0xD0D7DE,
        .chart_line = 0x1A7F37,
        .chart_bar = 0x218BFF,
        .chart_day_fill = 0xDAE8FF,
        .chart_night_fill = 0xCBD7E1
    }
};

static hamview_theme_t current_theme = HAMVIEW_THEME_DARK;

static inline const hamview_theme_palette_t *theme_palette(void)
{
    return &g_theme_palettes[current_theme];
}

static inline lv_color_t theme_bg(void) { return lv_color_hex(theme_palette()->background); }
static inline lv_color_t theme_surface(void) { return lv_color_hex(theme_palette()->surface); }
static inline lv_color_t theme_surface_alt(void) { return lv_color_hex(theme_palette()->surface_alt); }
static inline lv_color_t theme_shadow(void) { return lv_color_hex(theme_palette()->shadow); }
static inline lv_color_t theme_primary_text(void) { return lv_color_hex(theme_palette()->primary_text); }
static inline lv_color_t theme_secondary_text(void) { return lv_color_hex(theme_palette()->secondary_text); }
static inline lv_color_t theme_muted_text(void) { return lv_color_hex(theme_palette()->muted_text); }
static inline lv_color_t theme_accent_primary(void) { return lv_color_hex(theme_palette()->accent_primary); }
static inline lv_color_t theme_accent_secondary(void) { return lv_color_hex(theme_palette()->accent_secondary); }
static inline lv_color_t theme_warning(void) { return lv_color_hex(theme_palette()->warning); }
static inline lv_color_t theme_error(void) { return lv_color_hex(theme_palette()->error); }
static inline lv_color_t theme_border(void) { return lv_color_hex(theme_palette()->border); }
static inline lv_color_t theme_chart_line(void) { return lv_color_hex(theme_palette()->chart_line); }
static inline lv_color_t theme_chart_bar(void) { return lv_color_hex(theme_palette()->chart_bar); }
static inline lv_color_t theme_chart_day_fill(void) { return lv_color_hex(theme_palette()->chart_day_fill); }
static inline lv_color_t theme_chart_night_fill(void) { return lv_color_hex(theme_palette()->chart_night_fill); }

static void set_label_primary(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_primary_text(), 0);
    }
}

static void set_label_secondary(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_secondary_text(), 0);
    }
}

static void set_label_muted(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_muted_text(), 0);
    }
}

static void set_label_accent_primary(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_accent_primary(), 0);
    }
}

static void set_label_accent_secondary(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_accent_secondary(), 0);
    }
}

static void set_label_warning(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_warning(), 0);
    }
}

static void set_label_error(lv_obj_t *label)
{
    if (label) {
        lv_obj_set_style_text_color(label, theme_error(), 0);
    }
}

static void configure_card_surface(lv_obj_t *obj, bool alternate)
{
    if (!obj) return;
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, alternate ? theme_surface_alt() : theme_surface(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 18, 0);
    lv_obj_set_style_pad_all(obj, 18, 0);
    lv_obj_set_style_pad_row(obj, 12, 0);
    lv_obj_set_style_pad_column(obj, 14, 0);
    lv_obj_set_style_shadow_width(obj, 18, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 6, 0);
    lv_obj_set_style_shadow_color(obj, theme_shadow(), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, theme_border(), 0);
}

static void register_card(lv_obj_t *obj, bool alternate)
{
    if (!obj) return;
    for (size_t i = 0; i < theme_card_count; ++i) {
        if (theme_card_registry[i].obj == obj) {
            theme_card_registry[i].alternate = alternate;
            return;
        }
    }
    if (theme_card_count < HAMVIEW_THEME_MAX_CARDS) {
        theme_card_registry[theme_card_count].obj = obj;
        theme_card_registry[theme_card_count].alternate = alternate;
        ++theme_card_count;
    }
}

static lv_obj_t *create_card(lv_obj_t *parent, bool alternate)
{
    lv_obj_t *card = lv_obj_create(parent);
    configure_card_surface(card, alternate);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    register_card(card, alternate);
    return card;
}

static void anim_exec_btn_scale(void *obj, int32_t v)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, (lv_coord_t)v, 0);
}

static void style_primary_button(lv_obj_t *btn)
{
    if (!btn) {
        return;
    }

    lv_obj_set_style_bg_color(btn, theme_surface_alt(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, theme_accent_primary(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, theme_accent_primary(), LV_STATE_PRESSED);

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        set_label_primary(label);
    }
}

static void style_dropdown(lv_obj_t *dropdown)
{
    if (!dropdown) {
        return;
    }

    lv_obj_set_style_bg_color(dropdown, theme_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, theme_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, theme_primary_text(), LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, theme_primary_text(), LV_PART_SELECTED);
    lv_obj_set_style_text_color(dropdown, theme_primary_text(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(dropdown, theme_surface(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(dropdown, theme_surface(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_ITEMS);
}

static void style_textarea(lv_obj_t *ta)
{
    if (!ta) {
        return;
    }

    lv_obj_set_style_bg_color(ta, theme_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, theme_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, theme_primary_text(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, theme_muted_text(), LV_PART_TEXTAREA_PLACEHOLDER);
}

static void style_table(lv_obj_t *tbl)
{
    if (!tbl) {
        return;
    }

    lv_obj_set_style_bg_color(tbl, theme_surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tbl, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tbl, theme_border(), LV_PART_MAIN);
    lv_obj_set_style_text_color(tbl, theme_primary_text(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tbl, theme_surface_alt(), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(tbl, LV_OPA_40, LV_PART_SELECTED);
}

static void apply_theme(void)
{
    if (dashboard_screen) {
        lv_obj_set_style_bg_color(dashboard_screen, theme_bg(), 0);
        lv_obj_set_style_bg_opa(dashboard_screen, LV_OPA_COVER, 0);
    }

    if (tabview) {
        lv_obj_set_style_bg_color(tabview, theme_bg(), 0);
        lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);
        lv_obj_t *tab_content = lv_tabview_get_content(tabview);
        if (tab_content) {
            lv_obj_set_style_bg_color(tab_content, theme_bg(), 0);
            lv_obj_set_style_bg_opa(tab_content, LV_OPA_TRANSP, 0);
        }
    }

    for (size_t i = 0; i < theme_card_count; ++i) {
        lv_obj_t *card = theme_card_registry[i].obj;
        if (card) {
            configure_card_surface(card, theme_card_registry[i].alternate);
        }
    }

    style_primary_button(settings_btn);
    style_primary_button(theme_toggle_btn);
    style_primary_button(weather_refresh_btn);
    style_primary_button(weather_units_btn);
    style_primary_button(weather_mute_btn);
    style_primary_button(settings_wifi_scan_btn);
    style_primary_button(settings_save_btn);
    style_primary_button(settings_cancel_btn);

    style_dropdown(mode_filter_dropdown);
    style_dropdown(band_filter_dropdown);
    style_dropdown(spot_ttl_dropdown);
    style_dropdown(screen_timeout_dropdown);
    style_dropdown(brightness_dropdown);

    style_textarea(username_field);
    style_textarea(password_field);
    style_textarea(callsign_field);
    style_textarea(band_field);
    style_textarea(weather_zip_field);
    style_textarea(icom_wifi_ip_field);
    style_textarea(icom_wifi_user_field);
    style_textarea(icom_wifi_pass_field);
    style_textarea(icom_wifi_port_field);
    style_textarea(alert_callsigns_field);
    style_textarea(alert_states_field);
    style_textarea(alert_countries_field);

    if (settings_modal) {
        configure_card_surface(settings_modal, false);
        lv_obj_set_style_pad_all(settings_modal, 20, 0);
        lv_obj_set_style_shadow_width(settings_modal, 24, 0);
        lv_obj_set_style_shadow_opa(settings_modal, LV_OPA_40, 0);
        lv_obj_set_flex_flow(settings_modal, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(settings_modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    }

    if (ic705_select_modal) {
        lv_obj_set_style_bg_color(ic705_select_modal, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(ic705_select_modal, LV_OPA_70, 0);
    }
    if (ic705_select_card) {
        configure_card_surface(ic705_select_card, false);
        lv_obj_set_style_pad_all(ic705_select_card, 18, 0);
        lv_obj_set_style_shadow_width(ic705_select_card, 20, 0);
        lv_obj_set_style_shadow_opa(ic705_select_card, LV_OPA_40, 0);
        lv_obj_set_flex_flow(ic705_select_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ic705_select_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }
    if (ic705_select_list) {
        lv_obj_set_style_bg_color(ic705_select_list, theme_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ic705_select_list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(ic705_select_list, theme_border(), LV_PART_MAIN);
        lv_obj_set_style_border_width(ic705_select_list, 1, LV_PART_MAIN);
        lv_obj_set_style_text_color(ic705_select_list, theme_primary_text(), LV_PART_ITEMS);
    }
    if (ic705_select_title) {
        set_label_accent_primary(ic705_select_title);
    }
    if (ic705_select_hint) {
        set_label_muted(ic705_select_hint);
    }
    style_primary_button(ic705_rescan_btn);
    style_primary_button(ic705_close_btn);

    style_table(table);

    if (title_label) {
        set_label_accent_secondary(title_label);
    }
    if (wifi_status_label) {
        set_label_accent_primary(wifi_status_label);
    }
    if (ham_status_label) {
        set_label_accent_primary(ham_status_label);
    }
    if (error_label) {
        set_label_error(error_label);
    }
    if (message_label) {
        set_label_secondary(message_label);
    }
    if (ham_local_time_label) {
        set_label_accent_secondary(ham_local_time_label);
    }
    if (ham_utc_time_label) {
        set_label_accent_secondary(ham_utc_time_label);
    }
    if (weather_local_time_label) {
        set_label_accent_primary(weather_local_time_label);
    }
    if (weather_utc_time_label) {
        set_label_accent_primary(weather_utc_time_label);
    }
    if (weather_location_label) {
        set_label_primary(weather_location_label);
    }
    if (weather_condition_label) {
        set_label_primary(weather_condition_label);
    }
    if (weather_temp_label) {
        set_label_accent_secondary(weather_temp_label);
    }
    if (weather_extra_label) {
        set_label_secondary(weather_extra_label);
    }
    if (weather_forecast_label) {
        set_label_secondary(weather_forecast_label);
    }
    if (weather_updated_label) {
        set_label_muted(weather_updated_label);
    }
    if (log_label) {
        set_label_primary(log_label);
    }
    if (activity_summary_label) {
        set_label_secondary(activity_summary_label);
    }
    if (mode_summary_label) {
        set_label_secondary(mode_summary_label);
    }
    if (daily_summary_label) {
        set_label_secondary(daily_summary_label);
    }
    if (theme_toggle_label) {
        const char *toggle_text = (current_theme == HAMVIEW_THEME_DARK)
                                      ? LV_SYMBOL_EYE_OPEN " Dark Mode"
                                      : LV_SYMBOL_EYE_CLOSE " Light Mode";
        lv_label_set_text(theme_toggle_label, toggle_text);
        set_label_primary(theme_toggle_label);
    }

    if (activity_chart) {
        lv_obj_set_style_bg_color(activity_chart, theme_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(activity_chart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(activity_chart, theme_secondary_text(), LV_PART_TICKS);
    }
    if (activity_chart && activity_series) {
        lv_chart_set_series_color(activity_chart, activity_series, theme_chart_line());
    }
    if (mode_chart) {
        lv_obj_set_style_bg_color(mode_chart, theme_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mode_chart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(mode_chart, theme_secondary_text(), LV_PART_TICKS);
    }
    if (mode_chart && mode_series) {
        lv_chart_set_series_color(mode_chart, mode_series, theme_chart_bar());
    }
    if (daily_chart) {
        lv_obj_set_style_bg_color(daily_chart, theme_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(daily_chart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(daily_chart, theme_secondary_text(), LV_PART_TICKS);
    }
    if (daily_chart && daily_series) {
        lv_chart_set_series_color(daily_chart, daily_series, theme_warning());
    }

    if (table) {
        lv_obj_invalidate(table);
    }
    if (activity_chart) {
        lv_obj_invalidate(activity_chart);
    }
    if (mode_chart) {
        lv_obj_invalidate(mode_chart);
    }
    if (daily_chart) {
        lv_obj_invalidate(daily_chart);
    }

    update_status_labels();
    update_weather_panel();
    update_weather_action_buttons();
    update_event_log_panel();
    update_clock_labels();
}

static void animate_button_feedback(lv_obj_t *btn)
{
    if (!btn) {
        return;
    }

    lv_obj_update_layout(btn);
    lv_coord_t pivot_x = lv_obj_get_width(btn) / 2;
    lv_coord_t pivot_y = lv_obj_get_height(btn) / 2;
    lv_obj_set_style_transform_pivot_x(btn, pivot_x, 0);
    lv_obj_set_style_transform_pivot_y(btn, pivot_y, 0);

    const int32_t scale_default = 256;
    const int32_t scale_pressed = 230;

    lv_anim_t anim_down;
    lv_anim_init(&anim_down);
    lv_anim_set_var(&anim_down, btn);
    lv_anim_set_values(&anim_down, scale_default, scale_pressed);
    lv_anim_set_time(&anim_down, 90);
    lv_anim_set_path_cb(&anim_down, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim_down, anim_exec_btn_scale);
    lv_anim_start(&anim_down);

    lv_anim_t anim_up;
    lv_anim_init(&anim_up);
    lv_anim_set_var(&anim_up, btn);
    lv_anim_set_values(&anim_up, scale_pressed, scale_default);
    lv_anim_set_time(&anim_up, 140);
    lv_anim_set_delay(&anim_up, 90);
    lv_anim_set_path_cb(&anim_up, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim_up, anim_exec_btn_scale);
    lv_anim_start(&anim_up);
}

typedef enum {
    WEATHER_ACTION_REFRESH = 0,
    WEATHER_ACTION_TOGGLE_UNITS,
    WEATHER_ACTION_TOGGLE_ALERT_MUTE,
} weather_action_t;

static const uint16_t SPOT_TTL_MINUTES_OPTIONS[] = {10, 20, 30, 45, 60, 90, 120};
static const size_t SPOT_TTL_DEFAULT_INDEX = 2;
static const char *SPOT_TTL_DROPDOWN_OPTIONS =
    "10 minutes\n"
    "20 minutes\n"
    "30 minutes\n"
    "45 minutes\n"
    "60 minutes\n"
    "90 minutes\n"
    "120 minutes";

static const uint16_t SCREEN_TIMEOUT_MINUTES_OPTIONS[] = {0, 1, 5, 10, 15, 30};
static const size_t SCREEN_TIMEOUT_DEFAULT_INDEX = 0;
static const char *SCREEN_TIMEOUT_DROPDOWN_OPTIONS =
    "Always On\n"
    "1 minute\n"
    "5 minutes\n"
    "10 minutes\n"
    "15 minutes\n"
    "30 minutes";

static const uint16_t SCREEN_BRIGHTNESS_OPTIONS[] = {20, 40, 60, 80, 100};
static const size_t SCREEN_BRIGHTNESS_DEFAULT_INDEX = 4;
static const char *SCREEN_BRIGHTNESS_DROPDOWN_OPTIONS =
    "20%\n"
    "40%\n"
    "60%\n"
    "80%\n"
    "100%";

static const uint16_t SPOT_AGE_FILTER_OPTIONS[] = {0, 5, 10, 20, 30, 60};
static const size_t SPOT_AGE_FILTER_DEFAULT_INDEX = 0;
static const char *SPOT_AGE_FILTER_DROPDOWN_OPTIONS =
    "All ages\n"
    "5 minutes\n"
    "10 minutes\n"
    "20 minutes\n"
    "30 minutes\n"
    "60 minutes";

static const char *MODE_FILTER_OPTIONS_TEXT =
    "All Modes\n"
    "CW\n"
    "Digital\n"
    "Voice\n"
    "Other";

static const char *BAND_FILTER_OPTIONS_TEXT =
    "All Bands\n"
    "160m\n"
    "80m\n"
    "60m\n"
    "40m\n"
    "30m\n"
    "20m\n"
    "17m\n"
    "15m\n"
    "12m\n"
    "10m\n"
    "6m\n"
    "2m\n"
    "70cm";

static const char *ACTIVITY_MODE_LABELS[HAMVIEW_ACTIVITY_MODE_COUNT] = {"CW", "Digital", "Voice", "Other"};

static void update_activity_charts_internal(void)
{
    if (!activity_chart || !mode_chart || !activity_series || !mode_series) {
        return;
    }

    hamview_activity_summary_t summary = {0};
    hamview_backend_get_activity_summary(&summary);

    uint16_t max_timeline = 0;
    for (size_t i = 0; i < HAMVIEW_ACTIVITY_BUCKET_COUNT; ++i) {
        lv_coord_t value = (i < summary.bucket_count) ? (lv_coord_t)summary.timeline_buckets[i] : 0;
        lv_chart_set_value_by_id(activity_chart, activity_series, i, value);
        if (value > max_timeline) {
            max_timeline = (uint16_t)value;
        }
    }
    if (max_timeline < 1) {
        max_timeline = 1;
    }
    lv_chart_set_range(activity_chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_timeline + 1);
    lv_chart_refresh(activity_chart);

    if (activity_summary_label) {
        char text[112];
        if (summary.total_spots == 0) {
            snprintf(text, sizeof(text), "No recent spots yet.");
        } else {
            size_t offset = (size_t)snprintf(text, sizeof(text), "Buckets (old->new):");
            for (size_t i = 0; i < summary.bucket_count && offset < sizeof(text); ++i) {
                offset += (size_t)snprintf(text + offset, sizeof(text) - offset, " %u", summary.timeline_buckets[i]);
            }
        }
        lv_label_set_text(activity_summary_label, text);
    }

    uint16_t max_mode = 0;
    for (size_t i = 0; i < HAMVIEW_ACTIVITY_MODE_COUNT; ++i) {
        lv_coord_t value = (i < summary.mode_count) ? (lv_coord_t)summary.mode_counts[i] : 0;
        lv_chart_set_value_by_id(mode_chart, mode_series, i, value);
        if (value > max_mode) {
            max_mode = (uint16_t)value;
        }
    }
    if (max_mode < 1) {
        max_mode = 1;
    }
    lv_chart_set_range(mode_chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_mode + 1);
    lv_chart_refresh(mode_chart);

    if (mode_summary_label) {
        char text[128];
        snprintf(text, sizeof(text), "%s %u    %s %u    %s %u    %s %u",
                 ACTIVITY_MODE_LABELS[0], summary.mode_counts[0],
                 ACTIVITY_MODE_LABELS[1], summary.mode_counts[1],
                 ACTIVITY_MODE_LABELS[2], summary.mode_counts[2],
                 ACTIVITY_MODE_LABELS[3], summary.mode_counts[3]);
        lv_label_set_text(mode_summary_label, text);
    }

    if (!daily_chart || !daily_series) {
        return;
    }

    uint16_t max_hour = 0;
    uint32_t total_daily = 0;
    for (size_t i = 0; i < HAMVIEW_ACTIVITY_HOURLY_COUNT; ++i) {
        lv_coord_t value = (i < summary.hourly_count) ? (lv_coord_t)summary.hourly_counts[i] : 0;
        lv_chart_set_value_by_id(daily_chart, daily_series, i, value);
        if (value > max_hour) {
            max_hour = (uint16_t)value;
        }
        total_daily += (uint32_t)value;
    }
    uint16_t peak_hour = max_hour;
    if (max_hour < 1) {
        max_hour = 1;
    }
    lv_chart_set_range(daily_chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_hour + 1);
    lv_chart_refresh(daily_chart);

    if (daily_summary_label) {
        char text[128];
        snprintf(text, sizeof(text), "24h total: %u    Peak hour: %u", total_daily, peak_hour);
        lv_label_set_text(daily_summary_label, text);
    }

    memset(daily_shading_state.is_day, 0, sizeof(daily_shading_state.is_day));
    daily_shading_state.valid = false;
    if (summary.has_day_night) {
        memcpy(daily_shading_state.is_day, summary.hourly_is_day, sizeof(daily_shading_state.is_day));
        daily_shading_state.valid = true;
    }

    lv_obj_invalidate(daily_chart);
}

static void update_activity_charts_async(void *param)
{
    (void)param;
    update_activity_charts_internal();
}

static void update_activity_charts(void)
{
    if (!lv_port_is_in_lvgl_task()) {
        lv_async_call(update_activity_charts_async, NULL);
        return;
    }
    update_activity_charts_internal();
}

static void daily_chart_draw_event(lv_event_t *e)
{
    if (!daily_shading_state.valid) {
        return;
    }

    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc || dsc->class_p != &lv_chart_class || dsc->type != LV_CHART_DRAW_PART_DIV_LINE_INIT) {
        return;
    }

    lv_obj_t *chart = lv_event_get_target(e);
    lv_area_t plot_area;
    lv_obj_get_content_coords(chart, &plot_area);
    lv_coord_t width = lv_area_get_width(&plot_area);
    if (width <= 0) {
        return;
    }

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.border_opa = LV_OPA_TRANSP;

    lv_color_t day_color = theme_chart_day_fill();
    lv_color_t night_color = theme_chart_night_fill();

    for (size_t i = 0; i < HAMVIEW_ACTIVITY_HOURLY_COUNT; ++i) {
        lv_area_t segment = plot_area;
        lv_coord_t x_start = plot_area.x1 + (lv_coord_t)(((int32_t)width * (int32_t)i) / HAMVIEW_ACTIVITY_HOURLY_COUNT);
        lv_coord_t x_end = plot_area.x1 + (lv_coord_t)(((int32_t)width * (int32_t)(i + 1)) / HAMVIEW_ACTIVITY_HOURLY_COUNT) - 1;
        segment.x1 = x_start;
        segment.x2 = x_end;
        rect_dsc.bg_color = daily_shading_state.is_day[i] ? day_color : night_color;
        rect_dsc.bg_opa = daily_shading_state.is_day[i] ? LV_OPA_40 : LV_OPA_50;
        lv_draw_rect(dsc->draw_ctx, &rect_dsc, &segment);
    }
}

static uint16_t dropdown_value_for_index(uint32_t index, const uint16_t *values, size_t count, size_t default_index)
{
    if (count == 0) {
        return 0;
    }
    if (index >= count) {
        index = default_index < count ? default_index : 0;
    }
    return values[index];
}

static uint32_t dropdown_index_for_value(uint16_t value, const uint16_t *values, size_t count, size_t default_index)
{
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) {
            return (uint32_t)i;
        }
    }
    return (uint32_t)((default_index < count) ? default_index : 0);
}

static void to_uppercase(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlcpy(dst, src, dst_len);
    for (size_t i = 0; i < len; ++i) {
        dst[i] = (char)toupper((unsigned char)dst[i]);
    }
}

static double parse_frequency_mhz(const char *freq_text)
{
    if (!freq_text || !*freq_text) {
        return 0.0;
    }
    return atof(freq_text);
}

static spot_filter_band_t classify_band_from_freq(const char *freq_text)
{
    double mhz = parse_frequency_mhz(freq_text);
    if (mhz <= 0.0) {
        return SPOT_FILTER_BAND_ALL;
    }
    if (mhz >= 1.8 && mhz < 2.1) return SPOT_FILTER_BAND_160M;
    if (mhz >= 3.3 && mhz < 4.1) return SPOT_FILTER_BAND_80M;
    if (mhz >= 5.2 && mhz < 5.5) return SPOT_FILTER_BAND_60M;
    if (mhz >= 6.8 && mhz < 7.5) return SPOT_FILTER_BAND_40M;
    if (mhz >= 9.9 && mhz < 10.2) return SPOT_FILTER_BAND_30M;
    if (mhz >= 13.9 && mhz < 14.5) return SPOT_FILTER_BAND_20M;
    if (mhz >= 17.9 && mhz < 18.2) return SPOT_FILTER_BAND_17M;
    if (mhz >= 20.9 && mhz < 21.5) return SPOT_FILTER_BAND_15M;
    if (mhz >= 24.8 && mhz < 25.1) return SPOT_FILTER_BAND_12M;
    if (mhz >= 27.9 && mhz < 30.1) return SPOT_FILTER_BAND_10M;
    if (mhz >= 50.0 && mhz < 54.5) return SPOT_FILTER_BAND_6M;
    if (mhz >= 144.0 && mhz < 149.0) return SPOT_FILTER_BAND_2M;
    if (mhz >= 420.0 && mhz < 451.0) return SPOT_FILTER_BAND_70CM;
    return SPOT_FILTER_BAND_ALL;
}

static spot_filter_mode_t classify_mode_from_text(const char *mode_text)
{
    char mode_upper[32];
    to_uppercase(mode_upper, sizeof(mode_upper), mode_text ? mode_text : "");
    if (strcmp(mode_upper, "CW") == 0) {
        return SPOT_FILTER_MODE_CW;
    }
    if (strstr(mode_upper, "FT8") || strstr(mode_upper, "FT4") || strstr(mode_upper, "RTTY") ||
        strstr(mode_upper, "PSK") || strstr(mode_upper, "DIG") || strstr(mode_upper, "JS8") ||
        strstr(mode_upper, "OLIVIA") || strstr(mode_upper, "WSPR")) {
        return SPOT_FILTER_MODE_DIGITAL;
    }
    if (strstr(mode_upper, "SSB") || strstr(mode_upper, "USB") || strstr(mode_upper, "LSB") ||
        strstr(mode_upper, "AM") || strstr(mode_upper, "FM") || strstr(mode_upper, "VOICE")) {
        return SPOT_FILTER_MODE_VOICE;
    }
    if (mode_upper[0] == '\0') {
        return SPOT_FILTER_MODE_OTHER;
    }
    return SPOT_FILTER_MODE_OTHER;
}

static lv_color_t color_for_mode(spot_filter_mode_t mode)
{
    lv_color_t accent_primary = theme_accent_primary();
    lv_color_t accent_secondary = theme_accent_secondary();
    lv_color_t primary_text = theme_primary_text();

    switch (mode) {
        case SPOT_FILTER_MODE_CW:
            return accent_secondary;
        case SPOT_FILTER_MODE_DIGITAL:
            return accent_primary;
        case SPOT_FILTER_MODE_VOICE:
            return lv_color_mix(accent_primary, accent_secondary, LV_OPA_40);
        case SPOT_FILTER_MODE_OTHER:
            return lv_color_mix(primary_text, accent_primary, LV_OPA_30);
        case SPOT_FILTER_MODE_ALL:
        default:
            return primary_text;
    }
}

static lv_obj_t *create_status_card(lv_obj_t *parent, const char *title, lv_obj_t **value_out)
{
    lv_obj_t *card = create_card(parent, false);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_pad_column(card, 0, 0);
    lv_obj_set_width(card, LV_PCT(45));

    lv_obj_t *title_label_local = lv_label_create(card);
    set_label_muted(title_label_local);
    lv_obj_set_style_text_font(title_label_local, &lv_font_montserrat_16, 0);
    lv_label_set_text(title_label_local, title ? title : "Status");

    lv_obj_t *value_label = lv_label_create(card);
    set_label_accent_primary(value_label);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_label_set_text(value_label, "--");

    if (value_out) {
        *value_out = value_label;
    }

    return value_label;
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    static uint8_t scan_tick = 0;
    hamview_screen_timer_tick();
    lv_port_sem_take();
    if (dashboard_loaded) {
        update_status_labels();
        update_spots_table();
        update_weather_panel();
        update_weather_action_buttons();
        update_event_log_panel();
        update_clock_labels();
        update_rf_radar_panel();
        update_ic705_panel();
        if (rf_ble_list_dirty) {
            ic705_select_refresh();
            rf_ble_list_dirty = false;
        }
#if HAMVIEW_HAS_BT_CLASSIC
        if (rf_bt_list_dirty) {
            ic705_select_refresh();
            rf_bt_list_dirty = false;
        }
#endif
        if (++scan_tick >= 6) {
            scan_tick = 0;
            request_wifi_scan();
        }
    }
    lv_port_sem_give();
}

static void request_wifi_scan(void)
{
    if (!view_event_handle) {
        return;
    }
    if (!rf_wifi_scan_enabled) {
        return;
    }
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, 0);
}

static void update_rf_radar_panel(void)
{
    if (!rf_radar_label) {
        return;
    }

    char text[512];
    size_t offset = 0;
    offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "RF Radar\n");

    if (!rf_wifi_scan_enabled) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "Wi-Fi: disabled\n");
    } else if (!rf_wifi_list_valid || rf_wifi_list.cnt == 0) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "Wi-Fi: scan pending\n");
    } else {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "Wi-Fi (%u):\n", rf_wifi_list.cnt);
        uint16_t max = rf_wifi_list.cnt;
        if (max > 6) {
            max = 6;
        }
        for (uint16_t i = 0; i < max && offset < sizeof(text); ++i) {
            const struct view_data_wifi_item *item = &rf_wifi_list.aps[i];
            offset += (size_t)snprintf(text + offset, sizeof(text) - offset,
                                       "- %s (%d dBm)%s\n",
                                       item->ssid, item->rssi, item->auth_mode ? " *" : "");
        }
    }

    if (rf_lora_enabled) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset,
                                   "LoRa: %lu pkts  RSSI %d  SNR %d\n",
                                   (unsigned long)rf_lora_rx_count,
                                   (int)rf_lora_last_rssi,
                                   (int)rf_lora_last_snr);
    } else {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "LoRa: disabled\n");
    }

    text[sizeof(text) - 1] = '\0';
    lv_label_set_text(rf_radar_label, text);
}

static void update_ic705_panel(void)
{
    if (!ic705_status_label || !ic705_link_label || !ic705_target_label || !ic705_device_label || !ic705_freq_label || !ic705_mode_label || !ic705_smeter_label || !ic705_smeter_bar || !ic705_last_update_label) {
        return;
    }

    hamview_icom_state_t state = {0};
    hamview_icom_get_state(&state);

    if (state.connected) {
        lv_label_set_text(ic705_status_label, "Status: Connected");
    } else {
        lv_label_set_text(ic705_status_label, "Status: Disconnected");
    }

    lv_label_set_text(ic705_link_label, "Link: WiFi CI-V");

    hamview_settings_t settings;
    hamview_settings_get(&settings);
    if (settings.icom_wifi_enabled && settings.icom_wifi_ip[0]) {
        char target_line[64];
        snprintf(target_line, sizeof(target_line), "Target: %s:%u", settings.icom_wifi_ip, (unsigned)settings.icom_wifi_port);
        lv_label_set_text(ic705_target_label, target_line);
    } else {
        lv_label_set_text(ic705_target_label, "Target: --");
    }

    if (state.device_name[0]) {
        char device_line[64];
        snprintf(device_line, sizeof(device_line), "Device: %s", state.device_name);
        lv_label_set_text(ic705_device_label, device_line);
    } else if (state.device_addr[0]) {
        char device_line[64];
        snprintf(device_line, sizeof(device_line), "Device: %s", state.device_addr);
        lv_label_set_text(ic705_device_label, device_line);
    } else {
        lv_label_set_text(ic705_device_label, "Device: --");
    }

    if (state.freq_hz > 0) {
        char freq_line[48];
        snprintf(freq_line, sizeof(freq_line), "Frequency: %llu Hz", (unsigned long long)state.freq_hz);
        lv_label_set_text(ic705_freq_label, freq_line);
    } else {
        lv_label_set_text(ic705_freq_label, "Frequency: --");
    }

    if (state.mode[0]) {
        char mode_line[32];
        snprintf(mode_line, sizeof(mode_line), "Mode: %s", state.mode);
        lv_label_set_text(ic705_mode_label, mode_line);
    } else {
        lv_label_set_text(ic705_mode_label, "Mode: --");
    }

    if (state.s_meter_raw) {
        char smeter_line[32];
        snprintf(smeter_line, sizeof(smeter_line), "S-meter: %u", (unsigned)state.s_meter_raw);
        lv_label_set_text(ic705_smeter_label, smeter_line);
        lv_bar_set_value(ic705_smeter_bar, state.s_meter_raw, LV_ANIM_OFF);
    } else {
        lv_label_set_text(ic705_smeter_label, "S-meter: --");
        lv_bar_set_value(ic705_smeter_bar, 0, LV_ANIM_OFF);
    }

    if (state.last_update_ms > 0) {
        char update_line[48];
        snprintf(update_line, sizeof(update_line), "Last update: %llums", (unsigned long long)state.last_update_ms);
        lv_label_set_text(ic705_last_update_label, update_line);
    } else {
        lv_label_set_text(ic705_last_update_label, "Last update: --");
    }
}

static void radar_ble_toggle_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    rf_ble_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (rf_ble_enabled) {
#if HAMVIEW_HAS_BT_CLASSIC
        rf_bt_start_scan();
#elif HAMVIEW_HAS_BLE
        rf_ble_start_scan();
#endif
    } else {
#if HAMVIEW_HAS_BT_CLASSIC
        rf_bt_stop_scan();
#elif HAMVIEW_HAS_BLE
        rf_ble_stop_scan();
#endif
    }
    update_rf_radar_panel();
}

static void radar_wifi_toggle_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    rf_wifi_scan_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (!rf_wifi_scan_enabled) {
        memset(&rf_wifi_list, 0, sizeof(rf_wifi_list));
        rf_wifi_list_valid = false;
    } else {
        request_wifi_scan();
    }
    update_rf_radar_panel();
}

static void radar_lora_toggle_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    rf_lora_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (rf_lora_enabled) {
        rf_lora_start_scan();
    } else {
        rf_lora_stop_scan();
    }
    update_rf_radar_panel();
}

static void ic705_connect_btn_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (!HAMVIEW_HAS_BLE) {
        hamview_event_log_append("ic705", "BLE not available in this build");
        return;
    }

    if (!rf_ble_enabled) {
        rf_ble_enabled = true;
        if (rf_ble_switch) {
            lv_obj_add_state(rf_ble_switch, LV_STATE_CHECKED);
        }
#if HAMVIEW_HAS_BT_CLASSIC
        rf_bt_start_scan();
#elif HAMVIEW_HAS_BLE
        rf_ble_start_scan();
#endif
        update_rf_radar_panel();
    } else {
#if HAMVIEW_HAS_BT_CLASSIC
        if (!rf_bt_scanning) {
            rf_bt_start_scan();
        }
#elif HAMVIEW_HAS_BLE
        if (!rf_ble_scanning) {
            rf_ble_start_scan();
        }
#endif
    }

    ic705_select_open();
}

static void ic705_select_open(void)
{
    if (!ic705_select_modal) {
        ic705_select_modal = lv_obj_create(lv_layer_top());
        lv_obj_set_size(ic705_select_modal, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(ic705_select_modal, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(ic705_select_modal, LV_OPA_70, 0);
        lv_obj_set_style_border_width(ic705_select_modal, 0, 0);

        ic705_select_card = create_card(ic705_select_modal, false);
        lv_obj_set_size(ic705_select_card, LV_PCT(80), LV_PCT(80));

    #if HAMVIEW_HAS_BT_CLASSIC
        hamview_icom_connect(NULL);
    #endif
        lv_obj_center(ic705_select_card);
        lv_obj_set_style_pad_all(ic705_select_card, 18, 0);
        lv_obj_set_style_pad_row(ic705_select_card, 10, 0);
        lv_obj_set_style_shadow_width(ic705_select_card, 20, 0);
        lv_obj_set_style_shadow_opa(ic705_select_card, LV_OPA_40, 0);
        lv_obj_set_flex_flow(ic705_select_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ic705_select_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        ic705_select_title = lv_label_create(ic705_select_card);
        lv_label_set_text(ic705_select_title, "Select BT Device");
        set_label_accent_primary(ic705_select_title);
        lv_obj_set_style_text_font(ic705_select_title, &lv_font_montserrat_20, 0);

        ic705_select_hint = lv_label_create(ic705_select_card);
        lv_label_set_text(ic705_select_hint, "Scanning for devices...");
        set_label_muted(ic705_select_hint);
        lv_obj_set_style_text_font(ic705_select_hint, &lv_font_montserrat_14, 0);

        ic705_select_list = lv_list_create(ic705_select_card);
        lv_obj_set_width(ic705_select_list, LV_PCT(100));
        lv_obj_set_flex_grow(ic705_select_list, 1);
        lv_obj_set_style_bg_color(ic705_select_list, theme_surface_alt(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ic705_select_list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(ic705_select_list, theme_border(), LV_PART_MAIN);
        lv_obj_set_style_border_width(ic705_select_list, 1, LV_PART_MAIN);
        lv_obj_set_style_text_color(ic705_select_list, theme_primary_text(), LV_PART_ITEMS);

        lv_obj_t *btn_row = lv_obj_create(ic705_select_card);
        lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_row, 0, 0);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        ic705_rescan_btn = lv_btn_create(btn_row);
        lv_obj_set_flex_grow(ic705_rescan_btn, 1);
        lv_obj_add_event_cb(ic705_rescan_btn, ic705_rescan_btn_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *rescan_label = lv_label_create(ic705_rescan_btn);
        lv_label_set_text(rescan_label, "Rescan");
        lv_obj_center(rescan_label);
        style_primary_button(ic705_rescan_btn);

        ic705_close_btn = lv_btn_create(btn_row);
        lv_obj_set_flex_grow(ic705_close_btn, 1);
        lv_obj_add_event_cb(ic705_close_btn, ic705_close_btn_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *close_label = lv_label_create(ic705_close_btn);
        lv_label_set_text(close_label, "Close");
        lv_obj_center(close_label);
        style_primary_button(ic705_close_btn);
    }

    rf_ble_list_dirty = true;
    ic705_select_refresh();
    lv_obj_clear_flag(ic705_select_modal, LV_OBJ_FLAG_HIDDEN);
}

static void ic705_select_close(void)
{
    if (!ic705_select_modal) {
        return;
    }
    lv_obj_add_flag(ic705_select_modal, LV_OBJ_FLAG_HIDDEN);
}

static void ic705_select_refresh(void)
{
    if (!ic705_select_modal || !ic705_select_list || lv_obj_has_flag(ic705_select_modal, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_obj_clean(ic705_select_list);

#if HAMVIEW_HAS_BT_CLASSIC
    lv_label_set_text(ic705_select_hint,
                      "On IC-705: Bluetooth > Pair/Connect > Search Data Device.\n"
                      "Select 'HAMVIEW ICOM METER' and confirm the passkey.");
    lv_obj_t *note = lv_list_add_text(ic705_select_list, "Waiting for IC-705 pairing...");
    (void)note;
#else
    if (rf_ble_device_count == 0) {
        lv_label_set_text(ic705_select_hint, "No devices yet. Keep scanning...");
        return;
    }

    lv_label_set_text(ic705_select_hint, "Tap a device to connect (BLE)");
    for (size_t i = 0; i < rf_ble_device_count; ++i) {
        char line[96];
        if (rf_ble_devices[i].name[0]) {
            snprintf(line, sizeof(line), "%s  [%s]  %d dBm",
                     rf_ble_devices[i].name,
                     rf_ble_devices[i].addr,
                     rf_ble_devices[i].rssi);
        } else {
            snprintf(line, sizeof(line), "%s  %d dBm",
                     rf_ble_devices[i].addr,
                     rf_ble_devices[i].rssi);
        }
        lv_obj_t *btn = lv_list_add_btn(ic705_select_list, LV_SYMBOL_BLUETOOTH, line);
        lv_obj_add_event_cb(btn, ic705_select_item_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
#endif
}

static void ic705_select_item_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
#if HAMVIEW_HAS_BT_CLASSIC
    (void)index;
    return;
#else
    if (index >= rf_ble_device_count) {
        return;
    }

    hamview_icom_set_device(rf_ble_devices[index].name, rf_ble_devices[index].addr);
    hamview_event_log_append("ic705", "Connecting to %s (%s)",
                             rf_ble_devices[index].name[0] ? rf_ble_devices[index].name : "unknown",
                             rf_ble_devices[index].addr);
    if (!hamview_icom_connect(rf_ble_devices[index].addr)) {
        hamview_event_log_append("ic705", "BT connect failed");
    }
#endif
    update_ic705_panel();
    ic705_select_close();
}

static void ic705_rescan_btn_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
 #if HAMVIEW_HAS_BT_CLASSIC
    rf_bt_start_scan();
    rf_bt_list_dirty = true;
#else
    rf_ble_start_scan();
    rf_ble_list_dirty = true;
#endif
    ic705_select_refresh();
}

static void ic705_close_btn_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    ic705_select_close();
}

#if HAMVIEW_HAS_BLE
static void rf_ble_extract_name(const uint8_t *adv_data, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!adv_data) {
        return;
    }
    uint8_t len = 0;
    const uint8_t *name = esp_ble_resolve_adv_data(adv_data, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
    if (!name || len == 0) {
        name = esp_ble_resolve_adv_data(adv_data, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
    }
    if (!name || len == 0) {
        return;
    }
    size_t copy_len = len;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, name, copy_len);
    out[copy_len] = '\0';
}

static void rf_ble_add_device(const esp_bd_addr_t addr, int rssi, const char *name)
{
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    for (size_t i = 0; i < rf_ble_device_count; ++i) {
        if (strcmp(rf_ble_devices[i].addr, addr_str) == 0) {
            rf_ble_devices[i].rssi = rssi;
            if (name && name[0]) {
                strlcpy(rf_ble_devices[i].name, name, sizeof(rf_ble_devices[i].name));
            }
            rf_ble_list_dirty = true;
            return;
        }
    }

    if (rf_ble_device_count < ARRAY_SIZE(rf_ble_devices)) {
        strlcpy(rf_ble_devices[rf_ble_device_count].addr, addr_str, sizeof(rf_ble_devices[0].addr));
        if (name && name[0]) {
            strlcpy(rf_ble_devices[rf_ble_device_count].name, name, sizeof(rf_ble_devices[0].name));
        } else {
            rf_ble_devices[rf_ble_device_count].name[0] = '\0';
        }
        rf_ble_devices[rf_ble_device_count].rssi = rssi;
        rf_ble_device_count++;
        rf_ble_list_dirty = true;
        return;
    }

    size_t weakest = 0;
    for (size_t i = 1; i < rf_ble_device_count; ++i) {
        if (rf_ble_devices[i].rssi < rf_ble_devices[weakest].rssi) {
            weakest = i;
        }
    }
    if (rssi > rf_ble_devices[weakest].rssi) {
        strlcpy(rf_ble_devices[weakest].addr, addr_str, sizeof(rf_ble_devices[0].addr));
        if (name && name[0]) {
            strlcpy(rf_ble_devices[weakest].name, name, sizeof(rf_ble_devices[0].name));
        } else {
            rf_ble_devices[weakest].name[0] = '\0';
        }
        rf_ble_devices[weakest].rssi = rssi;
        rf_ble_list_dirty = true;
    }
}

static void rf_ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (!param) {
        return;
    }
    switch (event) {
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
                char name_buf[32];
                rf_ble_extract_name(param->scan_rst.ble_adv, name_buf, sizeof(name_buf));
                rf_ble_add_device(param->scan_rst.bda, param->scan_rst.rssi, name_buf);
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: {
            rf_ble_scanning = false;
            if (rf_ble_enabled) {
                esp_ble_gap_start_scanning(5);
                rf_ble_scanning = true;
            }
            break;
        }
        default:
            break;
    }
}

static void rf_ble_start_scan(void)
{
    if (rf_ble_scanning) {
        return;
    }
    if (!rf_ble_initialized) {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
            esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if !(defined(CONFIG_BT_CLASSIC_ENABLED) && CONFIG_BT_CLASSIC_ENABLED)
            esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
#endif
            if (esp_bt_controller_init(&cfg) != ESP_OK) {
                return;
            }
        }

        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
#if defined(CONFIG_BT_CLASSIC_ENABLED) && CONFIG_BT_CLASSIC_ENABLED
            if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
#else
            if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
#endif
                return;
            }
        }

        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            if (esp_bluedroid_init() != ESP_OK) {
                return;
            }
        }
        if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
            if (esp_bluedroid_enable() != ESP_OK) {
                return;
            }
        }
        esp_ble_gap_register_callback(rf_ble_gap_cb);
        rf_ble_initialized = true;
    }

    memset(rf_ble_devices, 0, sizeof(rf_ble_devices));
    rf_ble_device_count = 0;
    rf_ble_list_dirty = true;

    static esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_PASSIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
    };
    esp_ble_gap_set_scan_params(&scan_params);
    esp_ble_gap_start_scanning(5);
    rf_ble_scanning = true;
}

static void rf_ble_stop_scan(void)
{
    if (rf_ble_scanning) {
        esp_ble_gap_stop_scanning();
    }
    rf_ble_scanning = false;
    rf_ble_device_count = 0;
    rf_ble_list_dirty = true;
}
#else
static void rf_ble_start_scan(void)
{
    (void)0;
}

static void rf_ble_stop_scan(void)
{
    (void)0;
}
#endif

#if HAMVIEW_HAS_BT_CLASSIC
static void rf_bt_add_device(const uint8_t *addr, int rssi, const char *name)
{
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    for (size_t i = 0; i < rf_bt_device_count; ++i) {
        if (strcmp(rf_bt_devices[i].addr, addr_str) == 0) {
            rf_bt_devices[i].rssi = rssi;
            if (name && name[0]) {
                strlcpy(rf_bt_devices[i].name, name, sizeof(rf_bt_devices[i].name));
            }
            rf_bt_list_dirty = true;
            return;
        }
    }

    if (rf_bt_device_count < ARRAY_SIZE(rf_bt_devices)) {
        strlcpy(rf_bt_devices[rf_bt_device_count].addr, addr_str, sizeof(rf_bt_devices[0].addr));
        if (name && name[0]) {
            strlcpy(rf_bt_devices[rf_bt_device_count].name, name, sizeof(rf_bt_devices[0].name));
        } else {
            rf_bt_devices[rf_bt_device_count].name[0] = '\0';
        }
        rf_bt_devices[rf_bt_device_count].rssi = rssi;
        rf_bt_device_count++;
        rf_bt_list_dirty = true;
        return;
    }

    size_t weakest = 0;
    for (size_t i = 1; i < rf_bt_device_count; ++i) {
        if (rf_bt_devices[i].rssi < rf_bt_devices[weakest].rssi) {
            weakest = i;
        }
    }
    if (rssi > rf_bt_devices[weakest].rssi) {
        strlcpy(rf_bt_devices[weakest].addr, addr_str, sizeof(rf_bt_devices[0].addr));
        if (name && name[0]) {
            strlcpy(rf_bt_devices[weakest].name, name, sizeof(rf_bt_devices[0].name));
        } else {
            rf_bt_devices[weakest].name[0] = '\0';
        }
        rf_bt_devices[weakest].rssi = rssi;
        rf_bt_list_dirty = true;
    }
}

static void rf_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            char name_buf[32] = {0};
            if (param->disc_res.num_prop > 0) {
                for (int i = 0; i < param->disc_res.num_prop; ++i) {
                    if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR) {
                        uint8_t len = 0;
                        const uint8_t *name = esp_bt_gap_resolve_eir_data((uint8_t *)param->disc_res.prop[i].val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
                        if (!name || len == 0) {
                            name = esp_bt_gap_resolve_eir_data((uint8_t *)param->disc_res.prop[i].val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
                        }
                        if (name && len > 0) {
                            size_t copy_len = len >= sizeof(name_buf) ? sizeof(name_buf) - 1 : len;
                            memcpy(name_buf, name, copy_len);
                            name_buf[copy_len] = '\0';
                        }
                    }
                }
            }
            rf_bt_add_device(param->disc_res.bda, param->disc_res.rssi, name_buf);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                rf_bt_scanning = false;
            }
            break;
        }
        default:
            break;
    }
}

static void rf_bt_start_scan(void)
{
    if (rf_bt_scanning) {
        return;
    }

    if (!rf_bt_initialized) {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
            esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            if (esp_bt_controller_init(&cfg) != ESP_OK) {
                return;
            }
        }

        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
                return;
            }
        }

        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            if (esp_bluedroid_init() != ESP_OK) {
                return;
            }
        }
        if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
            if (esp_bluedroid_enable() != ESP_OK) {
                return;
            }
        }

        esp_bt_gap_register_callback(rf_bt_gap_cb);
        esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
        rf_bt_initialized = true;
    }

    memset(rf_bt_devices, 0, sizeof(rf_bt_devices));
    rf_bt_device_count = 0;
    rf_bt_list_dirty = true;

    hamview_icom_connect(NULL);
    rf_bt_scanning = true;
}

static void rf_bt_stop_scan(void)
{
    if (rf_bt_scanning) {
        esp_bt_gap_cancel_discovery();
    }
    rf_bt_scanning = false;
    rf_bt_device_count = 0;
    rf_bt_list_dirty = true;
    esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_NONE);
}
#endif

static void rf_lora_on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    (void)payload;
    (void)size;
    rf_lora_rx_count++;
    rf_lora_last_rssi = rssi;
    rf_lora_last_snr = snr;
    Radio.Rx(0);
}

static void rf_lora_on_rx_timeout(void)
{
    Radio.Rx(0);
}

static void rf_lora_on_rx_error(void)
{
    Radio.Rx(0);
}

static void rf_lora_start_scan(void)
{
    if (!rf_lora_initialized) {
        static RadioEvents_t events = {
            .RxDone = rf_lora_on_rx_done,
            .RxTimeout = rf_lora_on_rx_timeout,
            .RxError = rf_lora_on_rx_error,
        };
        Radio.Init(&events);
        Radio.SetChannel(915000000);
        Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, false, 0, false, true);
        rf_lora_initialized = true;
    }
    rf_lora_rx_count = 0;
    rf_lora_last_rssi = 0;
    rf_lora_last_snr = 0;
    Radio.Rx(0);
}

static void rf_lora_stop_scan(void)
{
    if (rf_lora_initialized) {
        Radio.Sleep();
    }
}

static void set_message(const char *fmt, ...)
{
    if (!message_label) return;
    char buffer[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    lv_label_set_text(message_label, buffer);
}

static void settings_save_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    hamview_settings_t settings;
    hamview_settings_get(&settings);

    const char *username = lv_textarea_get_text(username_field);
    const char *password = lv_textarea_get_text(password_field);
    const char *callsign = lv_textarea_get_text(callsign_field);
    const char *band = lv_textarea_get_text(band_field);
    const char *weather_zip = lv_textarea_get_text(weather_zip_field);
    const char *icom_ip = icom_wifi_ip_field ? lv_textarea_get_text(icom_wifi_ip_field) : "";
    const char *icom_user = icom_wifi_user_field ? lv_textarea_get_text(icom_wifi_user_field) : "";
    const char *icom_pass = icom_wifi_pass_field ? lv_textarea_get_text(icom_wifi_pass_field) : "";
    const char *icom_port_text = icom_wifi_port_field ? lv_textarea_get_text(icom_wifi_port_field) : "";
    const char *alert_callsigns = alert_callsigns_field ? lv_textarea_get_text(alert_callsigns_field) : "";
    const char *alert_states = alert_states_field ? lv_textarea_get_text(alert_states_field) : "";
    const char *alert_countries = alert_countries_field ? lv_textarea_get_text(alert_countries_field) : "";

    strlcpy(settings.username, username ? username : "", sizeof(settings.username));
    strlcpy(settings.password, password ? password : "", sizeof(settings.password));
    strlcpy(settings.filter_callsign, callsign ? callsign : "", sizeof(settings.filter_callsign));
    strlcpy(settings.filter_band, band ? band : "", sizeof(settings.filter_band));
    settings.alert_sound_enabled = false;
    if (brightness_dropdown) {
        uint32_t idx = lv_dropdown_get_selected(brightness_dropdown);
        settings.screen_brightness_percent = dropdown_value_for_index(idx, SCREEN_BRIGHTNESS_OPTIONS,
                                                                      ARRAY_SIZE(SCREEN_BRIGHTNESS_OPTIONS),
                                                                      SCREEN_BRIGHTNESS_DEFAULT_INDEX);
    }
    strlcpy(settings.weather_zip, weather_zip ? weather_zip : "", sizeof(settings.weather_zip));
    strlcpy(settings.icom_wifi_ip, icom_ip ? icom_ip : "", sizeof(settings.icom_wifi_ip));
    strlcpy(settings.icom_wifi_user, icom_user ? icom_user : "", sizeof(settings.icom_wifi_user));
    strlcpy(settings.icom_wifi_pass, icom_pass ? icom_pass : "", sizeof(settings.icom_wifi_pass));
    settings.icom_wifi_port = (uint16_t)atoi(icom_port_text ? icom_port_text : "0");
    settings.icom_wifi_enabled = icom_wifi_switch ? lv_obj_has_state(icom_wifi_switch, LV_STATE_CHECKED) : false;
    strlcpy(settings.alert_callsigns, alert_callsigns, sizeof(settings.alert_callsigns));
    strlcpy(settings.alert_states, alert_states, sizeof(settings.alert_states));
    strlcpy(settings.alert_countries, alert_countries, sizeof(settings.alert_countries));
    if (spot_ttl_dropdown) {
        uint32_t idx = lv_dropdown_get_selected(spot_ttl_dropdown);
        settings.spot_ttl_minutes = dropdown_value_for_index(idx, SPOT_TTL_MINUTES_OPTIONS,
                                                             ARRAY_SIZE(SPOT_TTL_MINUTES_OPTIONS),
                                                             SPOT_TTL_DEFAULT_INDEX);
    }
    if (screen_timeout_dropdown) {
        uint32_t idx = lv_dropdown_get_selected(screen_timeout_dropdown);
        settings.screen_timeout_minutes = dropdown_value_for_index(idx, SCREEN_TIMEOUT_MINUTES_OPTIONS,
                                                                   ARRAY_SIZE(SCREEN_TIMEOUT_MINUTES_OPTIONS),
                                                                   SCREEN_TIMEOUT_DEFAULT_INDEX);
    }
    if (spot_age_dropdown) {
        uint32_t idx = lv_dropdown_get_selected(spot_age_dropdown);
        settings.spot_age_filter_minutes = dropdown_value_for_index(idx, SPOT_AGE_FILTER_OPTIONS,
                                                                     ARRAY_SIZE(SPOT_AGE_FILTER_OPTIONS),
                                                                     SPOT_AGE_FILTER_DEFAULT_INDEX);
    }

    if (hamview_settings_save(&settings) == ESP_OK) {
        spot_age_filter_minutes = settings.spot_age_filter_minutes;
        hamview_backend_on_settings_updated();
        hamview_weather_on_settings_updated();
        hamview_screen_configure(settings.screen_timeout_minutes);
        hamview_screen_set_brightness(settings.screen_brightness_percent);
        hamview_icom_set_civ_wifi(settings.icom_wifi_enabled, settings.icom_wifi_ip, settings.icom_wifi_port,
                      settings.icom_wifi_user, settings.icom_wifi_pass);
        set_message("Settings saved");
    } else {
        set_message("Failed to save settings");
    }
    close_settings_modal();
}

static void settings_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    close_settings_modal();
}

static void open_settings_modal(void)
{
    lv_port_sem_take();
    hamview_settings_t settings;
    hamview_settings_get(&settings);

    if (!settings_modal) {
        settings_modal = lv_obj_create(settings_tab ? settings_tab : dashboard_screen);
        lv_obj_set_size(settings_modal, LV_PCT(100), LV_PCT(100));
        lv_obj_align(settings_modal, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(settings_modal, lv_color_hex(0x101010), 0);
        lv_obj_set_style_bg_opa(settings_modal, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(settings_modal, 12, 0);
        lv_obj_set_style_pad_row(settings_modal, 10, 0);
        lv_obj_set_style_pad_column(settings_modal, 8, 0);
        lv_obj_set_style_border_width(settings_modal, 0, 0);
        lv_obj_set_style_shadow_width(settings_modal, 0, 0);
        lv_obj_set_style_shadow_opa(settings_modal, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(settings_modal, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(settings_modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *title = lv_label_create(settings_modal);
        lv_label_set_text(title, "HamAlert Settings");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
        set_label_accent_secondary(title);

        username_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(username_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(username_field, "HamAlert Username");
        lv_textarea_set_one_line(username_field, true);
        lv_textarea_set_max_length(username_field, sizeof(((hamview_settings_t *)0)->username) - 1);
        lv_obj_add_event_cb(username_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(username_field);

        password_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(password_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(password_field, "HamAlert Password");
        lv_textarea_set_one_line(password_field, true);
        lv_textarea_set_max_length(password_field, sizeof(((hamview_settings_t *)0)->password) - 1);
        lv_textarea_set_password_mode(password_field, true);
        lv_obj_add_event_cb(password_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(password_field);

        callsign_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(callsign_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(callsign_field, "Filter Callsign (optional)");
        lv_textarea_set_one_line(callsign_field, true);
        lv_textarea_set_max_length(callsign_field, sizeof(((hamview_settings_t *)0)->filter_callsign) - 1);
        lv_obj_add_event_cb(callsign_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(callsign_field);

        band_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(band_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(band_field, "Filter Band (e.g. 20m)");
        lv_textarea_set_one_line(band_field, true);
        lv_textarea_set_max_length(band_field, sizeof(((hamview_settings_t *)0)->filter_band) - 1);
        lv_obj_add_event_cb(band_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(band_field);

        weather_zip_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(weather_zip_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(weather_zip_field, "Weather ZIP (e.g. 94016)");
        lv_textarea_set_one_line(weather_zip_field, true);
        lv_textarea_set_max_length(weather_zip_field, sizeof(((hamview_settings_t *)0)->weather_zip) - 1);
        lv_obj_add_event_cb(weather_zip_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(weather_zip_field);

        lv_obj_t *icom_label = lv_label_create(settings_modal);
        lv_label_set_text(icom_label, "IC-705 WiFi CI-V");
        set_label_accent_primary(icom_label);

        lv_obj_t *icom_row = lv_obj_create(settings_modal);
        lv_obj_set_size(icom_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(icom_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(icom_row, 0, 0);
        lv_obj_set_flex_flow(icom_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(icom_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *icom_enable_label = lv_label_create(icom_row);
        lv_label_set_text(icom_enable_label, "Enable WiFi CI-V");
        lv_obj_set_style_text_font(icom_enable_label, &lv_font_montserrat_18, 0);
        set_label_primary(icom_enable_label);

        icom_wifi_switch = lv_switch_create(icom_row);

        icom_wifi_ip_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(icom_wifi_ip_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(icom_wifi_ip_field, "IC-705 IP (e.g. 192.168.1.120)");
        lv_textarea_set_one_line(icom_wifi_ip_field, true);
        lv_textarea_set_max_length(icom_wifi_ip_field, sizeof(((hamview_settings_t *)0)->icom_wifi_ip) - 1);
        lv_obj_add_event_cb(icom_wifi_ip_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(icom_wifi_ip_field);

        icom_wifi_user_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(icom_wifi_user_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(icom_wifi_user_field, "IC-705 Network Username");
        lv_textarea_set_one_line(icom_wifi_user_field, true);
        lv_textarea_set_max_length(icom_wifi_user_field, sizeof(((hamview_settings_t *)0)->icom_wifi_user) - 1);
        lv_obj_add_event_cb(icom_wifi_user_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(icom_wifi_user_field);

        icom_wifi_pass_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(icom_wifi_pass_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(icom_wifi_pass_field, "IC-705 Network Password");
        lv_textarea_set_one_line(icom_wifi_pass_field, true);
        lv_textarea_set_max_length(icom_wifi_pass_field, sizeof(((hamview_settings_t *)0)->icom_wifi_pass) - 1);
        lv_textarea_set_password_mode(icom_wifi_pass_field, true);
        lv_obj_add_event_cb(icom_wifi_pass_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(icom_wifi_pass_field);

        icom_wifi_port_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(icom_wifi_port_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(icom_wifi_port_field, "IC-705 CI-V UDP port (default 50001)");
        lv_textarea_set_one_line(icom_wifi_port_field, true);
        lv_textarea_set_max_length(icom_wifi_port_field, 6);
        lv_obj_add_event_cb(icom_wifi_port_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(icom_wifi_port_field);

        settings_wifi_scan_btn = lv_btn_create(settings_modal);
        lv_obj_set_width(settings_wifi_scan_btn, LV_PCT(100));
        lv_obj_add_event_cb(settings_wifi_scan_btn, settings_wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *scan_label = lv_label_create(settings_wifi_scan_btn);
        lv_label_set_text(scan_label, "Change Wi-Fi Network");
        lv_obj_center(scan_label);
        style_primary_button(settings_wifi_scan_btn);

        lv_obj_t *ttl_label = lv_label_create(settings_modal);
        lv_label_set_text(ttl_label, "Spot Expiration");
        set_label_accent_primary(ttl_label);

        spot_ttl_dropdown = lv_dropdown_create(settings_modal);
        lv_obj_set_width(spot_ttl_dropdown, LV_PCT(100));
        lv_dropdown_set_options_static(spot_ttl_dropdown, SPOT_TTL_DROPDOWN_OPTIONS);
        style_dropdown(spot_ttl_dropdown);

        lv_obj_t *screen_label = lv_label_create(settings_modal);
        lv_label_set_text(screen_label, "Screen Timeout");
        set_label_accent_primary(screen_label);

        screen_timeout_dropdown = lv_dropdown_create(settings_modal);
        lv_obj_set_width(screen_timeout_dropdown, LV_PCT(100));
        lv_dropdown_set_options_static(screen_timeout_dropdown, SCREEN_TIMEOUT_DROPDOWN_OPTIONS);
        style_dropdown(screen_timeout_dropdown);

        lv_obj_t *age_label = lv_label_create(settings_modal);
        lv_label_set_text(age_label, "Spot Age Filter");
        set_label_accent_primary(age_label);

        spot_age_dropdown = lv_dropdown_create(settings_modal);
        lv_obj_set_width(spot_age_dropdown, LV_PCT(100));
        lv_dropdown_set_options_static(spot_age_dropdown, SPOT_AGE_FILTER_DROPDOWN_OPTIONS);
        style_dropdown(spot_age_dropdown);

        lv_obj_t *display_label = lv_label_create(settings_modal);
        lv_label_set_text(display_label, "Display");
        set_label_accent_primary(display_label);

        lv_obj_t *time_row = lv_obj_create(settings_modal);
        lv_obj_set_size(time_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(time_row, 0, 0);
        lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *time_label = lv_label_create(time_row);
        lv_label_set_text(time_label, "24-hour time");
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_18, 0);
        set_label_primary(time_label);

        time_format_switch = lv_switch_create(time_row);
        lv_obj_add_event_cb(time_format_switch, settings_time_format_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t *temp_row = lv_obj_create(settings_modal);
        lv_obj_set_size(temp_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(temp_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(temp_row, 0, 0);
        lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(temp_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *temp_label = lv_label_create(temp_row);
        lv_label_set_text(temp_label, "Show Celsius");
        lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_18, 0);
        set_label_primary(temp_label);

        temp_unit_switch = lv_switch_create(temp_row);
        lv_obj_add_event_cb(temp_unit_switch, settings_temp_unit_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t *brightness_row = lv_obj_create(settings_modal);
        lv_obj_set_size(brightness_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(brightness_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(brightness_row, 0, 0);
        lv_obj_set_flex_flow(brightness_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(brightness_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *brightness_label = lv_label_create(brightness_row);
        lv_label_set_text(brightness_label, "Screen brightness");
        lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_18, 0);
        set_label_primary(brightness_label);

        brightness_dropdown = lv_dropdown_create(brightness_row);
        lv_obj_set_width(brightness_dropdown, LV_PCT(45));
        lv_dropdown_set_options_static(brightness_dropdown, SCREEN_BRIGHTNESS_DROPDOWN_OPTIONS);
        style_dropdown(brightness_dropdown);

        lv_obj_t *alert_label = lv_label_create(settings_modal);
        lv_label_set_text(alert_label, "Alert Notifications");
        set_label_accent_primary(alert_label);

        lv_obj_t *sound_row = lv_obj_create(settings_modal);
        lv_obj_set_size(sound_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(sound_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sound_row, 0, 0);
        lv_obj_set_flex_flow(sound_row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sound_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *sound_label = lv_label_create(sound_row);
        lv_label_set_text(sound_label, "Alert tone");
        lv_obj_set_style_text_font(sound_label, &lv_font_montserrat_18, 0);
        set_label_primary(sound_label);

        lv_obj_t *sound_note = lv_label_create(sound_row);
        lv_label_set_text(sound_note, "Disabled on this hardware (causes display instability)");
        set_label_warning(sound_note);
        lv_obj_set_style_text_font(sound_note, &lv_font_montserrat_16, 0);

        lv_obj_t *alert_match_label = lv_label_create(settings_modal);
        lv_label_set_text(alert_match_label, "Alert Match Lists");
        set_label_accent_primary(alert_match_label);

        alert_callsigns_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(alert_callsigns_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(alert_callsigns_field, "Alert Callsigns (comma separated)");
        lv_textarea_set_one_line(alert_callsigns_field, false);
        lv_textarea_set_max_length(alert_callsigns_field, sizeof(((hamview_settings_t *)0)->alert_callsigns) - 1);
        lv_obj_add_event_cb(alert_callsigns_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(alert_callsigns_field);

        alert_states_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(alert_states_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(alert_states_field, "Alert States/Regions (comma separated)");
        lv_textarea_set_one_line(alert_states_field, false);
        lv_textarea_set_max_length(alert_states_field, sizeof(((hamview_settings_t *)0)->alert_states) - 1);
        lv_obj_add_event_cb(alert_states_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(alert_states_field);

        alert_countries_field = lv_textarea_create(settings_modal);
        lv_obj_set_width(alert_countries_field, LV_PCT(100));
        lv_textarea_set_placeholder_text(alert_countries_field, "Alert Countries (comma separated)");
        lv_textarea_set_one_line(alert_countries_field, false);
        lv_textarea_set_max_length(alert_countries_field, sizeof(((hamview_settings_t *)0)->alert_countries) - 1);
        lv_obj_add_event_cb(alert_countries_field, settings_textarea_event_cb, LV_EVENT_ALL, NULL);
        style_textarea(alert_countries_field);

        lv_obj_t *btn_row = lv_obj_create(settings_modal);
        lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_row, 0, 0);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        settings_save_btn = lv_btn_create(btn_row);
        lv_obj_set_flex_grow(settings_save_btn, 1);
        lv_obj_add_event_cb(settings_save_btn, settings_save_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *save_label = lv_label_create(settings_save_btn);
        lv_label_set_text(save_label, "Save");
        lv_obj_center(save_label);
        style_primary_button(settings_save_btn);

        settings_cancel_btn = lv_btn_create(btn_row);
        lv_obj_set_flex_grow(settings_cancel_btn, 1);
        lv_obj_add_event_cb(settings_cancel_btn, settings_cancel_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *cancel_label = lv_label_create(settings_cancel_btn);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_center(cancel_label);
        style_primary_button(settings_cancel_btn);

        settings_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_width(settings_keyboard, LV_PCT(100));
        lv_keyboard_set_textarea(settings_keyboard, NULL);
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(settings_keyboard, settings_keyboard_event_cb, LV_EVENT_ALL, NULL);
    }

    lv_textarea_set_text(username_field, settings.username);
    lv_textarea_set_text(password_field, settings.password);
    lv_textarea_set_text(callsign_field, settings.filter_callsign);
    lv_textarea_set_text(band_field, settings.filter_band);
    lv_textarea_set_text(weather_zip_field, settings.weather_zip);
    if (icom_wifi_ip_field) {
        lv_textarea_set_text(icom_wifi_ip_field, settings.icom_wifi_ip);
    }
    if (icom_wifi_user_field) {
        lv_textarea_set_text(icom_wifi_user_field, settings.icom_wifi_user);
    }
    if (icom_wifi_pass_field) {
        lv_textarea_set_text(icom_wifi_pass_field, settings.icom_wifi_pass);
    }
    if (icom_wifi_port_field) {
        char port_buf[8];
        snprintf(port_buf, sizeof(port_buf), "%u", (unsigned)settings.icom_wifi_port);
        lv_textarea_set_text(icom_wifi_port_field, port_buf);
    }
    if (alert_callsigns_field) {
        lv_textarea_set_text(alert_callsigns_field, settings.alert_callsigns);
    }
    if (alert_states_field) {
        lv_textarea_set_text(alert_states_field, settings.alert_states);
    }
    if (alert_countries_field) {
        lv_textarea_set_text(alert_countries_field, settings.alert_countries);
    }

    if (spot_ttl_dropdown) {
        uint32_t idx = dropdown_index_for_value(settings.spot_ttl_minutes, SPOT_TTL_MINUTES_OPTIONS,
                                                ARRAY_SIZE(SPOT_TTL_MINUTES_OPTIONS), SPOT_TTL_DEFAULT_INDEX);
        lv_dropdown_set_selected(spot_ttl_dropdown, idx);
    }
    if (screen_timeout_dropdown) {
        uint32_t idx = dropdown_index_for_value(settings.screen_timeout_minutes, SCREEN_TIMEOUT_MINUTES_OPTIONS,
                                                ARRAY_SIZE(SCREEN_TIMEOUT_MINUTES_OPTIONS), SCREEN_TIMEOUT_DEFAULT_INDEX);
        lv_dropdown_set_selected(screen_timeout_dropdown, idx);
    }

    if (spot_age_dropdown) {
        uint32_t idx = dropdown_index_for_value(settings.spot_age_filter_minutes, SPOT_AGE_FILTER_OPTIONS,
                                                ARRAY_SIZE(SPOT_AGE_FILTER_OPTIONS), SPOT_AGE_FILTER_DEFAULT_INDEX);
        lv_dropdown_set_selected(spot_age_dropdown, idx);
    }

    if (brightness_dropdown) {
        uint32_t idx = dropdown_index_for_value(settings.screen_brightness_percent, SCREEN_BRIGHTNESS_OPTIONS,
                                                ARRAY_SIZE(SCREEN_BRIGHTNESS_OPTIONS), SCREEN_BRIGHTNESS_DEFAULT_INDEX);
        lv_dropdown_set_selected(brightness_dropdown, idx);
    }

    if (time_format_switch) {
        if (time_use_24h) {
            lv_obj_add_state(time_format_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(time_format_switch, LV_STATE_CHECKED);
        }
    }

    if (temp_unit_switch) {
        if (weather_show_celsius) {
            lv_obj_add_state(temp_unit_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(temp_unit_switch, LV_STATE_CHECKED);
        }
    }

    if (icom_wifi_switch) {
        if (settings.icom_wifi_enabled) {
            lv_obj_add_state(icom_wifi_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(icom_wifi_switch, LV_STATE_CHECKED);
        }
    }


    if (settings_modal && settings_tab && lv_obj_get_parent(settings_modal) != settings_tab) {
        lv_obj_set_parent(settings_modal, settings_tab);
        lv_obj_set_size(settings_modal, LV_PCT(100), LV_PCT(100));
        lv_obj_align(settings_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    lv_obj_clear_flag(settings_modal, LV_OBJ_FLAG_HIDDEN);
    if (tabview) {
        lv_tabview_set_act(tabview, 4, LV_ANIM_ON);
    }
    lv_port_sem_give();
}

static void close_settings_modal(void)
{
    lv_port_sem_take();
    if (settings_keyboard) {
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(settings_keyboard, NULL);
    }
    if (tabview) {
        lv_tabview_set_act(tabview, 0, LV_ANIM_ON);
    }
    lv_port_sem_give();
}

static void settings_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    open_settings_modal();
}

static void settings_wifi_scan_btn_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    close_settings_modal();
    hamview_wifi_ui_show();
}

static void settings_keyboard_event_cb(lv_event_t *e)
{
    if (!settings_keyboard) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(settings_keyboard, NULL);
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (!settings_keyboard) return;

    switch (code) {
    case LV_EVENT_FOCUSED:
    case LV_EVENT_CLICKED:
        lv_obj_clear_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(settings_keyboard, ta);
        break;
    case LV_EVENT_DEFOCUSED:
        if (lv_keyboard_get_textarea(settings_keyboard) == ta) {
            lv_keyboard_set_textarea(settings_keyboard, NULL);
        }
        break;
    default:
        break;
    }
}

static void settings_time_format_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *sw = lv_event_get_target(e);
    time_use_24h = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_clock_labels();
}

static void settings_temp_unit_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *sw = lv_event_get_target(e);
    weather_show_celsius = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_weather_panel();
}

static void build_dashboard_ui(void)
{
    if (dashboard_screen) return;
    dashboard_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(dashboard_screen, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(dashboard_screen, LV_OPA_COVER, 0);

    tabview = lv_tabview_create(dashboard_screen, LV_DIR_TOP, 0);
    lv_obj_set_size(tabview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tabview, 0, 0);
    lv_obj_t *main_tab = lv_tabview_add_tab(tabview, "Spots");
    lv_obj_t *weather_tab = lv_tabview_add_tab(tabview, "Weather");
    radar_tab = lv_tabview_add_tab(tabview, "Radar");
    ic705_tab = lv_tabview_add_tab(tabview, "IC-705");
    settings_tab = lv_tabview_add_tab(tabview, "Settings");

    lv_obj_set_style_bg_opa(main_tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(weather_tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(radar_tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(ic705_tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(settings_tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(main_tab, 0, 0);
    lv_obj_set_style_pad_all(weather_tab, 0, 0);
    lv_obj_set_style_pad_all(radar_tab, 0, 0);
    lv_obj_set_style_pad_all(ic705_tab, 0, 0);
    lv_obj_set_style_pad_all(settings_tab, 0, 0);

    lv_obj_t *tab_bar = lv_obj_get_child(tabview, 0);
    if (tab_bar) {
        lv_obj_add_flag(tab_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(tab_bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tab_bar, 0, 0);
        lv_obj_set_style_pad_all(tab_bar, 0, 0);
        lv_obj_set_height(tab_bar, 0);
    }
    header_container = NULL;
    status_row = NULL;
    clock_row = NULL;
    spots_container = NULL;
    activity_container = NULL;
    weather_container = NULL;
    log_container = NULL;
    theme_toggle_btn = NULL;
    theme_toggle_label = NULL;
    mode_filter_dropdown = NULL;
    band_filter_dropdown = NULL;
    activity_chart = NULL;
    mode_chart = NULL;
    daily_chart = NULL;
    activity_series = NULL;
    mode_series = NULL;
    daily_series = NULL;
    activity_summary_label = NULL;
    mode_summary_label = NULL;
    daily_summary_label = NULL;
    weather_location_label = NULL;
    weather_condition_label = NULL;
    weather_temp_label = NULL;
    weather_extra_label = NULL;
    weather_updated_label = NULL;
    weather_alerts_container = NULL;
    weather_warning_label = NULL;
    weather_forecast_label = NULL;
    weather_action_row = NULL;
    weather_refresh_btn = NULL;
    weather_units_btn = NULL;
    weather_mute_btn = NULL;
    weather_refresh_label = NULL;
    weather_units_label = NULL;
    weather_mute_label = NULL;
    weather_local_time_label = NULL;
    weather_utc_time_label = NULL;
    log_label = NULL;
    rf_radar_label = NULL;
    ic705_status_label = NULL;
    ic705_link_label = NULL;
    ic705_target_label = NULL;
    ic705_device_label = NULL;
    ic705_freq_label = NULL;
    ic705_mode_label = NULL;
    ic705_smeter_label = NULL;
    ic705_last_update_label = NULL;
    ic705_connect_btn = NULL;
    ic705_select_modal = NULL;
    ic705_select_card = NULL;
    ic705_select_title = NULL;
    ic705_select_hint = NULL;
    ic705_select_list = NULL;
    ic705_rescan_btn = NULL;
    ic705_close_btn = NULL;

    const lv_coord_t hor = lv_disp_get_hor_res(NULL);
    const lv_coord_t ver = lv_disp_get_ver_res(NULL);
    const lv_coord_t pad = 8;

    title_label = lv_label_create(main_tab);
    lv_label_set_text(title_label, "HamView");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, pad, pad);

    settings_btn = lv_btn_create(main_tab);
    lv_obj_set_size(settings_btn, 90, 30);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, -pad, pad);
    lv_obj_add_event_cb(settings_btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *settings_label = lv_label_create(settings_btn);
    lv_label_set_text(settings_label, "Settings");
    lv_obj_center(settings_label);

    wifi_status_label = lv_label_create(main_tab);
    lv_label_set_text(wifi_status_label, "Wi-Fi: --");
    lv_obj_set_style_text_color(wifi_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_LEFT, pad, pad + 32);

    wifi_signal_label = lv_label_create(main_tab);
    lv_label_set_text(wifi_signal_label, "Signal: --");
    lv_obj_set_style_text_color(wifi_signal_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(wifi_signal_label, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_signal_label, LV_ALIGN_TOP_LEFT, pad, pad + 52);

    ham_status_label = lv_label_create(main_tab);
    lv_label_set_text(ham_status_label, "HamAlert: --");
    lv_obj_set_style_text_color(ham_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ham_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(ham_status_label, LV_ALIGN_TOP_LEFT, pad, pad + 74);

    ham_local_time_label = lv_label_create(main_tab);
    lv_label_set_text(ham_local_time_label, "Local: --");
    lv_obj_set_style_text_color(ham_local_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ham_local_time_label, &lv_font_montserrat_16, 0);
    lv_obj_align(ham_local_time_label, LV_ALIGN_TOP_LEFT, pad, pad + 96);

    ham_utc_time_label = lv_label_create(main_tab);
    lv_label_set_text(ham_utc_time_label, "UTC: --");
    lv_obj_set_style_text_color(ham_utc_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(ham_utc_time_label, &lv_font_montserrat_16, 0);
    lv_obj_align(ham_utc_time_label, LV_ALIGN_TOP_LEFT, pad, pad + 118);

    error_label = lv_label_create(main_tab);
    lv_label_set_text(error_label, "");
    lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(error_label, hor - (pad * 2));
    lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(error_label, LV_ALIGN_TOP_LEFT, pad, pad + 140);

    table = lv_table_create(main_tab);
    lv_table_set_col_cnt(table, 5);
    lv_table_set_row_cnt(table, HAMVIEW_MAX_SPOTS + 1);
    lv_table_set_col_width(table, 0, 90);
    lv_table_set_col_width(table, 1, 70);
    lv_table_set_col_width(table, 2, 70);
    lv_table_set_col_width(table, 3, 80);
    lv_table_set_col_width(table, 4, hor - (pad * 2) - (90 + 70 + 70 + 80));
    lv_obj_set_size(table, hor - (pad * 2), ver - 210);
    lv_obj_align(table, LV_ALIGN_TOP_LEFT, pad, pad + 170);
    lv_obj_add_event_cb(table, spots_table_draw_event, LV_EVENT_DRAW_PART_END, NULL);

    const char *headers[] = {"Call", "Freq", "Mode", "Spotter", "Comment"};
    for (int col = 0; col < 5; ++col) {
        lv_table_set_cell_value(table, 0, col, headers[col]);
    }

    message_label = lv_label_create(main_tab);
    lv_label_set_text(message_label, "Waiting for HamAlert updates...");
    lv_obj_set_style_text_color(message_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(message_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(message_label, hor - (pad * 2));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, pad, -pad);

    weather_location_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_location_label, "Weather: --");
    lv_obj_set_style_text_color(weather_location_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(weather_location_label, &lv_font_montserrat_20, 0);
    lv_obj_align(weather_location_label, LV_ALIGN_TOP_LEFT, pad, pad);

    weather_refresh_btn = lv_btn_create(weather_tab);
    lv_obj_set_size(weather_refresh_btn, 90, 30);
    lv_obj_align(weather_refresh_btn, LV_ALIGN_TOP_RIGHT, -pad, pad);
    lv_obj_add_event_cb(weather_refresh_btn, weather_action_btn_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)WEATHER_ACTION_REFRESH);
    weather_refresh_label = lv_label_create(weather_refresh_btn);
    lv_label_set_text(weather_refresh_label, "Refresh");
    lv_obj_center(weather_refresh_label);

    weather_condition_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_condition_label, "Condition: --");
    lv_obj_set_style_text_color(weather_condition_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(weather_condition_label, &lv_font_montserrat_16, 0);
    lv_obj_align(weather_condition_label, LV_ALIGN_TOP_LEFT, pad, pad + 30);

    weather_temp_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_temp_label, "--");
    lv_obj_set_style_text_color(weather_temp_label, lv_color_hex(0x7EE787), 0);
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_32, 0);
    lv_obj_align(weather_temp_label, LV_ALIGN_TOP_LEFT, pad, pad + 54);

    weather_extra_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_extra_label, "Humidity: --    Wind: --");
    lv_obj_set_style_text_color(weather_extra_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(weather_extra_label, &lv_font_montserrat_16, 0);
    lv_obj_align(weather_extra_label, LV_ALIGN_TOP_LEFT, pad, pad + 96);

    weather_sun_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_sun_label, "Sunrise: --   Sunset: --");
    lv_obj_set_style_text_color(weather_sun_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(weather_sun_label, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_sun_label, LV_ALIGN_TOP_LEFT, pad, pad + 118);

    weather_warning_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_warning_label, "Warnings: --");
    lv_obj_set_style_text_color(weather_warning_label, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(weather_warning_label, &lv_font_montserrat_16, 0);
    lv_obj_align(weather_warning_label, LV_ALIGN_TOP_LEFT, pad, pad + 140);

    weather_forecast_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_forecast_label, "Hourly forecast pending...");
    lv_obj_set_style_text_color(weather_forecast_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(weather_forecast_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_forecast_label, hor - (pad * 2));
    lv_label_set_long_mode(weather_forecast_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(weather_forecast_label, LV_ALIGN_TOP_LEFT, pad, pad + 166);

    weather_updated_label = lv_label_create(weather_tab);
    lv_label_set_text(weather_updated_label, "Updated: --");
    lv_obj_set_style_text_color(weather_updated_label, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(weather_updated_label, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_updated_label, LV_ALIGN_BOTTOM_LEFT, pad, -pad);

    weather_alerts_container = lv_obj_create(weather_tab);
    lv_obj_set_style_bg_opa(weather_alerts_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_alerts_container, 0, 0);
    lv_obj_set_size(weather_alerts_container, hor - (pad * 2), 80);
    lv_obj_align(weather_alerts_container, LV_ALIGN_BOTTOM_LEFT, pad, -36);

    rf_radar_label = lv_label_create(radar_tab);
    lv_label_set_text(rf_radar_label, "RF Radar\nWi-Fi: disabled\nLoRa: not enabled\n");
    lv_obj_set_style_text_color(rf_radar_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rf_radar_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(rf_radar_label, hor - (pad * 2));
    lv_label_set_long_mode(rf_radar_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(rf_radar_label, LV_ALIGN_TOP_LEFT, pad, pad);

    lv_obj_t *radar_toggle_row = lv_obj_create(radar_tab);
    lv_obj_set_size(radar_toggle_row, hor - (pad * 2), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(radar_toggle_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(radar_toggle_row, 0, 0);
    lv_obj_set_flex_flow(radar_toggle_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(radar_toggle_row, 8, 0);
    lv_obj_align(radar_toggle_row, LV_ALIGN_BOTTOM_LEFT, pad, -pad);

    lv_obj_t *wifi_row = lv_obj_create(radar_toggle_row);
    lv_obj_set_size(wifi_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_row, 0, 0);
    lv_obj_set_flex_flow(wifi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *wifi_label = lv_label_create(wifi_row);
    lv_label_set_text(wifi_label, "Enable Wi-Fi scan");
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_16, 0);

    rf_wifi_switch = lv_switch_create(wifi_row);
    if (rf_wifi_scan_enabled) {
        lv_obj_add_state(rf_wifi_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(rf_wifi_switch, radar_wifi_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lora_row = lv_obj_create(radar_toggle_row);
    lv_obj_set_size(lora_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(lora_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lora_row, 0, 0);
    lv_obj_set_flex_flow(lora_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lora_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lora_label = lv_label_create(lora_row);
    lv_label_set_text(lora_label, "Enable LoRa scan");
    lv_obj_set_style_text_color(lora_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(lora_label, &lv_font_montserrat_16, 0);

    rf_lora_switch = lv_switch_create(lora_row);
    lv_obj_add_event_cb(rf_lora_switch, radar_lora_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *ic705_container = lv_obj_create(ic705_tab);
    lv_obj_set_style_bg_opa(ic705_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ic705_container, 0, 0);
    lv_obj_set_style_pad_all(ic705_container, pad, 0);
    lv_obj_set_style_pad_row(ic705_container, 8, 0);
    lv_obj_set_size(ic705_container, hor, ver);
    lv_obj_align(ic705_container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(ic705_container, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *ic705_title = lv_label_create(ic705_container);
    lv_label_set_text(ic705_title, "IC-705");
    set_label_accent_primary(ic705_title);
    lv_obj_set_style_text_font(ic705_title, &lv_font_montserrat_18, 0);

    ic705_status_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_status_label, "Status: Disconnected");
    set_label_secondary(ic705_status_label);
    lv_obj_set_style_text_font(ic705_status_label, &lv_font_montserrat_16, 0);

    ic705_link_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_link_label, "Link: WiFi CI-V");
    set_label_secondary(ic705_link_label);
    lv_obj_set_style_text_font(ic705_link_label, &lv_font_montserrat_16, 0);

    ic705_target_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_target_label, "Target: --");
    set_label_secondary(ic705_target_label);
    lv_obj_set_style_text_font(ic705_target_label, &lv_font_montserrat_16, 0);

    ic705_device_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_device_label, "Device: --");
    set_label_secondary(ic705_device_label);
    lv_obj_set_style_text_font(ic705_device_label, &lv_font_montserrat_16, 0);

    ic705_freq_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_freq_label, "Frequency: --");
    set_label_primary(ic705_freq_label);
    lv_obj_set_style_text_font(ic705_freq_label, &lv_font_montserrat_20, 0);

    ic705_mode_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_mode_label, "Mode: --");
    set_label_secondary(ic705_mode_label);
    lv_obj_set_style_text_font(ic705_mode_label, &lv_font_montserrat_16, 0);

    ic705_smeter_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_smeter_label, "S-meter: --");
    set_label_secondary(ic705_smeter_label);
    lv_obj_set_style_text_font(ic705_smeter_label, &lv_font_montserrat_16, 0);

    ic705_smeter_bar = lv_bar_create(ic705_container);
    lv_obj_set_width(ic705_smeter_bar, LV_PCT(100));
    lv_obj_set_height(ic705_smeter_bar, 18);
    lv_bar_set_range(ic705_smeter_bar, 0, 255);
    lv_bar_set_value(ic705_smeter_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ic705_smeter_bar, theme_surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ic705_smeter_bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ic705_smeter_bar, theme_accent_primary(), LV_PART_INDICATOR);

    ic705_last_update_label = lv_label_create(ic705_container);
    lv_label_set_text(ic705_last_update_label, "Last update: --");
    set_label_muted(ic705_last_update_label);
    lv_obj_set_style_text_font(ic705_last_update_label, &lv_font_montserrat_14, 0);

    update_spots_table();
}

static void update_spots_table_internal(void)
{
    if (!table) return;
    hamview_spot_t spots[HAMVIEW_MAX_SPOTS];
    size_t count = hamview_backend_get_spots(spots, HAMVIEW_MAX_SPOTS);

    for (size_t i = 0; i < HAMVIEW_MAX_SPOTS; ++i) {
        current_row_info[i].occupied = false;
    }

    size_t display_index = 0;
    for (size_t i = 0; i < count && display_index < HAMVIEW_MAX_SPOTS; ++i) {
        spot_row_info_t info = {0};
        if (!spot_passes_filters(&spots[i], &info)) {
            continue;
        }
        current_row_info[display_index] = info;
        current_row_info[display_index].occupied = true;

        char call_buf[48];
        if (info.is_priority) {
            snprintf(call_buf, sizeof(call_buf), LV_SYMBOL_WARNING " %s", spots[i].callsign);
        } else if (spots[i].is_new) {
            snprintf(call_buf, sizeof(call_buf), LV_SYMBOL_BELL " %s", spots[i].callsign);
        } else {
            strlcpy(call_buf, spots[i].callsign, sizeof(call_buf));
        }

        char location_buf[96] = "";
        if (spots[i].state[0] && spots[i].country[0]) {
            snprintf(location_buf, sizeof(location_buf), "%s, %s", spots[i].state, spots[i].country);
        } else if (spots[i].state[0]) {
            strlcpy(location_buf, spots[i].state, sizeof(location_buf));
        } else if (spots[i].country[0]) {
            strlcpy(location_buf, spots[i].country, sizeof(location_buf));
        } else if (spots[i].dxcc[0]) {
            strlcpy(location_buf, spots[i].dxcc, sizeof(location_buf));
        }

        const char *alert_prefix = info.is_priority ? "[ALERT] " : "";
        char comment_buf[192];
        if (spots[i].comment[0] && location_buf[0]) {
            snprintf(comment_buf, sizeof(comment_buf), "%s%s  |  %s  |  Age %lus", alert_prefix, spots[i].comment, location_buf, (unsigned long)spots[i].age_seconds);
        } else if (spots[i].comment[0]) {
            snprintf(comment_buf, sizeof(comment_buf), "%s%s  |  Age %lus", alert_prefix, spots[i].comment, (unsigned long)spots[i].age_seconds);
        } else if (location_buf[0]) {
            snprintf(comment_buf, sizeof(comment_buf), "%s%s  |  Age %lus", alert_prefix, location_buf, (unsigned long)spots[i].age_seconds);
        } else {
            snprintf(comment_buf, sizeof(comment_buf), "%sAge %lus", alert_prefix, (unsigned long)spots[i].age_seconds);
        }

        size_t row = display_index + 1;
        lv_table_set_cell_value(table, row, 0, call_buf);
        lv_table_set_cell_value(table, row, 1, spots[i].frequency);
        lv_table_set_cell_value(table, row, 2, spots[i].mode);
        lv_table_set_cell_value(table, row, 3, spots[i].spotter);
        lv_table_set_cell_value(table, row, 4, comment_buf);
        if (spots[i].is_new && info.is_priority) {
            if (location_buf[0]) {
                set_message("Alert: %s on %s %s (%s)", spots[i].callsign, spots[i].frequency, spots[i].mode, location_buf);
            } else {
                set_message("Alert: %s on %s %s", spots[i].callsign, spots[i].frequency, spots[i].mode);
            }
        }
        ++display_index;
    }

    for (size_t i = display_index; i < HAMVIEW_MAX_SPOTS; ++i) {
        size_t row = i + 1;
        for (int col = 0; col < 5; ++col) {
            lv_table_set_cell_value(table, row, col, "");
        }
        current_row_info[i].occupied = false;
    }

    if (message_label) {
        const char *existing = lv_label_get_text(message_label);
        if (display_index == 0) {
            if (!existing || existing[0] == '\0' || strcmp(existing, "No spots match current filters") == 0) {
                lv_label_set_text(message_label, "No spots match current filters");
            }
        } else if (existing && strcmp(existing, "No spots match current filters") == 0) {
            lv_label_set_text(message_label, "");
        }
    }

    update_activity_charts();
}

static void update_spots_table_async(void *param)
{
    (void)param;
    update_spots_table_internal();
}

static void update_spots_table(void)
{
    if (!lv_port_is_in_lvgl_task()) {
        lv_async_call(update_spots_table_async, NULL);
        return;
    }
    update_spots_table_internal();
}

static bool spot_passes_filters(const hamview_spot_t *spot, spot_row_info_t *info_out)
{
    if (!spot) {
        return false;
    }

    if (spot_age_filter_minutes > 0) {
        uint32_t max_age = (uint32_t)spot_age_filter_minutes * 60U;
        if (spot->age_seconds > max_age) {
            return false;
        }
    }

    spot_row_info_t info = {
        .mode_category = classify_mode_from_text(spot->mode),
        .band_category = classify_band_from_freq(spot->frequency),
        .is_new = spot->is_new,
        .is_priority = hamview_alert_is_high_priority(spot),
        .occupied = true,
    };

    bool mode_match = (current_mode_filter == SPOT_FILTER_MODE_ALL) || (current_mode_filter == info.mode_category);
    bool band_match = (current_band_filter == SPOT_FILTER_BAND_ALL) || (current_band_filter == info.band_category);

    if (info_out) {
        *info_out = info;
    }
    return mode_match && band_match;
}

static void filter_dropdown_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    if (target == mode_filter_dropdown) {
        uint32_t sel = lv_dropdown_get_selected(mode_filter_dropdown);
        if (sel > SPOT_FILTER_MODE_OTHER) {
            sel = SPOT_FILTER_MODE_OTHER;
        }
        current_mode_filter = (spot_filter_mode_t)sel;
    } else if (target == band_filter_dropdown) {
        uint32_t sel = lv_dropdown_get_selected(band_filter_dropdown);
        if (sel > SPOT_FILTER_BAND_70CM) {
            sel = SPOT_FILTER_BAND_70CM;
        }
        current_band_filter = (spot_filter_band_t)sel;
    }

    hamview_screen_record_activity();
    update_spots_table();
}

static void spots_table_draw_event(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc || dsc->part != LV_PART_ITEMS) {
        return;
    }

    uint32_t row = dsc->id;
    if (row == 0) {
        if (dsc->label_dsc) {
            dsc->label_dsc->color = theme_primary_text();
        }
        return;
    }

    uint32_t index = row - 1;
    if (index >= HAMVIEW_MAX_SPOTS) {
        return;
    }
    if (!current_row_info[index].occupied) {
        return;
    }

    const spot_row_info_t *info = &current_row_info[index];
    lv_color_t mode_color = color_for_mode(info->mode_category);

    if (dsc->rect_dsc) {
        dsc->rect_dsc->bg_opa = LV_OPA_TRANSP;
        dsc->rect_dsc->outline_width = 0;
    }
    if (dsc->label_dsc) {
        dsc->label_dsc->color = mode_color;
    }
}

static void update_status_labels(void)
{
    hamview_status_t status;
    hamview_backend_get_status(&status);

    if (wifi_status_label) {
        const char *symbol = status.wifi_connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE;
        char wifi_text[32];
        snprintf(wifi_text, sizeof(wifi_text), "%s Wi-Fi", symbol);
        lv_label_set_text(wifi_status_label, wifi_text);
        lv_obj_set_style_text_color(wifi_status_label,
                        status.wifi_connected ? theme_accent_secondary() : theme_error(), 0);
    }

    if (wifi_signal_label) {
        char signal_text[48];
        if (status.wifi_connected && last_wifi_connected) {
            int bars = 0;
            if (last_wifi_rssi >= -50) {
                bars = 4;
            } else if (last_wifi_rssi >= -60) {
                bars = 3;
            } else if (last_wifi_rssi >= -70) {
                bars = 2;
            } else if (last_wifi_rssi >= -80) {
                bars = 1;
            }
            snprintf(signal_text, sizeof(signal_text), "Signal: %d/4 (%d dBm)", bars, last_wifi_rssi);
        } else {
            snprintf(signal_text, sizeof(signal_text), "Signal: --");
        }
        lv_label_set_text(wifi_signal_label, signal_text);
    }

    if (ham_status_label) {
        const char *symbol = status.hamalert_connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE;
        char ham_text[48];
        if (status.hamalert_connected) {
            snprintf(ham_text, sizeof(ham_text), "%s HamAlert Connected", symbol);
        } else {
            snprintf(ham_text, sizeof(ham_text), "%s HamAlert", symbol);
        }
        lv_label_set_text(ham_status_label, ham_text);
        lv_obj_set_style_text_color(ham_status_label,
                                    status.hamalert_connected ? theme_accent_secondary() : theme_error(), 0);
    }

    if (strlen(status.last_error) > 0) {
        lv_label_set_text(error_label, status.last_error);
        set_label_error(error_label);
    } else {
        lv_label_set_text(error_label, "");
    }

    if (!status.hamalert_connected) {
        set_message("Configure HamAlert credentials via Settings");
    }
}

static void update_clock_labels(void)
{
    if (!ham_local_time_label && !weather_local_time_label) {
        return;
    }

    hamview_weather_info_t info;
    memset(&info, 0, sizeof(info));
    hamview_weather_get(&info);

    time_t now = time(NULL);
    bool time_ready = (now > 1000) && info.time_synced;

    char utc_text[48];
    char local_text[96];

    if (time_ready) {
        struct tm tm_utc = {0};
        gmtime_r(&now, &tm_utc);
        char utc_buf[16];
        strftime(utc_buf, sizeof(utc_buf), time_use_24h ? "%H:%M" : "%I:%M %p", &tm_utc);
        snprintf(utc_text, sizeof(utc_text), "UTC: %s", utc_buf);

        if (info.timezone_valid) {
            time_t local_epoch = now + (time_t)info.timezone_offset_minutes * 60;
            struct tm tm_local = {0};
            gmtime_r(&local_epoch, &tm_local);
            char local_buf[16];
            strftime(local_buf, sizeof(local_buf), time_use_24h ? "%H:%M" : "%I:%M %p", &tm_local);

            char tz_label[64];
            if (info.timezone_name[0]) {
                strlcpy(tz_label, info.timezone_name, sizeof(tz_label));
                if (strlen(tz_label) > 30) {
                    int hours = info.timezone_offset_minutes / 60;
                    int minutes = abs(info.timezone_offset_minutes % 60);
                    snprintf(tz_label, sizeof(tz_label), "UTC%+d:%02d", hours, minutes);
                }
            } else {
                int hours = info.timezone_offset_minutes / 60;
                int minutes = abs(info.timezone_offset_minutes % 60);
                snprintf(tz_label, sizeof(tz_label), "UTC%+d:%02d", hours, minutes);
            }

            snprintf(local_text, sizeof(local_text), "Local (%s) %s", tz_label, local_buf);
        } else {
            if (info.has_data) {
                snprintf(local_text, sizeof(local_text), "Local: timezone unavailable");
            } else {
                snprintf(local_text, sizeof(local_text), "Local: waiting for weather");
            }
        }
    } else {
        snprintf(utc_text, sizeof(utc_text), "UTC: --");
        if (info.last_error[0]) {
            snprintf(local_text, sizeof(local_text), "Local: %s", info.last_error);
        } else {
            snprintf(local_text, sizeof(local_text), "Local: syncing...");
        }
    }

    if (ham_local_time_label) {
        lv_label_set_text(ham_local_time_label, local_text);
    }
    if (ham_utc_time_label) {
        lv_label_set_text(ham_utc_time_label, utc_text);
    }
    if (weather_local_time_label) {
        lv_label_set_text(weather_local_time_label, local_text);
    }
    if (weather_utc_time_label) {
        lv_label_set_text(weather_utc_time_label, utc_text);
    }
}

static void update_weather_panel(void)
{
    if (!weather_location_label || !weather_alerts_container) {
        return;
    }

    hamview_weather_info_t info;
    memset(&info, 0, sizeof(info));
    bool have_info = hamview_weather_get(&info);

    if (!have_info || !info.has_data) {
        lv_label_set_text(weather_location_label, "Weather: configure ZIP in Settings");
        lv_label_set_text(weather_condition_label, info.last_error[0] ? info.last_error : "Waiting for weather data...");
        lv_label_set_text(weather_temp_label, "--");
        lv_label_set_text(weather_extra_label, "Humidity: --    Wind: --");
        lv_label_set_text(weather_updated_label, "Updated: --");
        if (weather_sun_label) {
            lv_label_set_text(weather_sun_label, "Sunrise: --   Sunset: --");
        }
        if (weather_warning_label) {
            lv_label_set_text(weather_warning_label, "Warnings: --");
            set_label_warning(weather_warning_label);
        }
        if (weather_forecast_label) {
            lv_label_set_text(weather_forecast_label, "Hourly forecast unavailable");
            set_label_muted(weather_forecast_label);
        }

        lv_obj_clean(weather_alerts_container);
        lv_obj_t *alerts_title = lv_label_create(weather_alerts_container);
        lv_obj_set_style_text_font(alerts_title, &lv_font_montserrat_18, 0);
        set_label_primary(alerts_title);
        lv_label_set_text(alerts_title, "Alerts");

        lv_obj_t *placeholder = lv_label_create(weather_alerts_container);
        lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_16, 0);
        set_label_muted(placeholder);
        lv_label_set_text(placeholder, "No alerts available");
        return;
    }

    char location_buf[96];
    if (strlen(info.location) > 0) {
        snprintf(location_buf, sizeof(location_buf), "%s", info.location);
    } else {
        snprintf(location_buf, sizeof(location_buf), "Weather");
    }
    lv_label_set_text(weather_location_label, location_buf);

    if (weather_condition_label) {
        lv_label_set_text(weather_condition_label, info.condition[0] ? info.condition : "Condition unavailable");
    }

    if (weather_temp_label) {
        char temp_buf[48];
        const char *primary = weather_show_celsius ? info.temperature_c : info.temperature_f;
        const char *fallback = weather_show_celsius ? info.temperature_f : info.temperature_c;
        if (primary[0]) {
            snprintf(temp_buf, sizeof(temp_buf), "%s", primary);
        } else if (fallback[0]) {
            snprintf(temp_buf, sizeof(temp_buf), "%s", fallback);
        } else {
            snprintf(temp_buf, sizeof(temp_buf), "--");
        }
        lv_label_set_text(weather_temp_label, temp_buf);
    }

    if (weather_extra_label) {
        char extra_buf[128];
        snprintf(extra_buf, sizeof(extra_buf), "Humidity: %s    Wind: %s",
                 info.humidity[0] ? info.humidity : "--",
                 info.wind_mph[0] ? info.wind_mph : "--");
        lv_label_set_text(weather_extra_label, extra_buf);
    }

    if (weather_sun_label) {
        char sun_buf[64];
        if (info.sun_times_valid && info.sun_times_count > 0) {
            uint16_t sunrise = info.sunrise_minutes[0];
            uint16_t sunset = info.sunset_minutes[0];
            uint16_t sr_h = sunrise / 60;
            uint16_t sr_m = sunrise % 60;
            uint16_t ss_h = sunset / 60;
            uint16_t ss_m = sunset % 60;
            if (!time_use_24h) {
                uint16_t sr_h12 = sr_h % 12;
                uint16_t ss_h12 = ss_h % 12;
                if (sr_h12 == 0) sr_h12 = 12;
                if (ss_h12 == 0) ss_h12 = 12;
                snprintf(sun_buf, sizeof(sun_buf), "Sunrise: %u:%02u %s   Sunset: %u:%02u %s",
                         sr_h12, sr_m, (sr_h >= 12) ? "PM" : "AM",
                         ss_h12, ss_m, (ss_h >= 12) ? "PM" : "AM");
            } else {
                snprintf(sun_buf, sizeof(sun_buf), "Sunrise: %02u:%02u   Sunset: %02u:%02u",
                         sr_h, sr_m, ss_h, ss_m);
            }
        } else {
            snprintf(sun_buf, sizeof(sun_buf), "Sunrise: --   Sunset: --");
        }
        lv_label_set_text(weather_sun_label, sun_buf);
    }

    if (weather_warning_label) {
        if (info.high_wind_warning || info.lightning_warning) {
            char warn_buf[96];
            const char *wind_text = info.high_wind_warning ? "High winds" : NULL;
            const char *light_text = info.lightning_warning ? "Lightning risk" : NULL;
            if (wind_text && light_text) {
                snprintf(warn_buf, sizeof(warn_buf), "%s Warnings: %s + %s", LV_SYMBOL_WARNING, wind_text, light_text);
            } else if (wind_text) {
                snprintf(warn_buf, sizeof(warn_buf), "%s Warnings: %s", LV_SYMBOL_WARNING, wind_text);
            } else {
                snprintf(warn_buf, sizeof(warn_buf), "%s Warnings: %s", LV_SYMBOL_WARNING, light_text ? light_text : "");
            }
            lv_label_set_text(weather_warning_label, warn_buf);
            lv_obj_set_style_text_color(weather_warning_label, theme_error(), 0);
        } else {
            lv_label_set_text(weather_warning_label, "Warnings: none");
            lv_obj_set_style_text_color(weather_warning_label, theme_accent_primary(), 0);
        }
    }

    if (weather_forecast_label) {
        if (info.forecast_valid && info.forecast_count > 0) {
            char forecast_buf[256];
            size_t offset = 0;
            offset += (size_t)snprintf(forecast_buf, sizeof(forecast_buf), "Next hours: ");
            size_t limit = info.forecast_count;
            if (limit > 4) {
                limit = 4;
            }
            for (size_t i = 0; i < limit; ++i) {
                const hamview_weather_hourly_entry_t *entry = &info.forecast[i];
                char segment[80];
                const char *forecast_temp = weather_show_celsius ? entry->temp_c : entry->temp_f;
                if (!forecast_temp[0]) {
                    forecast_temp = weather_show_celsius ? entry->temp_f : entry->temp_c;
                }
                int len = snprintf(segment, sizeof(segment), "%s %s", entry->time_local[0] ? entry->time_local : "--:--",
                                   forecast_temp[0] ? forecast_temp : "--");
                if (entry->precip_percent > 0) {
                    len += snprintf(segment + len, sizeof(segment) - (size_t)len, " %u%%", (unsigned)entry->precip_percent);
                }
                if (entry->lightning_risk || entry->sustained_wind_mph >= 30) {
                    len += snprintf(segment + len, sizeof(segment) - (size_t)len, " %s", LV_SYMBOL_WARNING);
                }
                size_t remaining = (offset < sizeof(forecast_buf)) ? (sizeof(forecast_buf) - offset) : 0;
                if (remaining > 1) {
                    int written = snprintf(forecast_buf + offset, remaining, "%s%s", (i > 0) ? ", " : "", segment);
                    if (written > 0) {
                        offset += (size_t)written;
                    }
                }
            }
            lv_label_set_text(weather_forecast_label, forecast_buf);
            set_label_secondary(weather_forecast_label);
        } else {
            lv_label_set_text(weather_forecast_label, "Hourly forecast not available");
            set_label_muted(weather_forecast_label);
        }
    }

    if (weather_updated_label) {
        char updated_buf[64];
        if (info.observation_time[0]) {
            snprintf(updated_buf, sizeof(updated_buf), "Updated: %s", info.observation_time);
        } else {
            snprintf(updated_buf, sizeof(updated_buf), "Updated: --");
        }
        lv_label_set_text(weather_updated_label, updated_buf);
    }

    lv_obj_clean(weather_alerts_container);

    lv_obj_t *alerts_title = lv_label_create(weather_alerts_container);
    lv_obj_set_style_text_font(alerts_title, &lv_font_montserrat_18, 0);
    lv_label_set_text(alerts_title, "Alerts");

    if (info.alert_count == 0) {
        lv_obj_t *placeholder = lv_label_create(weather_alerts_container);
        lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_16, 0);
        set_label_muted(placeholder);
        lv_label_set_text(placeholder, "No active alerts");
    } else {
        for (size_t idx = 0; idx < info.alert_count && idx < 3; ++idx) {
            lv_obj_t *alert_label = lv_label_create(weather_alerts_container);
            lv_obj_set_style_text_font(alert_label, &lv_font_montserrat_16, 0);
            set_label_secondary(alert_label);
            lv_label_set_text(alert_label, info.alerts[idx]);
        }
    }

    if (info.last_error[0]) {
        lv_obj_t *warning = lv_label_create(weather_alerts_container);
        lv_obj_set_style_text_font(warning, &lv_font_montserrat_14, 0);
        set_label_warning(warning);
        lv_label_set_text(warning, info.last_error);
    }
}

static bool format_event_log_timestamp(uint32_t epoch, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    if (epoch == 0) {
        out[0] = '\0';
        return false;
    }

    time_t ts = (time_t)epoch;
    struct tm tm_info;
#if defined(_MSC_VER)
    if (localtime_s(&tm_info, &ts) != 0) {
        out[0] = '\0';
        return false;
    }
#else
    if (!localtime_r(&ts, &tm_info)) {
        out[0] = '\0';
        return false;
    }
#endif
    if (strftime(out, out_size, "%H:%M:%S", &tm_info) == 0) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static void update_event_log_panel(void)
{
    if (!log_label) {
        return;
    }

    hamview_event_log_entry_t entries[HAMVIEW_EVENT_LOG_CAPACITY];
    size_t count = hamview_event_log_get(entries, HAMVIEW_EVENT_LOG_CAPACITY);
    if (count == 0) {
        lv_label_set_text(log_label, "Event log empty");
        return;
    }

    const size_t display_max = 20;
    size_t start = (count > display_max) ? (count - display_max) : 0;
    char text[4096];
    size_t offset = 0;
    for (size_t i = start; i < count; ++i) {
        const hamview_event_log_entry_t *entry = &entries[i];
        char time_buf[16] = {0};
        if (!format_event_log_timestamp(entry->timestamp, time_buf, sizeof(time_buf))) {
            snprintf(time_buf, sizeof(time_buf), "%lu", (unsigned long)entry->timestamp);
        }
        char line[192];
        snprintf(line, sizeof(line), "[%s] %s: %s",
                 (time_buf[0] != '\0') ? time_buf : "--:--",
                 entry->source[0] ? entry->source : "sys",
                 entry->message[0] ? entry->message : "--");
        size_t remaining = (offset < sizeof(text)) ? (sizeof(text) - offset) : 0;
        if (remaining <= 1) {
            break;
        }
        int written = snprintf(text + offset, remaining, "%s%s",
                               (offset > 0) ? "\n" : "",
                               line);
        if (written < 0) {
            break;
        }
        offset += (size_t)written;
    }

    if (offset == 0) {
        lv_label_set_text(log_label, "Event log empty");
    } else {
        text[offset] = '\0';
        lv_label_set_text(log_label, text);
    }
}

static void update_weather_action_buttons(void)
{
    if (weather_refresh_label) {
        lv_label_set_text(weather_refresh_label, LV_SYMBOL_REFRESH " Refresh");
        set_label_primary(weather_refresh_label);
    }

    if (weather_refresh_btn) {
        lv_obj_set_style_bg_color(weather_refresh_btn, theme_accent_primary(), 0);
        lv_obj_set_style_bg_color(weather_refresh_btn, theme_accent_primary(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(weather_refresh_btn, theme_accent_primary(), LV_STATE_PRESSED);
    }

    if (weather_units_label) {
        const char *units_text = weather_show_celsius ? LV_SYMBOL_SHUFFLE " Show F" : LV_SYMBOL_SHUFFLE " Show C";
        lv_label_set_text(weather_units_label, units_text);
        set_label_primary(weather_units_label);
    }

    if (weather_units_btn) {
        lv_color_t inactive = theme_surface_alt();
        lv_color_t active = theme_accent_secondary();
        lv_obj_set_style_bg_color(weather_units_btn, weather_show_celsius ? active : inactive, 0);
        lv_obj_set_style_bg_color(weather_units_btn, theme_accent_primary(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(weather_units_btn, theme_accent_primary(), LV_STATE_PRESSED);
    }

    bool muted = hamview_alert_is_muted();
    if (weather_mute_label) {
        const char *mute_text = muted ? LV_SYMBOL_VOLUME_MAX " Unmute alerts"
                                      : LV_SYMBOL_MUTE " Mute alerts";
        lv_label_set_text(weather_mute_label, mute_text);
        set_label_primary(weather_mute_label);
    }

    if (weather_mute_btn) {
        lv_color_t inactive = theme_surface_alt();
        lv_color_t muted_color = theme_error();
        lv_obj_set_style_bg_color(weather_mute_btn, muted ? muted_color : inactive, 0);
        lv_obj_set_style_bg_color(weather_mute_btn, theme_accent_primary(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(weather_mute_btn, theme_accent_primary(), LV_STATE_PRESSED);
    }
}

static void theme_toggle_btn_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    lv_obj_t *btn = lv_event_get_target(e);
    animate_button_feedback(btn);

    current_theme = (current_theme == HAMVIEW_THEME_DARK) ? HAMVIEW_THEME_LIGHT : HAMVIEW_THEME_DARK;
    apply_theme();

    if (message_label) {
        set_message(current_theme == HAMVIEW_THEME_DARK ? "Dark theme enabled" : "Light theme enabled");
    }
}

static void weather_action_btn_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    lv_obj_t *btn = lv_event_get_target(e);
    animate_button_feedback(btn);

    weather_action_t action = (weather_action_t)(uintptr_t)lv_event_get_user_data(e);
    switch (action) {
        case WEATHER_ACTION_REFRESH:
            hamview_weather_request_refresh();
            set_message("Weather refresh requested");
            break;
        case WEATHER_ACTION_TOGGLE_UNITS:
            weather_show_celsius = !weather_show_celsius;
            update_weather_panel();
            set_message(weather_show_celsius ? "Weather shown in Celsius" : "Weather shown in Fahrenheit");
            break;
        case WEATHER_ACTION_TOGGLE_ALERT_MUTE: {
            hamview_alert_toggle_muted();
            bool muted = hamview_alert_is_muted();
            set_message(muted ? "Alerts muted" : "Alerts unmuted");
            break;
        }
        default:
            break;
    }

    update_weather_action_buttons();
    update_event_log_panel();
}

void hamview_ui_show_dashboard(void)
{
    lv_port_sem_take();
    build_dashboard_ui();
    if (tabview) {
        lv_tabview_set_act(tabview, 0, LV_ANIM_OFF);
    }
    lv_scr_load(dashboard_screen);
    hamview_screen_record_activity();
    dashboard_loaded = true;
    update_clock_labels();
    lv_port_sem_give();
}

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    if (id != VIEW_EVENT_WIFI_ST || !event_data) return;
    const struct view_data_wifi_st *st = (const struct view_data_wifi_st *)event_data;
    last_wifi_connected = st->is_connected;
    last_wifi_rssi = st->rssi;
    if (st->is_connected) {
        hamview_ui_show_dashboard();
    } else {
        dashboard_loaded = false;
        hamview_wifi_ui_show();
    }
}

static void wifi_list_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    if (id != VIEW_EVENT_WIFI_LIST || !event_data) {
        return;
    }
    memcpy(&rf_wifi_list, event_data, sizeof(rf_wifi_list));
    rf_wifi_list_valid = true;
}

void hamview_ui_init(void)
{
    hamview_settings_t init_settings;
    hamview_settings_get(&init_settings);
    spot_age_filter_minutes = init_settings.spot_age_filter_minutes;

    hamview_icom_init();
    hamview_icom_set_civ_wifi(init_settings.icom_wifi_enabled, init_settings.icom_wifi_ip, init_settings.icom_wifi_port,
                              init_settings.icom_wifi_user, init_settings.icom_wifi_pass);

    lv_port_sem_take();
    build_dashboard_ui();
    lv_port_sem_give();

    hamview_screen_init();
    hamview_screen_configure(init_settings.screen_timeout_minutes);
    hamview_screen_set_brightness(init_settings.screen_brightness_percent);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, wifi_list_event_handler, NULL, NULL));

    request_wifi_scan();

    if (!refresh_timer) {
        refresh_timer = lv_timer_create(refresh_timer_cb, 5000, NULL);
    }
}
