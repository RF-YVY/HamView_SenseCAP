#ifndef HAMVIEW_SETTINGS_H
#define HAMVIEW_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char username[64];
    char password[64];
    char filter_callsign[32];
    char filter_band[16];
    char weather_zip[16];
    char icom_wifi_ip[32];
    char icom_wifi_user[32];
    char icom_wifi_pass[32];
    uint16_t icom_wifi_port;
    bool icom_wifi_enabled;
    uint16_t spot_ttl_minutes;
    uint16_t spot_age_filter_minutes;
    uint16_t screen_timeout_minutes;
    bool alert_sound_enabled;
    uint8_t screen_brightness_percent;
    char alert_callsigns[128];
    char alert_states[128];
    char alert_countries[128];
} hamview_settings_t;

esp_err_t hamview_settings_init(void);
void hamview_settings_get(hamview_settings_t *out);
esp_err_t hamview_settings_save(const hamview_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif
