#include "hamview_backend.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "indicator/view_data.h"
#include "indicator/config.h"
#include "hamview_settings.h"
#include "hamview_alert.h"
#include "hamview_event_log.h"
#include "hamview_weather.h"

extern esp_err_t esp_crt_bundle_attach(void *conf);

#define HAMALERT_HOST "hamalert.org"
#define HAMALERT_PORT 7300
#define REST_URL_BASE "https://hamalert.org/api.php"
#define HTTP_PORT 8080
#define FETCH_INTERVAL_MS 30000
#define KEEPALIVE_INTERVAL_MS 120000
#define HAMVIEW_HISTORY_MAX_SPOTS 512

#define BACKEND_EVENT_SETTINGS (1 << 0)

typedef struct {
    hamview_spot_t spot;
    uint64_t received_us;
} stored_spot_t;

static const char *TAG = "hamview_backend";

static stored_spot_t s_spots[HAMVIEW_MAX_SPOTS];
static size_t s_spot_count = 0;
static SemaphoreHandle_t s_spot_mutex;
static stored_spot_t *s_history_spots = NULL;
static size_t s_history_capacity = 0;
static size_t s_history_count = 0;

static bool s_wifi_connected = false;
static bool s_hamalert_connected = false;
static bool s_use_rest = false;
static bool s_sntp_started = false;
static char s_ip_address[16] = "";
static char s_last_error[96] = "";
static SemaphoreHandle_t s_status_mutex;

static EventGroupHandle_t s_backend_events;
static TaskHandle_t s_backend_task_handle = NULL;
static httpd_handle_t s_httpd = NULL;

static hamview_settings_t s_settings;
static uint64_t s_last_fetch_us = 0;

static void set_last_error(const char *fmt, ...)
{
    if (!s_status_mutex) return;
    va_list args;
    va_start(args, fmt);
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, args);
    xSemaphoreGive(s_status_mutex);
    va_end(args);
    if (s_last_error[0]) {
        hamview_event_log_append("backend", "%s", s_last_error);
    }
}

static uint64_t now_us(void)
{
    return esp_timer_get_time();
}
static void prune_expired_spots_locked(uint64_t now_us);
static uint64_t spot_ttl_us(void);
static void prune_history_locked(uint64_t now_us);
static uint32_t history_window_minutes(void);
static uint32_t classify_activity_mode(const char *mode_text);
static esp_err_t ensure_history_buffer(void);
static size_t history_capacity(void);

static void copy_status(hamview_status_t *out)
{
    if (!out) return;
    if (!s_status_mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    out->wifi_connected = s_wifi_connected;
    out->hamalert_connected = s_hamalert_connected;
    out->using_rest = s_use_rest;
    strlcpy(out->ip_address, s_ip_address, sizeof(out->ip_address));
    strlcpy(out->last_error, s_last_error, sizeof(out->last_error));
    xSemaphoreGive(s_status_mutex);
}

static void set_hamalert_connected(bool connected)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_hamalert_connected = connected;
    if (!connected) {
        s_last_fetch_us = 0;
    }
    xSemaphoreGive(s_status_mutex);
    hamview_event_log_append("backend", "HamAlert %s", connected ? "connected" : "offline");
}

static void set_ip_address(const char *ip_str)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    strlcpy(s_ip_address, ip_str ? ip_str : "", sizeof(s_ip_address));
    xSemaphoreGive(s_status_mutex);
    hamview_event_log_append("backend", "IP %s", (ip_str && *ip_str) ? ip_str : "cleared");
}

static void update_spot(const hamview_spot_t *spot)
{
    if (!spot) return;
    if (!s_spot_mutex) return;
    xSemaphoreTake(s_spot_mutex, portMAX_DELAY);

    uint64_t now = now_us();
    hamview_spot_t alert_candidate = {0};
    bool trigger_alert = false;
    for (int i = (int)HAMVIEW_MAX_SPOTS - 1; i > 0; --i) {
        s_spots[i] = s_spots[i - 1];
    }
    s_spots[0].spot = *spot;
    s_spots[0].received_us = now;
    s_spots[0].spot.is_new = true;

    size_t history_cap = history_capacity();
    if (history_cap > 0) {
        for (size_t i = history_cap; i > 1; --i) {
            s_history_spots[i - 1] = s_history_spots[i - 2];
        }
        s_history_spots[0].spot = *spot;
        s_history_spots[0].spot.is_new = true;
        s_history_spots[0].received_us = now;
        if (s_history_count < history_cap) {
            ++s_history_count;
        }
    } else {
        s_history_count = 0;
    }

    if (hamview_alert_is_high_priority(&s_spots[0].spot)) {
        alert_candidate = s_spots[0].spot;
        trigger_alert = true;
    }

    if (s_spot_count < HAMVIEW_MAX_SPOTS) {
        s_spot_count++;
    }

    for (size_t i = 1; i < s_spot_count; ++i) {
        s_spots[i].spot.is_new = false;
    }

    prune_expired_spots_locked(now);
    prune_history_locked(now);

    xSemaphoreGive(s_spot_mutex);

    if (trigger_alert) {
        hamview_alert_notify(&alert_candidate);
    }
}

static uint64_t spot_ttl_us(void)
{
    uint32_t minutes = s_settings.spot_ttl_minutes;
    if (minutes == 0) {
        return 0;
    }
    if (minutes > 720) {
        minutes = 720;
    }
    return (uint64_t)minutes * 60ULL * 1000000ULL;
}

static uint32_t history_window_minutes(void)
{
    uint32_t minimum = HAMVIEW_ACTIVITY_BUCKET_COUNT * HAMVIEW_ACTIVITY_BUCKET_MINUTES;
    uint32_t daily_min = HAMVIEW_ACTIVITY_HOURLY_COUNT * 60;
    if (minimum < daily_min) {
        minimum = daily_min;
    }
    uint32_t window = s_settings.spot_ttl_minutes;
    if (window == 0) {
        window = minimum;
    } else if (window < minimum) {
        window = minimum;
    }
    if (window > daily_min) {
        window = daily_min;
    }
    return window;
}

static size_t history_capacity(void)
{
    return (s_history_spots && s_history_capacity > 0) ? s_history_capacity : 0;
}

static esp_err_t ensure_history_buffer(void)
{
    if (s_history_spots) {
        return ESP_OK;
    }

    size_t capacity = (size_t)HAMVIEW_HISTORY_MAX_SPOTS;
    stored_spot_t *buffer = NULL;

    while (capacity >= 64 && !buffer) {
        size_t bytes = capacity * sizeof(stored_spot_t);
        buffer = (stored_spot_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buffer) {
            buffer = (stored_spot_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (buffer) {
            memset(buffer, 0, bytes);
            s_history_spots = buffer;
            s_history_capacity = capacity;
            s_history_count = 0;
            ESP_LOGI(TAG, "History buffer allocated for %zu spots", capacity);
            return ESP_OK;
        }
        capacity /= 2;
    }

    ESP_LOGE(TAG, "Failed to allocate history buffer (requested %u spots)", HAMVIEW_HISTORY_MAX_SPOTS);
    s_history_capacity = 0;
    s_history_count = 0;
    return ESP_ERR_NO_MEM;
}

static void prune_history_locked(uint64_t now)
{
    size_t history_cap = history_capacity();
    if (history_cap == 0 || s_history_count == 0) {
        s_history_count = 0;
        return;
    }
    if (s_history_count > history_cap) {
        s_history_count = history_cap;
    }
    uint64_t window_us = (uint64_t)history_window_minutes() * 60ULL * 1000000ULL;
    size_t write = 0;
    for (size_t read = 0; read < s_history_count; ++read) {
        uint64_t age_us = (now >= s_history_spots[read].received_us)
                              ? (now - s_history_spots[read].received_us)
                              : 0;
        if (age_us <= window_us) {
            if (write != read) {
                s_history_spots[write] = s_history_spots[read];
            }
            ++write;
        }
    }
    if (write < s_history_count) {
        s_history_count = write;
    }
}

static uint32_t classify_activity_mode(const char *mode_text)
{
    char mode_upper[32];
    size_t len = strlcpy(mode_upper, mode_text ? mode_text : "", sizeof(mode_upper));
    for (size_t i = 0; i < len; ++i) {
        mode_upper[i] = (char)toupper((unsigned char)mode_upper[i]);
    }

    if (strcmp(mode_upper, "CW") == 0) {
        return 0;
    }
    if (strstr(mode_upper, "FT8") || strstr(mode_upper, "FT4") || strstr(mode_upper, "RTTY") ||
        strstr(mode_upper, "PSK") || strstr(mode_upper, "DIG") || strstr(mode_upper, "JS8") ||
        strstr(mode_upper, "OLIVIA") || strstr(mode_upper, "WSPR")) {
        return 1;
    }
    if (strstr(mode_upper, "SSB") || strstr(mode_upper, "USB") || strstr(mode_upper, "LSB") ||
        strstr(mode_upper, "AM") || strstr(mode_upper, "FM") || strstr(mode_upper, "VOICE")) {
        return 2;
    }
    return 3;
}

static void prune_expired_spots_locked(uint64_t now)
{
    uint64_t ttl = spot_ttl_us();
    if (ttl == 0 || s_spot_count == 0) {
        return;
    }

    size_t write = 0;
    for (size_t read = 0; read < s_spot_count; ++read) {
        uint64_t age_us = (now >= s_spots[read].received_us)
                              ? (now - s_spots[read].received_us)
                              : 0;
        if (age_us <= ttl) {
            if (write != read) {
                s_spots[write] = s_spots[read];
            }
            ++write;
        }
    }

    if (write < s_spot_count) {
        s_spot_count = write;
    }
}

size_t hamview_backend_get_spots(hamview_spot_t *out, size_t max_out)
{
    if (!out || max_out == 0) {
        return 0;
    }
    if (!s_spot_mutex) return 0;

    xSemaphoreTake(s_spot_mutex, portMAX_DELAY);
    uint64_t now = now_us();
    prune_expired_spots_locked(now);
    size_t count = s_spot_count;
    if (count > max_out) count = max_out;
    for (size_t i = 0; i < count; ++i) {
        out[i] = s_spots[i].spot;
        uint64_t diff_us = (now >= s_spots[i].received_us)
                               ? (now - s_spots[i].received_us)
                               : 0;
        out[i].age_seconds = (uint32_t)(diff_us / 1000000ULL);
    }
    xSemaphoreGive(s_spot_mutex);
    return count;
}

void hamview_backend_get_status(hamview_status_t *out)
{
    copy_status(out);
}

void hamview_backend_get_activity_summary(hamview_activity_summary_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->bucket_count = HAMVIEW_ACTIVITY_BUCKET_COUNT;
    out->bucket_minutes = HAMVIEW_ACTIVITY_BUCKET_MINUTES;
    out->mode_count = HAMVIEW_ACTIVITY_MODE_COUNT;
    out->hourly_count = HAMVIEW_ACTIVITY_HOURLY_COUNT;

    if (!s_spot_mutex) {
        return;
    }

    const uint64_t bucket_span_us = (uint64_t)HAMVIEW_ACTIVITY_BUCKET_MINUTES * 60ULL * 1000000ULL;
    const uint64_t hour_span_us = 60ULL * 60ULL * 1000000ULL;

    xSemaphoreTake(s_spot_mutex, portMAX_DELAY);
    uint64_t now = now_us();
    prune_history_locked(now);
    size_t history_cap = history_capacity();
    size_t history_count = s_history_count;
    if (history_cap > 0 && history_count > 0) {
        for (size_t i = 0; i < history_count; ++i) {
            const stored_spot_t *entry = &s_history_spots[i];
            uint64_t age_us = (now >= entry->received_us) ? (now - entry->received_us) : 0;
            uint32_t bucket_from_now = bucket_span_us > 0 ? (uint32_t)(age_us / bucket_span_us) : 0;
            if (bucket_from_now < HAMVIEW_ACTIVITY_BUCKET_COUNT) {
                size_t bucket_index = HAMVIEW_ACTIVITY_BUCKET_COUNT - 1 - bucket_from_now;
                if (bucket_index < HAMVIEW_ACTIVITY_BUCKET_COUNT) {
                    out->timeline_buckets[bucket_index]++;
                }
            }
            uint32_t hour_from_now = hour_span_us > 0 ? (uint32_t)(age_us / hour_span_us) : 0;
            if (hour_from_now < HAMVIEW_ACTIVITY_HOURLY_COUNT) {
                size_t hour_index = HAMVIEW_ACTIVITY_HOURLY_COUNT - 1 - hour_from_now;
                if (hour_index < HAMVIEW_ACTIVITY_HOURLY_COUNT) {
                    out->hourly_counts[hour_index]++;
                }
            }
            uint32_t mode_index = classify_activity_mode(entry->spot.mode);
            if (mode_index < HAMVIEW_ACTIVITY_MODE_COUNT) {
                out->mode_counts[mode_index]++;
            }
            out->total_spots++;
        }
    }
    xSemaphoreGive(s_spot_mutex);

    hamview_weather_info_t weather = {0};
    if (hamview_weather_get(&weather) && weather.sun_times_valid && weather.sun_times_count > 0) {
        time_t epoch_now = time(NULL);
        if (epoch_now > 0) {
            bool any_pair = false;
            for (size_t i = 0; i < HAMVIEW_ACTIVITY_HOURLY_COUNT; ++i) {
                uint32_t hours_from_now = (uint32_t)(HAMVIEW_ACTIVITY_HOURLY_COUNT - 1 - i);
                time_t hour_start = epoch_now - (time_t)hours_from_now * 3600;
                time_t midpoint = hour_start + 1800;
                bool is_day = false;
                for (size_t j = 0; j < weather.sun_times_count; ++j) {
                    uint32_t sunrise = weather.sunrise_epoch[j];
                    uint32_t sunset = weather.sunset_epoch[j];
                    if (sunrise == 0 || sunset == 0) {
                        continue;
                    }
                    if (sunset <= sunrise) {
                        continue;
                    }
                    any_pair = true;
                    if ((time_t)sunrise <= midpoint && midpoint < (time_t)sunset) {
                        is_day = true;
                        break;
                    }
                }
                out->hourly_is_day[i] = is_day;
            }
            if (any_pair) {
                out->has_day_night = true;
            }
        }
    }
}

static void clear_spots(void)
{
    if (!s_spot_mutex) return;
    xSemaphoreTake(s_spot_mutex, portMAX_DELAY);
    memset(s_spots, 0, sizeof(s_spots));
    s_spot_count = 0;
    size_t hist_cap = history_capacity();
    if (s_history_spots && hist_cap > 0) {
        memset(s_history_spots, 0, hist_cap * sizeof(stored_spot_t));
    }
    s_history_count = 0;
    xSemaphoreGive(s_spot_mutex);
}

static void handle_wifi_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    if (id != VIEW_EVENT_WIFI_ST || !event_data) {
        return;
    }
    const struct view_data_wifi_st *st = (const struct view_data_wifi_st *)event_data;
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_wifi_connected = st->is_connected;
    xSemaphoreGive(s_status_mutex);

    if (st->is_connected) {
        set_last_error("");
        esp_netif_ip_info_t info = {0};
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif) {
            netif = esp_netif_get_handle_from_ifkey("WIFI_STA");
        }
        if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                     esp_ip4_addr_get_byte(&info.ip, 0),
                     esp_ip4_addr_get_byte(&info.ip, 1),
                     esp_ip4_addr_get_byte(&info.ip, 2),
                     esp_ip4_addr_get_byte(&info.ip, 3));
            set_ip_address(ip_str);
        }
        hamview_event_log_append("backend", "Wi-Fi connected");
    } else {
        set_ip_address("");
        set_hamalert_connected(false);
        hamview_event_log_append("backend", "Wi-Fi disconnected");
    }
}

static void add_field(char *dst, size_t dst_len, const cJSON *obj, const char *key, const char *fallback_key)
{
    const char *val = NULL;
    if (obj) {
        const cJSON *node = cJSON_GetObjectItemCaseSensitive(obj, key);
        if (cJSON_IsString(node) && node->valuestring) {
            val = node->valuestring;
        }
        if (!val && fallback_key) {
            node = cJSON_GetObjectItemCaseSensitive(obj, fallback_key);
            if (cJSON_IsString(node) && node->valuestring) {
                val = node->valuestring;
            }
        }
    }
    if (!val) {
        val = "";
    }
    strlcpy(dst, val, dst_len);
}

static void ensure_sntp_started(void)
{
    if (s_sntp_started) {
        return;
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time.nist.gov");
    sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started using time.nist.gov");
}

static bool is_time_valid(void)
{
    time_t now = 0;
    time(&now);
    return now > 1700000000;
}

static void process_spot_json_obj(cJSON *obj)
{
    if (!obj) return;
    hamview_spot_t spot = {0};
    add_field(spot.callsign, sizeof(spot.callsign), obj, "callsign", "dx");
    if (strlen(spot.callsign) == 0) {
        return;
    }
    add_field(spot.frequency, sizeof(spot.frequency), obj, "frequency", "freq");
    add_field(spot.mode, sizeof(spot.mode), obj, "mode", NULL);
    add_field(spot.spotter, sizeof(spot.spotter), obj, "spotter", "de");
    add_field(spot.time_utc, sizeof(spot.time_utc), obj, "time", NULL);
    add_field(spot.dxcc, sizeof(spot.dxcc), obj, "dxcc", "country");
    add_field(spot.state, sizeof(spot.state), obj, "state", "us_state");
    add_field(spot.country, sizeof(spot.country), obj, "country", "dxcc");
    add_field(spot.continent, sizeof(spot.continent), obj, "continent", "cont");
    add_field(spot.comment, sizeof(spot.comment), obj, "comment", "comments");

    if (strlen(spot.frequency) > 0) {
        double freq_val = atof(spot.frequency);
        if (freq_val > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.3f", freq_val);
            strlcpy(spot.frequency, buf, sizeof(spot.frequency));
        }
    }

    update_spot(&spot);
}

static void process_spot_json(const char *json)
{
    if (!json || strlen(json) == 0) {
        return;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return;
    }
    if (cJSON_IsArray(root)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, root) {
            if (cJSON_IsObject(item)) {
                process_spot_json_obj(item);
            }
        }
    } else if (cJSON_IsObject(root)) {
        cJSON *spots = cJSON_GetObjectItem(root, "spots");
        if (cJSON_IsArray(spots)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, spots) {
                if (cJSON_IsObject(item)) {
                    process_spot_json_obj(item);
                }
            }
        } else {
            process_spot_json_obj(root);
        }
    }
    cJSON_Delete(root);
    s_last_fetch_us = now_us();
}

static void telnet_close(int *sock)
{
    if (sock && *sock >= 0) {
        shutdown(*sock, SHUT_RDWR);
        close(*sock);
        *sock = -1;
    }
    set_hamalert_connected(false);
}

static bool telnet_send_line(int sock, const char *line)
{
    if (sock < 0 || !line) return false;
    size_t len = strlen(line);
    if (send(sock, line, len, 0) < 0) {
        return false;
    }
    if (send(sock, "\r\n", 2, 0) < 0) {
        return false;
    }
    return true;
}

static int telnet_connect(void)
{
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(HAMALERT_HOST, "7300", &hints, &res);
    if (err != 0 || !res) {
        set_last_error("DNS error: %d", err);
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        set_last_error("socket error: %d", errno);
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        set_last_error("connect failed: %d", errno);
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    struct timeval tv = {
        .tv_sec = 2,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return sock;
}

static bool telnet_login(int sock, const hamview_settings_t *settings)
{
    char buffer[256];
    int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        set_last_error("No response after connect");
        return false;
    }

    if (!telnet_send_line(sock, settings->username)) {
        set_last_error("send username failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        set_last_error("No prompt for password");
        return false;
    }

    if (!telnet_send_line(sock, settings->password)) {
        set_last_error("send password failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        set_last_error("Auth response missing");
        return false;
    }

    if (!telnet_send_line(sock, "set/json")) {
        set_last_error("set/json failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // Drain any acknowledgements
    recv(sock, buffer, sizeof(buffer) - 1, 0);
    return true;
}

static void telnet_loop(void)
{
    int sock = -1;
    char recv_buf[1024];
    size_t recv_len = 0;
    uint64_t last_keepalive = now_us();

    while (s_wifi_connected && !s_use_rest) {
        if (sock < 0) {
            telnet_close(&sock);
            clear_spots();
            if (strlen(s_settings.username) == 0 || strlen(s_settings.password) == 0) {
                set_last_error("Set HamAlert credentials");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            sock = telnet_connect();
            if (sock < 0) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            if (!telnet_login(sock, &s_settings)) {
                telnet_close(&sock);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            set_hamalert_connected(true);
            set_last_error("");
            recv_len = 0;
            last_keepalive = now_us();
            ESP_LOGI(TAG, "HamAlert telnet connected");
        }

        int n = recv(sock, recv_buf + recv_len, sizeof(recv_buf) - recv_len - 1, 0);
        if (n > 0) {
            recv_len += n;
            recv_buf[recv_len] = '\0';

            char *line_start = recv_buf;
            char *newline = NULL;
            while ((newline = strchr(line_start, '\n')) != NULL) {
                *newline = '\0';
                char *line = line_start;
                while (*line == '\r' || *line == '\n') line++;
                if (strlen(line) > 0 && line[0] == '{') {
                    process_spot_json(line);
                }
                line_start = newline + 1;
            }
            recv_len = strlen(line_start);
            memmove(recv_buf, line_start, recv_len + 1);
            last_keepalive = now_us();
        } else if (n == 0) {
            set_last_error("HamAlert closed connection");
            telnet_close(&sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                uint64_t now = now_us();
                if (now - last_keepalive > KEEPALIVE_INTERVAL_MS * 1000ULL) {
                    telnet_send_line(sock, "");
                    last_keepalive = now;
                }
                EventBits_t bits = xEventGroupWaitBits(s_backend_events, BACKEND_EVENT_SETTINGS, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
                if (bits & BACKEND_EVENT_SETTINGS) {
                    ESP_LOGI(TAG, "Settings updated, reconnecting");
                    telnet_close(&sock);
                }
            } else {
                set_last_error("recv error %d", errno);
                telnet_close(&sock);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
    }
    telnet_close(&sock);
}

static esp_err_t rest_fetch_once(void) __attribute__((unused));
static void rest_loop(void) __attribute__((unused));

static esp_err_t rest_fetch_once(void)
{
    if (strlen(s_settings.username) == 0 || strlen(s_settings.password) == 0) {
        set_last_error("Set HamAlert credentials");
        return ESP_FAIL;
    }

    if (!is_time_valid()) {
        ensure_sntp_started();
        set_last_error("Waiting for time sync");
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), REST_URL_BASE "?user=%s&pass=%s", s_settings.username, s_settings.password);
    if (strlen(s_settings.filter_callsign) > 0) {
        strlcat(url, "&callsign=", sizeof(url));
        strlcat(url, s_settings.filter_callsign, sizeof(url));
    }
    if (strlen(s_settings.filter_band) > 0) {
        strlcat(url, "&band=", sizeof(url));
        strlcat(url, s_settings.filter_band, sizeof(url));
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        set_last_error("http client init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        set_last_error("http open error %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_fetch_headers(client);
    if (status < 0) {
        set_last_error("fetch headers failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char *buffer = malloc(4096);
    if (!buffer) {
        set_last_error("malloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    size_t buf_size = 4096;
    int read_len = 0;
    while (true) {
        int r = esp_http_client_read(client, buffer + read_len, buf_size - read_len - 1);
        if (r < 0) {
            set_last_error("read failed");
            free(buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (r == 0) {
            break;
        }
        read_len += r;
        if ((size_t)read_len >= buf_size - 1) {
            size_t new_size = buf_size * 2;
            char *tmp = realloc(buffer, new_size);
            if (!tmp) {
                set_last_error("realloc failed");
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            buffer = tmp;
            buf_size = new_size;
        }
    }
    buffer[read_len] = '\0';

    status = esp_http_client_get_status_code(client);
    if (status != 200) {
        set_last_error("REST status %d", status);
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    process_spot_json(buffer);
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    set_last_error("");
    set_hamalert_connected(true);
    return ESP_OK;
}

static void rest_loop(void)
{
    set_hamalert_connected(false);
    while (s_wifi_connected && s_use_rest) {
        esp_err_t err = rest_fetch_once();
        if (err == ESP_OK) {
            set_hamalert_connected(true);
        }
        EventBits_t bits = xEventGroupWaitBits(s_backend_events, BACKEND_EVENT_SETTINGS, pdTRUE, pdFALSE, pdMS_TO_TICKS(FETCH_INTERVAL_MS));
        if (bits & BACKEND_EVENT_SETTINGS) {
            ESP_LOGI(TAG, "Settings updated (REST loop)");
        }
    }
    set_hamalert_connected(false);
}

static esp_err_t http_send_spots(httpd_req_t *req)
{
    hamview_spot_t spots[HAMVIEW_MAX_SPOTS];
    size_t count = hamview_backend_get_spots(spots, HAMVIEW_MAX_SPOTS);

    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < count; ++i) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "callsign", spots[i].callsign);
        cJSON_AddStringToObject(obj, "frequency", spots[i].frequency);
        cJSON_AddStringToObject(obj, "mode", spots[i].mode);
        cJSON_AddStringToObject(obj, "spotter", spots[i].spotter);
        cJSON_AddStringToObject(obj, "time", spots[i].time_utc);
        cJSON_AddStringToObject(obj, "dxcc", spots[i].dxcc);
        cJSON_AddStringToObject(obj, "state", spots[i].state);
        cJSON_AddStringToObject(obj, "country", spots[i].country);
        cJSON_AddStringToObject(obj, "continent", spots[i].continent);
        cJSON_AddStringToObject(obj, "comment", spots[i].comment);
        cJSON_AddNumberToObject(obj, "age", spots[i].age_seconds);
        cJSON_AddBoolToObject(obj, "isNew", spots[i].is_new);
        cJSON_AddItemToArray(root, obj);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json error");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_sendstr(req, json);
    free(json);
    return resp;
}

static esp_err_t http_send_status(httpd_req_t *req)
{
    hamview_status_t status;
    hamview_backend_get_status(&status);

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "wifiConnected", status.wifi_connected);
    cJSON_AddBoolToObject(obj, "hamalertConnected", status.hamalert_connected);
    cJSON_AddBoolToObject(obj, "usingRest", status.using_rest);
    cJSON_AddStringToObject(obj, "ip", status.ip_address);
    cJSON_AddStringToObject(obj, "lastError", status.last_error);
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json error");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_sendstr(req, json);
    free(json);
    return resp;
}

static esp_err_t http_handler_spots(httpd_req_t *req)
{
    return http_send_spots(req);
}

static esp_err_t http_handler_test(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "HamView backend OK");
}

static esp_err_t http_handler_root(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>HamView</title>"
        "<style>body{font-family:Courier,monospace;background:#000;color:#0f0;padding:20px;}"
        "h1{color:#0ff;text-align:center;}table{width:100%;border-collapse:collapse;}"
        "th,td{padding:8px;border-bottom:1px solid #333;}th{color:#ff0;}"
        ".status{margin-bottom:20px;}</style>"
        "<script>async function refresh(){const spots=await fetch('/api/spots').then(r=>r.json());"
        "const status=await fetch('/api/status').then(r=>r.json());"
        "document.getElementById('ip').textContent=status.ip;"
        "document.getElementById('hamalert').textContent=status.hamalertConnected?'Connected':'Offline';"
        "document.getElementById('mode').textContent=status.usingRest?'REST':'Telnet';"
        "document.getElementById('error').textContent=status.lastError||'None';"
        "const body=document.getElementById('tbody');body.innerHTML='';"
        "if(spots.length===0){body.innerHTML='<tr><td colspan=9>No spots yet</td></tr>';}"
        "spots.forEach(s=>{const row=document.createElement('tr');"
        "row.innerHTML=`<td>${s.callsign}</td><td>${s.frequency}</td><td>${s.mode}</td><td>${s.spotter}</td><td>${s.time}</td><td>${s.continent}</td><td>${s.dxcc}</td><td>${s.age}s</td><td>${s.comment}</td>`;body.appendChild(row);});"
        "}setInterval(refresh,5000);window.onload=refresh;</script></head><body>"
        "<h1>HamView Spots</h1><div class='status'>IP: <span id='ip'></span> | HamAlert: <span id='hamalert'></span> | Mode: <span id='mode'></span> | Error: <span id='error'></span></div>"
        "<table><thead><tr><th>Call</th><th>Freq</th><th>Mode</th><th>Spotter</th><th>Time</th><th>Cont</th><th>DXCC</th><th>Age</th><th>Comment</th></tr></thead><tbody id='tbody'></tbody></table></body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static esp_err_t http_handler_status(httpd_req_t *req)
{
    return http_send_status(req);
}

static void start_http_server(void)
{
    if (s_httpd) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.ctrl_port = HTTP_PORT + 1;
    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = http_handler_root, .user_ctx = NULL};
        httpd_uri_t uri_spots = {.uri = "/api/spots", .method = HTTP_GET, .handler = http_handler_spots, .user_ctx = NULL};
        httpd_uri_t uri_status = {.uri = "/api/status", .method = HTTP_GET, .handler = http_handler_status, .user_ctx = NULL};
        httpd_uri_t uri_test = {.uri = "/test", .method = HTTP_GET, .handler = http_handler_test, .user_ctx = NULL};
        httpd_register_uri_handler(s_httpd, &uri_root);
        httpd_register_uri_handler(s_httpd, &uri_spots);
        httpd_register_uri_handler(s_httpd, &uri_status);
        httpd_register_uri_handler(s_httpd, &uri_test);
        ESP_LOGI(TAG, "HTTP server started on %d", HTTP_PORT);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}

static void backend_task(void *arg)
{
    (void)arg;
    start_http_server();

    while (1) {
        hamview_settings_get(&s_settings);
        s_use_rest = false;
        if (!s_wifi_connected) {
            set_hamalert_connected(false);
            EventBits_t bits = xEventGroupWaitBits(s_backend_events, BACKEND_EVENT_SETTINGS, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            (void)bits;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        telnet_loop();
        EventBits_t bits = xEventGroupWaitBits(s_backend_events, BACKEND_EVENT_SETTINGS, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        if (bits & BACKEND_EVENT_SETTINGS) {
            ESP_LOGI(TAG, "Backend settings flag noted");
        }
    }
}

esp_err_t hamview_backend_init(void)
{
    if (!s_spot_mutex) {
        s_spot_mutex = xSemaphoreCreateMutex();
    }
    if (!s_status_mutex) {
        s_status_mutex = xSemaphoreCreateMutex();
    }
    if (!s_backend_events) {
        s_backend_events = xEventGroupCreate();
    }

    hamview_settings_get(&s_settings);
    s_use_rest = false;
    if (ensure_history_buffer() != ESP_OK) {
        ESP_LOGW(TAG, "History buffer unavailable; extended analytics limited");
    }

    hamview_alert_init();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, handle_wifi_event, NULL, NULL));

    if (!s_backend_task_handle) {
        xTaskCreatePinnedToCore(backend_task, "hamview_backend", 8192, NULL, 5, &s_backend_task_handle, tskNO_AFFINITY);
    }
    return ESP_OK;
}

void hamview_backend_on_settings_updated(void)
{
    hamview_settings_get(&s_settings);
    s_use_rest = false;
    if (s_spot_mutex) {
        xSemaphoreTake(s_spot_mutex, portMAX_DELAY);
        uint64_t now = now_us();
        prune_expired_spots_locked(now);
        prune_history_locked(now);
        xSemaphoreGive(s_spot_mutex);
    }
    xEventGroupSetBits(s_backend_events, BACKEND_EVENT_SETTINGS);
}
