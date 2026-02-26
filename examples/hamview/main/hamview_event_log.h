#ifndef HAMVIEW_EVENT_LOG_H
#define HAMVIEW_EVENT_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAMVIEW_EVENT_LOG_CAPACITY 64

typedef struct {
    uint32_t timestamp;
    char source[16];
    char message[128];
} hamview_event_log_entry_t;

void hamview_event_log_append(const char *source, const char *fmt, ...);
size_t hamview_event_log_get(hamview_event_log_entry_t *out, size_t max_entries);
void hamview_event_log_clear(void);

#ifdef __cplusplus
}
#endif

#endif
