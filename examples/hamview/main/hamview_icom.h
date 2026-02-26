#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    char device_name[32];
    char device_addr[18];
    uint64_t freq_hz;
    char mode[12];
    uint8_t s_meter_raw;
    uint64_t last_update_ms;
} hamview_icom_state_t;

void hamview_icom_init(void);
void hamview_icom_reset(void);
void hamview_icom_set_connected(bool connected);
void hamview_icom_set_device(const char *name, const char *addr);
void hamview_icom_on_civ_bytes(const uint8_t *data, size_t len);
bool hamview_icom_connect(const char *addr);
void hamview_icom_disconnect(void);
bool hamview_icom_get_state(hamview_icom_state_t *out);
void hamview_icom_set_civ_wifi(bool enable, const char *ip, uint16_t port, const char *username, const char *password);

#ifdef __cplusplus
}
#endif
