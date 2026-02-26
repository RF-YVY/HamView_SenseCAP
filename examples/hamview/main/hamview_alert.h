#ifndef HAMVIEW_ALERT_H
#define HAMVIEW_ALERT_H

#include <stdbool.h>

#include "hamview_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

void hamview_alert_init(void);
bool hamview_alert_is_high_priority(const hamview_spot_t *spot);
void hamview_alert_notify(const hamview_spot_t *spot);
void hamview_alert_set_muted(bool muted);
bool hamview_alert_is_muted(void);
void hamview_alert_toggle_muted(void);

#ifdef __cplusplus
}
#endif

#endif
