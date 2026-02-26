#ifndef HAMVIEW_INDICATOR_WIFI_H
#define HAMVIEW_INDICATOR_WIFI_H

#include "config.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

int indicator_wifi_init(void);
void indicator_wifi_notify_status(void);

#ifdef __cplusplus
}
#endif

#endif
