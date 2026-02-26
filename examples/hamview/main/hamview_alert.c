#include "hamview_alert.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_i2s.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "hamview_screen.h"
#include "hamview_settings.h"
#include "hamview_event_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ALERT_SAMPLE_RATE       (16000)
#define ALERT_FREQ_HZ           (1400)
#define ALERT_DURATION_MS       (220)
#define ALERT_BUFFER_SAMPLES    (256)
#define ALERT_COOLDOWN_US       (5ULL * 1000000ULL)
#define ALERT_VOLUME_SCALE      (26000)
#define ALERT_I2S_PORT          I2S_NUM_1

static const char *TAG = "hamview_alert";

static SemaphoreHandle_t s_audio_mutex;
static bool s_initialized = false;
static bool s_audio_ready = false;
static bool s_audio_failed = false;
static uint64_t s_last_alert_us = 0;
static bool s_muted = false;

static bool ensure_initialized(void)
{
    if (!s_initialized) {
        s_audio_mutex = xSemaphoreCreateMutex();
        if (!s_audio_mutex) {
            ESP_LOGE(TAG, "audio mutex alloc failed");
            s_audio_failed = true;
            return false;
        }
        s_initialized = true;
    }
    return true;
}

static esp_err_t ensure_audio_ready(void)
{
    if (s_audio_failed) {
        return ESP_FAIL;
    }
    if (s_audio_ready) {
        return ESP_OK;
    }
    esp_err_t err = bsp_i2s_init(ALERT_I2S_PORT, ALERT_SAMPLE_RATE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s init failed: %s", esp_err_to_name(err));
        s_audio_failed = true;
        return err;
    }
    err = i2s_zero_dma_buffer(ALERT_I2S_PORT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s zero buffer failed: %s", esp_err_to_name(err));
    }
    s_audio_ready = true;
    return ESP_OK;
}

static void play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!ensure_initialized()) {
        return;
    }
    if (xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (ensure_audio_ready() != ESP_OK) {
        xSemaphoreGive(s_audio_mutex);
        return;
    }

    const size_t total_samples = (ALERT_SAMPLE_RATE * duration_ms) / 1000;
    const float step = (2.0f * (float)M_PI * (float)freq_hz) / (float)ALERT_SAMPLE_RATE;
    float phase = 0.0f;
    int16_t samples[ALERT_BUFFER_SAMPLES * 2];

    size_t remaining = total_samples;
    while (remaining > 0) {
        size_t chunk = remaining > ALERT_BUFFER_SAMPLES ? ALERT_BUFFER_SAMPLES : remaining;
        for (size_t i = 0; i < chunk; ++i) {
            float value = sinf(phase);
            int16_t sample = (int16_t)(value * (float)ALERT_VOLUME_SCALE);
            samples[2 * i] = sample;
            samples[2 * i + 1] = sample;
            phase += step;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        size_t bytes_to_write = chunk * 2 * sizeof(int16_t);
        size_t bytes_written = 0;
        esp_err_t err = i2s_write(ALERT_I2S_PORT, samples, bytes_to_write, &bytes_written, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_written != bytes_to_write) {
            ESP_LOGW(TAG, "i2s write failed: %s (%u/%u)", esp_err_to_name(err), (unsigned)bytes_written, (unsigned)bytes_to_write);
            break;
        }
        remaining -= chunk;
    }
    i2s_zero_dma_buffer(ALERT_I2S_PORT);
    xSemaphoreGive(s_audio_mutex);
}

static bool equals_ignore_case(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        char ca = (char)toupper((unsigned char)*a);
        char cb = (char)toupper((unsigned char)*b);
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static bool contains_ignore_case(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return false;
    }
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return false;
    }
    size_t hay_len = strlen(haystack);
    if (hay_len < needle_len) {
        return false;
    }
    for (size_t i = 0; i <= hay_len - needle_len; ++i) {
        size_t j = 0;
        while (j < needle_len) {
            char c1 = (char)toupper((unsigned char)haystack[i + j]);
            char c2 = (char)toupper((unsigned char)needle[j]);
            if (c1 != c2) {
                break;
            }
            ++j;
        }
        if (j == needle_len) {
            return true;
        }
    }
    return false;
}

static void uppercase_copy(char *dst, size_t dst_len, const char *src)
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

static void trim_in_place(char *text)
{
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    size_t start = 0;
    while (start < len && isspace((unsigned char)text[start])) {
        start++;
    }
    size_t end = len;
    while (end > start && isspace((unsigned char)text[end - 1])) {
        end--;
    }
    if (start > 0 || end < len) {
        memmove(text, text + start, end - start);
        text[end - start] = '\0';
    }
}

static bool token_list_match(const char *list, const char *value, bool substring)
{
    if (!list || list[0] == '\0' || !value || value[0] == '\0') {
        return false;
    }

    char list_copy[128];
    strlcpy(list_copy, list, sizeof(list_copy));

    char value_upper[128];
    uppercase_copy(value_upper, sizeof(value_upper), value);

    char *token = strtok(list_copy, ",;\n");
    while (token) {
        trim_in_place(token);
        if (*token) {
            char token_upper[128];
            uppercase_copy(token_upper, sizeof(token_upper), token);
            if (substring) {
                if (strstr(value_upper, token_upper)) {
                    return true;
                }
            } else {
                if (strcmp(value_upper, token_upper) == 0) {
                    return true;
                }
            }
        }
        token = strtok(NULL, ",;\n");
    }
    return false;
}

static bool band_filter_matches(const hamview_spot_t *spot, const char *filter)
{
    if (!spot || !filter || filter[0] == '\0') {
        return false;
    }
    double freq_mhz = atof(spot->frequency);
    if (freq_mhz <= 0.0) {
        return false;
    }

    struct band_range {
        const char *name;
        double min_mhz;
        double max_mhz;
    };

    static const struct band_range bands[] = {
        {"160", 1.8, 2.0},
        {"80", 3.3, 4.1},
        {"60", 5.2, 5.5},
        {"40", 6.8, 7.5},
        {"30", 9.9, 10.2},
        {"20", 13.9, 14.5},
        {"17", 17.9, 18.2},
        {"15", 20.9, 21.5},
        {"12", 24.8, 25.1},
        {"10", 27.9, 30.1},
        {"6", 50.0, 54.5},
        {"2", 144.0, 149.0},
        {"70", 420.0, 451.0},
    };

    for (size_t i = 0; i < sizeof(bands) / sizeof(bands[0]); ++i) {
        if (contains_ignore_case(filter, bands[i].name)) {
            if (freq_mhz >= bands[i].min_mhz && freq_mhz <= bands[i].max_mhz) {
                return true;
            }
        }
    }
    return false;
}

static bool has_priority_keyword(const hamview_spot_t *spot)
{
    static const char *KEYWORDS[] = {"ALERT", "DXPED", "DXPEDITION", "RARE", "SOTA", "POTA", "IOTA", "URGENT", "SPECIAL"};
    if (!spot) {
        return false;
    }
    if (spot->comment[0]) {
        if (strchr(spot->comment, '!')) {
            return true;
        }
        for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); ++i) {
            if (contains_ignore_case(spot->comment, KEYWORDS[i])) {
                return true;
            }
        }
    }
    return false;
}

void hamview_alert_init(void)
{
    ensure_initialized();
}

bool hamview_alert_is_high_priority(const hamview_spot_t *spot)
{
    if (!spot) {
        return false;
    }
    hamview_settings_t settings;
    hamview_settings_get(&settings);

    if (settings.alert_callsigns[0]) {
        if (token_list_match(settings.alert_callsigns, spot->callsign, false) ||
            token_list_match(settings.alert_callsigns, spot->spotter, false)) {
            return true;
        }
    }

    if (settings.alert_states[0]) {
        if (token_list_match(settings.alert_states, spot->state, false) ||
            token_list_match(settings.alert_states, spot->comment, true)) {
            return true;
        }
    }

    if (settings.alert_countries[0]) {
        if (token_list_match(settings.alert_countries, spot->country, true) ||
            token_list_match(settings.alert_countries, spot->dxcc, true) ||
            token_list_match(settings.alert_countries, spot->continent, false)) {
            return true;
        }
    }

    if (settings.filter_callsign[0]) {
        if (equals_ignore_case(spot->callsign, settings.filter_callsign) ||
            equals_ignore_case(spot->spotter, settings.filter_callsign) ||
            contains_ignore_case(spot->comment, settings.filter_callsign)) {
            return true;
        }
    }

    if (settings.filter_band[0]) {
        if (contains_ignore_case(spot->frequency, settings.filter_band) ||
            band_filter_matches(spot, settings.filter_band)) {
            return true;
        }
    }

    return has_priority_keyword(spot);
}

void hamview_alert_notify(const hamview_spot_t *spot)
{
    if (!spot) {
        return;
    }
    if (s_muted) {
        return;
    }
    hamview_alert_init();
    hamview_settings_t settings;
    hamview_settings_get(&settings);

    bool sound_enabled = settings.alert_sound_enabled;


    hamview_screen_record_activity();

    if (sound_enabled) {
        uint64_t now = esp_timer_get_time();
        if (now - s_last_alert_us < ALERT_COOLDOWN_US) {
            return;
        }
        s_last_alert_us = now;
        play_tone(ALERT_FREQ_HZ, ALERT_DURATION_MS);
    }
}

void hamview_alert_set_muted(bool muted)
{
    s_muted = muted;
    hamview_event_log_append("alerts", "Alerts %s", muted ? "muted" : "unmuted");
}

bool hamview_alert_is_muted(void)
{
    return s_muted;
}

void hamview_alert_toggle_muted(void)
{
    hamview_alert_set_muted(!s_muted);
}
