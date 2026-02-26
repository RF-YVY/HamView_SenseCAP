#ifndef LV_PORT_H
#define LV_PORT_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void lv_port_init(void);
void lv_port_sem_take(void);
void lv_port_sem_give(void);
bool lv_port_is_in_lvgl_task(void);

#ifdef __cplusplus
}
#endif

#endif
