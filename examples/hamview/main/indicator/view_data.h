#ifndef HAMVIEW_VIEW_DATA_H
#define HAMVIEW_VIEW_DATA_H

#include "config.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum start_screen {
    SCREEN_SENSECAP_LOG,
    SCREEN_WIFI_CONFIG,
};

#define WIFI_SCAN_LIST_SIZE  15

struct view_data_wifi_st {
    bool   is_connected;
    bool   is_connecting;
    bool   is_network;
    char   ssid[32];
    int8_t rssi;
};

struct view_data_wifi_config {
    char    ssid[32];
    uint8_t password[64];
    bool    have_password;
};

struct view_data_wifi_item {
    char   ssid[32];
    bool   auth_mode;
    int8_t rssi;
};

struct view_data_wifi_list {
    bool  is_connect;
    struct view_data_wifi_item  connect;
    uint16_t cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg {
    uint8_t ret;
    char    msg[64];
};

struct view_data_display {
    int   brightness;
    bool  sleep_mode_en;
    int   sleep_mode_time_min;
};

struct view_data_time_cfg {
    bool    time_format_24;
    bool    auto_update;
    time_t  time;
    bool    set_time;
    bool    auto_update_zone;
    int8_t  zone;
    bool    daylight;
} __attribute__((packed));

struct sensor_data_average {
    float   data;
    time_t  timestamp;
    bool    valid;
};

struct sensor_data_minmax {
    float   max;
    float   min;
    time_t  timestamp;
    bool    valid;
};

enum sensor_data_type {
    SENSOR_DATA_CO2,
    SENSOR_DATA_TVOC,
    SENSOR_DATA_TEMP,
    SENSOR_DATA_HUMIDITY,
};

struct view_data_sensor_data {
    enum sensor_data_type sensor_type;
    float  vaule;
};

struct view_data_sensor_history_data {
    enum sensor_data_type sensor_type;
    struct sensor_data_average data_day[24];
    struct sensor_data_minmax data_week[7];
    uint8_t resolution;
    float day_min;
    float day_max;
    float week_min;
    float week_max;
};

enum {
    VIEW_EVENT_SCREEN_START = 0,
    VIEW_EVENT_TIME,
    VIEW_EVENT_WIFI_ST,
    VIEW_EVENT_CITY,
    VIEW_EVENT_SENSOR_DATA,
    VIEW_EVENT_SENSOR_TEMP,
    VIEW_EVENT_SENSOR_HUMIDITY,
    VIEW_EVENT_SENSOR_TVOC,
    VIEW_EVENT_SENSOR_CO2,
    VIEW_EVENT_SENSOR_TEMP_HISTORY,
    VIEW_EVENT_SENSOR_HUMIDITY_HISTORY,
    VIEW_EVENT_SENSOR_TVOC_HISTORY,
    VIEW_EVENT_SENSOR_CO2_HISTORY,
    VIEW_EVENT_SENSOR_DATA_HISTORY,
    VIEW_EVENT_WIFI_LIST,
    VIEW_EVENT_WIFI_LIST_REQ,
    VIEW_EVENT_WIFI_CONNECT,
    VIEW_EVENT_WIFI_CONNECT_RET,
    VIEW_EVENT_WIFI_CFG_DELETE,
    VIEW_EVENT_TIME_CFG_UPDATE,
    VIEW_EVENT_TIME_CFG_APPLY,
    VIEW_EVENT_DISPLAY_CFG,
    VIEW_EVENT_BRIGHTNESS_UPDATE,
    VIEW_EVENT_DISPLAY_CFG_APPLY,
    VIEW_EVENT_SHUTDOWN,
    VIEW_EVENT_FACTORY_RESET,
    VIEW_EVENT_SCREEN_CTRL,
    VIEW_EVENT_ALL,
};

#ifdef __cplusplus
}
#endif

#endif
