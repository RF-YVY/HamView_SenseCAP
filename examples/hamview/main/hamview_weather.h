#define HAMVIEW_WEATHER_SUN_TIMES 3
#define HAMVIEW_WEATHER_HOURLY_COUNT 12

#ifndef HAMVIEW_WEATHER_H
#define HAMVIEW_WEATHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char time_local[8];
    char temp_f[8];
    char temp_c[8];
    uint8_t precip_percent;
    uint8_t sustained_wind_mph;
    bool lightning_risk;
} hamview_weather_hourly_entry_t;

typedef struct {
    bool has_data;
    char location[64];
    char condition[64];
    char temperature_f[8];
    char temperature_c[8];
    char feels_like_f[8];
    char humidity[8];
    char wind_mph[16];
    char observation_time[16];
    size_t alert_count;
    char alerts[3][96];
    uint32_t last_update_epoch;
    char last_error[96];
    bool timezone_valid;
    char timezone_name[64];
    int timezone_offset_minutes;
    bool time_synced;
    bool sun_times_valid;
    uint32_t sunrise_epoch[HAMVIEW_WEATHER_SUN_TIMES];
    uint32_t sunset_epoch[HAMVIEW_WEATHER_SUN_TIMES];
    uint16_t sunrise_minutes[HAMVIEW_WEATHER_SUN_TIMES];
    uint16_t sunset_minutes[HAMVIEW_WEATHER_SUN_TIMES];
    size_t sun_times_count;
    bool forecast_valid;
    size_t forecast_count;
    hamview_weather_hourly_entry_t forecast[HAMVIEW_WEATHER_HOURLY_COUNT];
    bool high_wind_warning;
    bool lightning_warning;
} hamview_weather_info_t;

esp_err_t hamview_weather_init(void);
void hamview_weather_on_settings_updated(void);
bool hamview_weather_get(hamview_weather_info_t *out);
void hamview_weather_request_refresh(void);

#ifdef __cplusplus
}
#endif

#endif
