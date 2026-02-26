#include "hamview_event_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static hamview_event_log_entry_t s_entries[HAMVIEW_EVENT_LOG_CAPACITY];
static size_t s_count = 0;
static size_t s_write_index = 0;
static SemaphoreHandle_t s_mutex = NULL;

static void ensure_mutex(void)
{
    if (s_mutex) {
        return;
    }
    s_mutex = xSemaphoreCreateMutex();
}

static uint32_t current_timestamp(void)
{
    time_t now = time(NULL);
    if (now > 0) {
        return (uint32_t)now;
    }
    uint64_t us = esp_timer_get_time();
    return (uint32_t)(us / 1000000ULL);
}

void hamview_event_log_append(const char *source, const char *fmt, ...)
{
    ensure_mutex();
    if (!s_mutex) {
        return;
    }
    char message[sizeof(((hamview_event_log_entry_t *)0)->message)];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (message[0] == '\0') {
        return;
    }

    hamview_event_log_entry_t entry = {0};
    entry.timestamp = current_timestamp();
    if (source && *source) {
        strlcpy(entry.source, source, sizeof(entry.source));
    } else {
        strlcpy(entry.source, "sys", sizeof(entry.source));
    }
    strlcpy(entry.message, message, sizeof(entry.message));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_entries[s_write_index] = entry;
    s_write_index = (s_write_index + 1) % HAMVIEW_EVENT_LOG_CAPACITY;
    if (s_count < HAMVIEW_EVENT_LOG_CAPACITY) {
        s_count++;
    }
    xSemaphoreGive(s_mutex);
}

size_t hamview_event_log_get(hamview_event_log_entry_t *out, size_t max_entries)
{
    if (!out || max_entries == 0) {
        return 0;
    }
    ensure_mutex();
    if (!s_mutex) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t count = s_count;
    if (count > max_entries) {
        count = max_entries;
    }
    size_t start = (s_write_index + HAMVIEW_EVENT_LOG_CAPACITY - s_count) % HAMVIEW_EVENT_LOG_CAPACITY;
    for (size_t i = 0; i < count; ++i) {
        size_t index = (start + i) % HAMVIEW_EVENT_LOG_CAPACITY;
        out[i] = s_entries[index];
    }
    xSemaphoreGive(s_mutex);
    return count;
}

void hamview_event_log_clear(void)
{
    ensure_mutex();
    if (!s_mutex) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    s_write_index = 0;
    xSemaphoreGive(s_mutex);
}
