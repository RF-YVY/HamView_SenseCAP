#ifndef HAMVIEW_BACKEND_H
#define HAMVIEW_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hamview_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAMVIEW_MAX_SPOTS 5

#define HAMVIEW_ACTIVITY_BUCKET_COUNT 6
#define HAMVIEW_ACTIVITY_BUCKET_MINUTES 5
#define HAMVIEW_ACTIVITY_MODE_COUNT 4
#define HAMVIEW_ACTIVITY_HOURLY_COUNT 24

typedef struct {
    char callsign[16];
    char frequency[16];
    char mode[12];
    char spotter[16];
    char time_utc[32];
    char dxcc[32];
    char state[24];
    char country[32];
    char continent[8];
    char comment[96];
    uint32_t age_seconds;
    bool is_new;
} hamview_spot_t;

typedef struct {
    bool wifi_connected;
    bool hamalert_connected;
    bool using_rest;
    char ip_address[16];
    char last_error[96];
} hamview_status_t;

typedef struct {
    uint16_t timeline_buckets[HAMVIEW_ACTIVITY_BUCKET_COUNT];
    size_t bucket_count;
    uint32_t bucket_minutes;
    uint16_t mode_counts[HAMVIEW_ACTIVITY_MODE_COUNT];
    size_t mode_count;
    uint32_t total_spots;
    uint16_t hourly_counts[HAMVIEW_ACTIVITY_HOURLY_COUNT];
    size_t hourly_count;
    bool has_day_night;
    bool hourly_is_day[HAMVIEW_ACTIVITY_HOURLY_COUNT];
} hamview_activity_summary_t;

esp_err_t hamview_backend_init(void);
void hamview_backend_on_settings_updated(void);
size_t hamview_backend_get_spots(hamview_spot_t *out, size_t max_out);
void hamview_backend_get_status(hamview_status_t *out);
void hamview_backend_get_activity_summary(hamview_activity_summary_t *out);

#ifdef __cplusplus
}
#endif

#endif
