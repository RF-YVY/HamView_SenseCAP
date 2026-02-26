#include "hamview_weather.h"

#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"

#include "hamview_settings.h"
#include "indicator/config.h"
#include "indicator/view_data.h"
#include "hamview_event_log.h"

extern esp_err_t esp_crt_bundle_attach(void *conf);

#define WEATHER_FETCH_INTERVAL_MS (10 * 60 * 1000)
#define WEATHER_EVENT_FETCH    (1 << 0)
#define WEATHER_EVENT_SETTINGS (1 << 1)

static const char *TAG = "hamview_weather";

static SemaphoreHandle_t s_info_mutex;
static hamview_weather_info_t s_info;
static EventGroupHandle_t s_weather_events;
static TaskHandle_t s_weather_task;
static bool s_wifi_connected = false;
static bool s_has_ip = false;
static bool s_sntp_started = false;
static bool s_time_synced = false;

static void ensure_sntp_started(void);
static void update_time_sync_flag(bool synced);
static bool url_encode_component(const char *src, char *dst, size_t dst_len);
static char *http_get_json(const char *url, int *out_status);
static const char *weather_code_to_text(int code);
static void format_temperature(char *dst, size_t dst_len, double value, char unit);
static void format_percentage(char *dst, size_t dst_len, double value);
static void format_speed(char *dst, size_t dst_len, double value);
static void format_observation_time(const char *iso8601, char *dst, size_t dst_len);
static bool find_hourly_value(cJSON *hourly, const char *field_name, const char *target_time, double *out_value);
static esp_err_t geocode_location(const char *query, double *out_lat, double *out_lon, char *display_name, size_t display_len);
static void fetch_weather_alerts(double latitude, double longitude, hamview_weather_info_t *info);
static bool parse_local_iso8601(const char *iso, int *year, int *month, int *day, int *hour, int *minute, int *second);
static bool local_datetime_to_epoch(int year, int month, int day, int hour, int minute, int second, int offset_minutes, uint32_t *epoch_out);
static void populate_sun_times(cJSON *daily, int offset_minutes, hamview_weather_info_t *info);
static void populate_hourly_forecast(cJSON *hourly, const char *current_time_iso, hamview_weather_info_t *info);

static void update_time_sync_flag(bool synced)
{
    s_time_synced = synced;
    if (!s_info_mutex) {
        return;
    }
    xSemaphoreTake(s_info_mutex, portMAX_DELAY);
    s_info.time_synced = synced;
    xSemaphoreGive(s_info_mutex);
}

static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    update_time_sync_flag(true);
    ESP_LOGI(TAG, "time synchronized");
}

static void ensure_sntp_started(void)
{
    if (s_sntp_started) {
        return;
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time.nist.gov");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    update_time_sync_flag(false);
    sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started using time.nist.gov");
}

static char *http_get_json(const char *url, int *out_status)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        if (out_status) {
            *out_status = -1;
        }
        return NULL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        if (out_status) {
            *out_status = -1;
        }
        return NULL;
    }

    int status_code = esp_http_client_fetch_headers(client);
    size_t buf_size = 4096;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (out_status) {
            *out_status = -1;
        }
        return NULL;
    }

    size_t read_len = 0;
    while (true) {
        if (read_len >= buf_size - 1) {
            size_t new_size = buf_size * 2;
            char *tmp = realloc(buffer, new_size);
            if (!tmp) {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                if (out_status) {
                    *out_status = -1;
                }
                return NULL;
            }
            buffer = tmp;
            buf_size = new_size;
        }
        int r = esp_http_client_read(client, buffer + read_len, buf_size - read_len - 1);
        if (r < 0) {
            free(buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            if (out_status) {
                *out_status = -1;
            }
            return NULL;
        }
        if (r == 0) {
            break;
        }
        read_len += r;
    }
    buffer[read_len] = '\0';

    status_code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (out_status) {
        *out_status = status_code;
    }

    if (status_code != 200) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static bool url_encode_component(const char *src, char *dst, size_t dst_len)
{
    if (!src || !dst || dst_len == 0) {
        return false;
    }
    size_t write = 0;
    const char hex[] = "0123456789ABCDEF";
    while (*src && write + 1 < dst_len) {
        unsigned char c = (unsigned char)(*src);
        bool keep = (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~');
        if (keep) {
            dst[write++] = (char)c;
        } else {
            if (write + 3 >= dst_len) {
                break;
            }
            dst[write++] = '%';
            dst[write++] = hex[(c >> 4) & 0x0F];
            dst[write++] = hex[c & 0x0F];
        }
        src++;
    }
    dst[write] = '\0';
    return *src == '\0';
}

static void format_temperature(char *dst, size_t dst_len, double value, char unit)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!isfinite(value)) {
        dst[0] = '\0';
        return;
    }
    int rounded = (int)((value >= 0.0) ? (value + 0.5) : (value - 0.5));
    snprintf(dst, dst_len, "%d%c", rounded, unit);
}

static void format_percentage(char *dst, size_t dst_len, double value)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!isfinite(value)) {
        dst[0] = '\0';
        return;
    }
    int rounded = (int)((value >= 0.0) ? (value + 0.5) : (value - 0.5));
    snprintf(dst, dst_len, "%d%%", rounded);
}

static void format_speed(char *dst, size_t dst_len, double value)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!isfinite(value)) {
        dst[0] = '\0';
        return;
    }
    int rounded = (int)((value >= 0.0) ? (value + 0.5) : (value - 0.5));
    snprintf(dst, dst_len, "%d mph", rounded);
}

static void format_observation_time(const char *iso8601, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!iso8601) {
        dst[0] = '\0';
        return;
    }
    const char *sep = strchr(iso8601, 'T');
    if (sep && strlen(sep) >= 6) {
        snprintf(dst, dst_len, "%.*s", 5, sep + 1);
        return;
    }
    strlcpy(dst, iso8601, dst_len);
}

static bool find_hourly_value(cJSON *hourly, const char *field_name, const char *target_time, double *out_value)
{
    if (!hourly || !field_name || !target_time || !out_value) {
        return false;
    }
    cJSON *time_arr = cJSON_GetObjectItem(hourly, "time");
    cJSON *value_arr = cJSON_GetObjectItem(hourly, field_name);
    if (!cJSON_IsArray(time_arr) || !cJSON_IsArray(value_arr)) {
        return false;
    }
    int count = cJSON_GetArraySize(time_arr);
    if (cJSON_GetArraySize(value_arr) != count) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        cJSON *time_item = cJSON_GetArrayItem(time_arr, i);
        if (!cJSON_IsString(time_item) || !time_item->valuestring) {
            continue;
        }
        if (strcmp(time_item->valuestring, target_time) == 0) {
            cJSON *value_item = cJSON_GetArrayItem(value_arr, i);
            if (cJSON_IsNumber(value_item)) {
                *out_value = value_item->valuedouble;
                return true;
            }
            break;
        }
    }
    return false;
}

static const char *weather_code_to_text(int code)
{
    switch (code) {
        case 0:
            return "Clear";
        case 1:
            return "Mostly clear";
        case 2:
            return "Partly cloudy";
        case 3:
            return "Overcast";
        case 45:
        case 48:
            return "Fog";
        case 51:
        case 53:
        case 55:
            return "Drizzle";
        case 56:
        case 57:
            return "Freezing drizzle";
        case 61:
        case 63:
        case 65:
            return "Rain";
        case 66:
        case 67:
            return "Freezing rain";
        case 71:
        case 73:
        case 75:
            return "Snow";
        case 77:
            return "Snow grains";
        case 80:
        case 81:
        case 82:
            return "Rain showers";
        case 85:
        case 86:
            return "Snow showers";
        case 95:
            return "Thunderstorm";
        case 96:
        case 99:
            return "Thunderstorm with hail";
        default:
            return "Unknown";
    }
}

static esp_err_t geocode_location(const char *query, double *out_lat, double *out_lon, char *display_name, size_t display_len)
{
    if (!query || !out_lat || !out_lon || !display_name) {
        return ESP_ERR_INVALID_ARG;
    }

    char encoded[96];
    if (!url_encode_component(query, encoded, sizeof(encoded))) {
        return ESP_ERR_INVALID_SIZE;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json",
             encoded);

    int status = 0;
    char *payload = http_get_json(url, &status);
    if (!payload) {
        return (status == 404) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *results = cJSON_GetObjectItem(root, "results");
    cJSON *first = (cJSON_IsArray(results)) ? cJSON_GetArrayItem(results, 0) : NULL;
    if (!cJSON_IsObject(first)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *lat_node = cJSON_GetObjectItem(first, "latitude");
    cJSON *lon_node = cJSON_GetObjectItem(first, "longitude");
    if (!cJSON_IsNumber(lat_node) || !cJSON_IsNumber(lon_node)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out_lat = lat_node->valuedouble;
    *out_lon = lon_node->valuedouble;

    const char *name = NULL;
    const char *admin1 = NULL;
    const char *country = NULL;
    cJSON *name_node = cJSON_GetObjectItem(first, "name");
    if (cJSON_IsString(name_node) && name_node->valuestring) {
        name = name_node->valuestring;
    }
    cJSON *admin_node = cJSON_GetObjectItem(first, "admin1");
    if (cJSON_IsString(admin_node) && admin_node->valuestring) {
        admin1 = admin_node->valuestring;
    }
    cJSON *country_node = cJSON_GetObjectItem(first, "country");
    if (cJSON_IsString(country_node) && country_node->valuestring) {
        country = country_node->valuestring;
    }

    display_name[0] = '\0';
    if (name && *name) {
        strlcpy(display_name, name, display_len);
    }
    if (admin1 && *admin1) {
        if (display_name[0]) {
            strlcat(display_name, ", ", display_len);
        }
        strlcat(display_name, admin1, display_len);
    }
    if (country && *country) {
        if (display_name[0]) {
            strlcat(display_name, ", ", display_len);
        }
        strlcat(display_name, country, display_len);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static void fetch_weather_alerts(double latitude, double longitude, hamview_weather_info_t *info)
{
    if (!info) {
        return;
    }
    info->alert_count = 0;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/warnings?latitude=%.4f&longitude=%.4f&timezone=auto",
             latitude, longitude);

    int status = 0;
    char *payload = http_get_json(url, &status);
    if (!payload) {
        return;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        return;
    }

    cJSON *warnings = cJSON_GetObjectItem(root, "warnings");
    if (cJSON_IsArray(warnings)) {
        size_t idx = 0;
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, warnings) {
            if (idx >= 3) {
                break;
            }
            const char *headline = NULL;
            const char *event = NULL;
            const char *region = NULL;

            cJSON *headline_node = cJSON_GetObjectItem(entry, "headline");
            if (cJSON_IsString(headline_node) && headline_node->valuestring) {
                headline = headline_node->valuestring;
            }
            cJSON *event_node = cJSON_GetObjectItem(entry, "event");
            if (cJSON_IsString(event_node) && event_node->valuestring) {
                event = event_node->valuestring;
            }
            cJSON *regions_node = cJSON_GetObjectItem(entry, "regions");
            if (cJSON_IsArray(regions_node) && cJSON_GetArraySize(regions_node) > 0) {
                cJSON *region_item = cJSON_GetArrayItem(regions_node, 0);
                if (cJSON_IsString(region_item) && region_item->valuestring) {
                    region = region_item->valuestring;
                }
            }

            const char *text = headline ? headline : event;
            if (!text || !*text) {
                continue;
            }

            char lowered[128];
            strncpy(lowered, text, sizeof(lowered) - 1);
            lowered[sizeof(lowered) - 1] = '\0';
            for (size_t c = 0; lowered[c]; ++c) {
                lowered[c] = (char)tolower((unsigned char)lowered[c]);
            }
            if (strstr(lowered, "thunder") || strstr(lowered, "lightning")) {
                info->lightning_warning = true;
            }
            if (strstr(lowered, "wind") || strstr(lowered, "gust")) {
                info->high_wind_warning = true;
            }

            if (region && *region) {
                snprintf(info->alerts[idx], sizeof(info->alerts[idx]), "%s (%s)", text, region);
            } else {
                strlcpy(info->alerts[idx], text, sizeof(info->alerts[idx]));
            }
            ++idx;
        }
        info->alert_count = idx;
    }

    cJSON_Delete(root);
}

static bool parse_local_iso8601(const char *iso, int *year, int *month, int *day, int *hour, int *minute, int *second)
{
    if (!iso) {
        return false;
    }

    int y = 0;
    int m = 0;
    int d = 0;
    int h = 0;
    int min = 0;
    int sec = 0;
    int matched = sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &sec);
    if (matched < 5) {
        return false;
    }
    if (matched == 5) {
        sec = 0;
    }
    if (year) *year = y;
    if (month) *month = m;
    if (day) *day = d;
    if (hour) *hour = h;
    if (minute) *minute = min;
    if (second) *second = sec;
    return true;
}

static bool local_datetime_to_epoch(int year, int month, int day, int hour, int minute, int second, int offset_minutes, uint32_t *epoch_out)
{
    if (!epoch_out || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    int64_t y = year;
    int64_t m = month;
    int64_t d = day;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }

    int64_t era = y / 400;
    if (y < 0 && y % 400) {
        era = (y - 399) / 400;
    }
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m - 3) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    int64_t seconds = days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + (int64_t)second;
    seconds -= (int64_t)offset_minutes * 60;
    if (seconds < 0) {
        seconds = 0;
    }
    if (seconds > UINT32_MAX) {
        seconds = UINT32_MAX;
    }

    *epoch_out = (uint32_t)seconds;
    return true;
}

static void populate_sun_times(cJSON *daily, int offset_minutes, hamview_weather_info_t *info)
{
    if (!daily || !info) {
        return;
    }

    info->sun_times_valid = false;
    info->sun_times_count = 0;
    memset(info->sunrise_epoch, 0, sizeof(info->sunrise_epoch));
    memset(info->sunset_epoch, 0, sizeof(info->sunset_epoch));
    memset(info->sunrise_minutes, 0, sizeof(info->sunrise_minutes));
    memset(info->sunset_minutes, 0, sizeof(info->sunset_minutes));

    if (!cJSON_IsObject(daily)) {
        return;
    }
    cJSON *sunrise_arr = cJSON_GetObjectItem(daily, "sunrise");
    cJSON *sunset_arr = cJSON_GetObjectItem(daily, "sunset");
    if (!cJSON_IsArray(sunrise_arr) || !cJSON_IsArray(sunset_arr)) {
        return;
    }

    int sunrise_count = cJSON_GetArraySize(sunrise_arr);
    int sunset_count = cJSON_GetArraySize(sunset_arr);
    if (sunrise_count <= 0 || sunset_count <= 0) {
        return;
    }

    int count = sunrise_count < sunset_count ? sunrise_count : sunset_count;
    if (count > (int)HAMVIEW_WEATHER_SUN_TIMES) {
        count = (int)HAMVIEW_WEATHER_SUN_TIMES;
    }

    for (int i = 0; i < count; ++i) {
        cJSON *sunrise_item = cJSON_GetArrayItem(sunrise_arr, i);
        cJSON *sunset_item = cJSON_GetArrayItem(sunset_arr, i);
        if (!cJSON_IsString(sunrise_item) || !cJSON_IsString(sunset_item)) {
            continue;
        }

        int sr_year = 0;
        int sr_month = 0;
        int sr_day = 0;
        int sr_hour = 0;
        int sr_min = 0;
        int sr_sec = 0;
        if (!parse_local_iso8601(sunrise_item->valuestring, &sr_year, &sr_month, &sr_day, &sr_hour, &sr_min, &sr_sec)) {
            continue;
        }

        int ss_year = 0;
        int ss_month = 0;
        int ss_day = 0;
        int ss_hour = 0;
        int ss_min = 0;
        int ss_sec = 0;
        if (!parse_local_iso8601(sunset_item->valuestring, &ss_year, &ss_month, &ss_day, &ss_hour, &ss_min, &ss_sec)) {
            continue;
        }

        uint32_t sunrise_epoch = 0;
        uint32_t sunset_epoch = 0;
        if (!local_datetime_to_epoch(sr_year, sr_month, sr_day, sr_hour, sr_min, sr_sec, offset_minutes, &sunrise_epoch)) {
            continue;
        }
        if (!local_datetime_to_epoch(ss_year, ss_month, ss_day, ss_hour, ss_min, ss_sec, offset_minutes, &sunset_epoch)) {
            continue;
        }

        size_t idx = info->sun_times_count;
        if (idx >= HAMVIEW_WEATHER_SUN_TIMES) {
            break;
        }
        info->sunrise_epoch[idx] = sunrise_epoch;
        info->sunset_epoch[idx] = sunset_epoch;
        info->sunrise_minutes[idx] = (uint16_t)(sr_hour * 60 + sr_min);
        info->sunset_minutes[idx] = (uint16_t)(ss_hour * 60 + ss_min);
        info->sun_times_count++;
    }

    info->sun_times_valid = info->sun_times_count > 0;
}

static void populate_hourly_forecast(cJSON *hourly, const char *current_time_iso, hamview_weather_info_t *info)
{
    if (!hourly || !info) {
        return;
    }

    info->forecast_valid = false;
    info->forecast_count = 0;
    memset(info->forecast, 0, sizeof(info->forecast));

    cJSON *time_arr = cJSON_GetObjectItem(hourly, "time");
    if (!cJSON_IsArray(time_arr)) {
        return;
    }

    int total = cJSON_GetArraySize(time_arr);
    if (total <= 0) {
        return;
    }

    cJSON *temp_arr = cJSON_GetObjectItem(hourly, "temperature_2m");
    cJSON *apparent_arr = cJSON_GetObjectItem(hourly, "apparent_temperature");
    cJSON *precip_arr = cJSON_GetObjectItem(hourly, "precipitation_probability");
    cJSON *code_arr = cJSON_GetObjectItem(hourly, "weathercode");
    cJSON *wind_arr = cJSON_GetObjectItem(hourly, "windspeed_10m");

    int start_idx = 0;
    bool start_found = false;
    if (current_time_iso && *current_time_iso) {
        for (int i = 0; i < total; ++i) {
            cJSON *time_item = cJSON_GetArrayItem(time_arr, i);
            if (!cJSON_IsString(time_item) || !time_item->valuestring) {
                continue;
            }
            int cmp = strcmp(time_item->valuestring, current_time_iso);
            if (cmp >= 0) {
                start_idx = i;
                start_found = true;
                break;
            }
        }
    }

    if (!start_found) {
        if (total > HAMVIEW_WEATHER_HOURLY_COUNT) {
            start_idx = total - HAMVIEW_WEATHER_HOURLY_COUNT;
        } else {
            start_idx = 0;
        }
    }

    size_t written = 0;
    for (int idx = start_idx; idx < total && written < HAMVIEW_WEATHER_HOURLY_COUNT; ++idx) {
        cJSON *time_item = cJSON_GetArrayItem(time_arr, idx);
        if (!cJSON_IsString(time_item) || !time_item->valuestring) {
            continue;
        }

        hamview_weather_hourly_entry_t *entry = &info->forecast[written];
        memset(entry, 0, sizeof(*entry));

        int sr_hour = 0;
        int sr_minute = 0;
        if (parse_local_iso8601(time_item->valuestring, NULL, NULL, NULL, &sr_hour, &sr_minute, NULL)) {
            snprintf(entry->time_local, sizeof(entry->time_local), "%02d:%02d", sr_hour, sr_minute);
        } else {
            strncpy(entry->time_local, time_item->valuestring, sizeof(entry->time_local) - 1);
            entry->time_local[sizeof(entry->time_local) - 1] = '\0';
        }

        double temp_val = NAN;
        if (cJSON_IsArray(temp_arr)) {
            cJSON *temp_item = cJSON_GetArrayItem(temp_arr, idx);
            if (cJSON_IsNumber(temp_item)) {
                temp_val = temp_item->valuedouble;
            }
        }
        if (!isfinite(temp_val) && cJSON_IsArray(apparent_arr)) {
            cJSON *temp_item = cJSON_GetArrayItem(apparent_arr, idx);
            if (cJSON_IsNumber(temp_item)) {
                temp_val = temp_item->valuedouble;
            }
        }
        if (isfinite(temp_val)) {
            format_temperature(entry->temp_f, sizeof(entry->temp_f), temp_val, 'F');
            double temp_c = (temp_val - 32.0) * (5.0 / 9.0);
            format_temperature(entry->temp_c, sizeof(entry->temp_c), temp_c, 'C');
        }

        if (cJSON_IsArray(precip_arr)) {
            cJSON *precip_item = cJSON_GetArrayItem(precip_arr, idx);
            if (cJSON_IsNumber(precip_item)) {
                double percent = precip_item->valuedouble;
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;
                entry->precip_percent = (uint8_t)(percent + 0.5);
            }
        }

        double wind_val = NAN;
        if (cJSON_IsArray(wind_arr)) {
            cJSON *wind_item = cJSON_GetArrayItem(wind_arr, idx);
            if (cJSON_IsNumber(wind_item)) {
                wind_val = wind_item->valuedouble;
            }
        }
        if (isfinite(wind_val)) {
            if (wind_val < 0) wind_val = 0;
            if (wind_val > 255) wind_val = 255;
            entry->sustained_wind_mph = (uint8_t)(wind_val + 0.5);
            if (wind_val >= 30.0) {
                info->high_wind_warning = true;
            }
        }

        if (cJSON_IsArray(code_arr)) {
            cJSON *code_item = cJSON_GetArrayItem(code_arr, idx);
            if (cJSON_IsNumber(code_item)) {
                int code = code_item->valueint;
                if (code == 95 || code == 96 || code == 99) {
                    entry->lightning_risk = true;
                    info->lightning_warning = true;
                }
            }
        }

        written++;
    }

    info->forecast_count = written;
    info->forecast_valid = written > 0;
}

static void set_error(const char *fmt, ...)
{
    if (!s_info_mutex) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    xSemaphoreTake(s_info_mutex, portMAX_DELAY);
    vsnprintf(s_info.last_error, sizeof(s_info.last_error), fmt, args);
    s_info.has_data = false;
    xSemaphoreGive(s_info_mutex);
    va_end(args);
    if (s_info.last_error[0]) {
        hamview_event_log_append("weather", "%s", s_info.last_error);
    }
}

static void clear_error(void)
{
    if (!s_info_mutex) {
        return;
    }
    xSemaphoreTake(s_info_mutex, portMAX_DELAY);
    s_info.last_error[0] = '\0';
    xSemaphoreGive(s_info_mutex);
}

static esp_err_t fetch_weather_now(void)
{
    hamview_settings_t settings;
    hamview_settings_get(&settings);

    if (strlen(settings.weather_zip) == 0) {
        set_error("Set weather ZIP in Settings");
        return ESP_ERR_INVALID_ARG;
    }

    char location_query[64];
    strlcpy(location_query, settings.weather_zip, sizeof(location_query));
    char search_query[64];
    strlcpy(search_query, settings.weather_zip, sizeof(search_query));
    bool numeric_zip = true;
    if (strlen(settings.weather_zip) == 5) {
        for (size_t i = 0; i < 5; ++i) {
            if (!isdigit((unsigned char)settings.weather_zip[i])) {
                numeric_zip = false;
                break;
            }
        }
    } else {
        numeric_zip = false;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    char display_location[64];
    esp_err_t geo_err = geocode_location(search_query, &latitude, &longitude, display_location, sizeof(display_location));
    if (geo_err != ESP_OK) {
        if (geo_err == ESP_ERR_NOT_FOUND) {
            set_error("Location not found");
        } else if (geo_err == ESP_ERR_NO_MEM) {
            set_error("Geocode out of memory");
        } else {
            set_error("Geocode failed");
        }
        return geo_err;
    }

    char forecast_url[512];
    snprintf(forecast_url, sizeof(forecast_url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&timezone=auto"
             "&temperature_unit=fahrenheit&windspeed_unit=mph&current_weather=true"
             "&hourly=temperature_2m,apparent_temperature,relativehumidity_2m,precipitation_probability,weathercode,windspeed_10m"
             "&daily=sunrise,sunset&past_days=1&forecast_days=2",
             latitude, longitude);

    int status = 0;
    char *payload = http_get_json(forecast_url, &status);
    if (!payload) {
        set_error("Forecast HTTP %d", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        set_error("Forecast parse error");
        return ESP_FAIL;
    }

    hamview_weather_info_t info = {0};
    info.time_synced = s_time_synced;
    if (s_info_mutex) {
        xSemaphoreTake(s_info_mutex, portMAX_DELAY);
        if (s_info.timezone_valid) {
            info.timezone_valid = s_info.timezone_valid;
            info.timezone_offset_minutes = s_info.timezone_offset_minutes;
            strlcpy(info.timezone_name, s_info.timezone_name, sizeof(info.timezone_name));
        }
        info.time_synced = s_info.time_synced;
        xSemaphoreGive(s_info_mutex);
    }

    if (display_location[0] != '\0') {
        strlcpy(info.location, display_location, sizeof(info.location));
    } else {
        strlcpy(info.location, location_query, sizeof(info.location));
    }

    cJSON *tz_node = cJSON_GetObjectItem(root, "timezone");
    if (cJSON_IsString(tz_node) && tz_node->valuestring) {
        strlcpy(info.timezone_name, tz_node->valuestring, sizeof(info.timezone_name));
        info.timezone_valid = true;
    }
    cJSON *offset_node = cJSON_GetObjectItem(root, "utc_offset_seconds");
    if (cJSON_IsNumber(offset_node)) {
        info.timezone_offset_minutes = offset_node->valueint / 60;
        info.timezone_valid = true;
    }
    if (info.timezone_valid) {
        ensure_sntp_started();
    }

    cJSON *current_weather = cJSON_GetObjectItem(root, "current_weather");
    if (!cJSON_IsObject(current_weather)) {
        cJSON_Delete(root);
        set_error("No current weather");
        return ESP_FAIL;
    }

    double temp_f = NAN;
    cJSON *temp_node = cJSON_GetObjectItem(current_weather, "temperature");
    if (cJSON_IsNumber(temp_node)) {
        temp_f = temp_node->valuedouble;
        format_temperature(info.temperature_f, sizeof(info.temperature_f), temp_f, 'F');
        double temp_c = (temp_f - 32.0) * (5.0 / 9.0);
        format_temperature(info.temperature_c, sizeof(info.temperature_c), temp_c, 'C');
    }

    cJSON *wind_node = cJSON_GetObjectItem(current_weather, "windspeed");
    if (cJSON_IsNumber(wind_node)) {
        double wind_mph = wind_node->valuedouble;
        format_speed(info.wind_mph, sizeof(info.wind_mph), wind_mph);
        if (wind_mph >= 30.0) {
            info.high_wind_warning = true;
        }
    }

    const char *time_str = NULL;
    cJSON *time_node = cJSON_GetObjectItem(current_weather, "time");
    if (cJSON_IsString(time_node) && time_node->valuestring) {
        time_str = time_node->valuestring;
        format_observation_time(time_str, info.observation_time, sizeof(info.observation_time));
    }

    int weather_code = -1;
    cJSON *code_node = cJSON_GetObjectItem(current_weather, "weathercode");
    if (cJSON_IsNumber(code_node)) {
        weather_code = code_node->valueint;
        if (weather_code == 95 || weather_code == 96 || weather_code == 99) {
            info.lightning_warning = true;
        }
    }
    strlcpy(info.condition, weather_code_to_text(weather_code), sizeof(info.condition));

    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (cJSON_IsObject(hourly) && time_str) {
        double humidity = NAN;
        if (find_hourly_value(hourly, "relativehumidity_2m", time_str, &humidity)) {
            format_percentage(info.humidity, sizeof(info.humidity), humidity);
        }
        double apparent = NAN;
        if (find_hourly_value(hourly, "apparent_temperature", time_str, &apparent)) {
            format_temperature(info.feels_like_f, sizeof(info.feels_like_f), apparent, 'F');
        }
    }

    if (cJSON_IsObject(hourly)) {
        populate_hourly_forecast(hourly, time_str, &info);
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (info.timezone_valid) {
        populate_sun_times(daily, info.timezone_offset_minutes, &info);
    }

    fetch_weather_alerts(latitude, longitude, &info);

    info.has_data = true;
    info.last_update_epoch = (uint32_t)time(NULL);
    info.last_error[0] = '\0';

    cJSON_Delete(root);

    info.time_synced = s_time_synced;
    xSemaphoreTake(s_info_mutex, portMAX_DELAY);
    s_info = info;
    xSemaphoreGive(s_info_mutex);
    hamview_event_log_append("weather", "Updated %s (%u alerts)",
                             info.location[0] ? info.location : "weather",
                             (unsigned)info.alert_count);
    return ESP_OK;
}

static void weather_task(void *arg)
{
    (void)arg;
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(s_weather_events, WEATHER_EVENT_FETCH | WEATHER_EVENT_SETTINGS,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(WEATHER_FETCH_INTERVAL_MS));
        bool trigger = (bits & (WEATHER_EVENT_FETCH | WEATHER_EVENT_SETTINGS)) != 0;
        if (!trigger) {
            trigger = true; // timer elapsed
        }
        if (!trigger) {
            continue;
        }
        if (!s_wifi_connected || !s_has_ip) {
            continue;
        }
        hamview_settings_t settings;
        hamview_settings_get(&settings);
        if (strlen(settings.weather_zip) == 0) {
            set_error("Set weather ZIP in Settings");
            continue;
        }
        esp_err_t err = fetch_weather_now();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "weather fetch failed: %s", esp_err_to_name(err));
        } else {
            clear_error();
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    (void)base;
    if (id != VIEW_EVENT_WIFI_ST || !event_data) {
        return;
    }
    const struct view_data_wifi_st *st = (const struct view_data_wifi_st *)event_data;
    s_wifi_connected = st->is_connected;
    if (s_wifi_connected) {
        esp_netif_ip_info_t info = {0};
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif) {
            netif = esp_netif_get_handle_from_ifkey("WIFI_STA");
        }
        if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
            s_has_ip = true;
            ensure_sntp_started();
            xEventGroupSetBits(s_weather_events, WEATHER_EVENT_FETCH);
        } else {
            s_has_ip = false;
        }
    } else {
        s_has_ip = false;
        update_time_sync_flag(false);
    }
}

esp_err_t hamview_weather_init(void)
{
    if (!s_info_mutex) {
        s_info_mutex = xSemaphoreCreateMutex();
    }
    if (!s_weather_events) {
        s_weather_events = xEventGroupCreate();
    }
    if (!s_info_mutex || !s_weather_events) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_info, 0, sizeof(s_info));

    esp_err_t err = esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }

    if (!s_weather_task) {
        xEventGroupSetBits(s_weather_events, WEATHER_EVENT_FETCH);
        const uint32_t weather_stack_words = 8192;
        xTaskCreatePinnedToCore(weather_task, "hamview_weather", weather_stack_words, NULL, 4, &s_weather_task, tskNO_AFFINITY);
    }

    return ESP_OK;
}

void hamview_weather_on_settings_updated(void)
{
    if (s_weather_events) {
        xEventGroupSetBits(s_weather_events, WEATHER_EVENT_SETTINGS);
    }
    hamview_event_log_append("weather", "Settings updated");
}

bool hamview_weather_get(hamview_weather_info_t *out)
{
    if (!out || !s_info_mutex) {
        return false;
    }
    xSemaphoreTake(s_info_mutex, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_info_mutex);
    return out->has_data;
}

void hamview_weather_request_refresh(void)
{
    if (s_weather_events) {
        xEventGroupSetBits(s_weather_events, WEATHER_EVENT_FETCH);
    }
    hamview_event_log_append("weather", "Manual refresh requested");
}
