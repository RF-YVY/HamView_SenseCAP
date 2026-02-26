#ifndef HAMVIEW_SCREEN_H
#define HAMVIEW_SCREEN_H

#include <stdint.h>
#include "lvgl.h"

void hamview_screen_init(void);
void hamview_screen_configure(uint16_t timeout_minutes);
void hamview_screen_record_activity(void);
void hamview_screen_handle_button(uint32_t key, lv_indev_state_t state);
void hamview_screen_timer_tick(void);
void hamview_screen_set_brightness(uint8_t percent);

#endif
