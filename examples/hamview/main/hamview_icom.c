#include "hamview_icom.h"

#include <string.h>
#include <errno.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <sys/select.h>
#include <stdlib.h>

#include "sdkconfig.h"
#if defined(__has_include)
#if __has_include("esp_bt.h") && defined(CONFIG_BT_BLE_ENABLED) && CONFIG_BT_BLE_ENABLED && defined(CONFIG_BT_BLUEDROID_ENABLED) && CONFIG_BT_BLUEDROID_ENABLED && defined(CONFIG_BT_BLE_42_FEATURES_SUPPORTED) && CONFIG_BT_BLE_42_FEATURES_SUPPORTED
#define HAMVIEW_ICOM_HAS_BLE 1
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#else
#define HAMVIEW_ICOM_HAS_BLE 0
#endif

#if __has_include("esp_spp_api.h") && defined(CONFIG_BT_CLASSIC_ENABLED) && CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_BT_SPP_ENABLED) && CONFIG_BT_SPP_ENABLED
#define HAMVIEW_ICOM_HAS_SPP 1
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "esp_gap_bt_api.h"
#else
#define HAMVIEW_ICOM_HAS_SPP 0
#endif
#else
#define HAMVIEW_ICOM_HAS_BLE 0
#define HAMVIEW_ICOM_HAS_SPP 0
#endif

#define CIV_MAX_FRAME 64
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char *TAG = "hamview_icom";

static hamview_icom_state_t s_state;
static SemaphoreHandle_t s_state_mutex;
static uint8_t s_civ_buf[CIV_MAX_FRAME];
static size_t s_civ_len = 0;
static TaskHandle_t s_civ_task_handle = NULL;
static bool s_civ_wifi_enabled = false;
static char s_civ_ip[32] = "";
static uint16_t s_civ_port = 50001;
static char s_civ_username[32] = "";
static char s_civ_password[32] = "";
static uint64_t s_civ_last_rx_ms = 0;

#define CIV_ADDR_IC705 0xA4
#define CIV_ADDR_CTRL  0xE1

#define ICOM_CTRL_PACKET_SIZE 0x10
#define ICOM_PING_PACKET_SIZE 0x15
#define ICOM_OPENCLOSE_SIZE 0x16
#define ICOM_TOKEN_SIZE 0x40
#define ICOM_STATUS_SIZE 0x50
#define ICOM_LOGIN_RESPONSE_SIZE 0x60
#define ICOM_LOGIN_SIZE 0x80
#define ICOM_CONNINFO_SIZE 0x90
#define ICOM_CAPS_HEADER_SIZE 0x42
#define ICOM_RADIO_CAP_SIZE 0x66
#define ICOM_CIV_HEADER_SIZE 0x15

#define ICOM_TOKEN_RENEW_MS 60000
#define ICOM_PING_PERIOD_MS 500
#define ICOM_IDLE_PERIOD_MS 100
#define ICOM_ARE_YOU_THERE_MS 500
#define ICOM_CIV_WATCHDOG_MS 2000

#define ICOM_TX_BUF_COUNT 16

#define ICOM_COMP_NAME "HAMVIEW"

typedef struct {
    uint8_t guid[16];
    uint8_t mac[6];
    bool use_guid;
    char name[32];
    uint8_t civ;
    uint32_t baudrate_be;
    uint16_t commoncap_be;
} icom_radio_cap_t;

typedef struct {
    uint16_t seq;
    uint16_t len;
    uint8_t data[256];
} icom_tx_entry_t;

static bool get_local_ipv4(uint8_t out[4])
{
    if (!out) {
        return false;
    }
    esp_netif_ip_info_t info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_STA");
    }
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    out[0] = esp_ip4_addr_get_byte(&info.ip, 0);
    out[1] = esp_ip4_addr_get_byte(&info.ip, 1);
    out[2] = esp_ip4_addr_get_byte(&info.ip, 2);
    out[3] = esp_ip4_addr_get_byte(&info.ip, 3);
    return true;
}

static uint32_t make_my_id(const uint8_t ip[4], uint16_t port)
{
    return ((uint32_t)ip[2] << 24) | ((uint32_t)ip[3] << 16) | port;
}

static void write_u16_le(uint8_t *buf, size_t off, uint16_t value)
{
    buf[off] = (uint8_t)(value & 0xff);
    buf[off + 1] = (uint8_t)((value >> 8) & 0xff);
}

static void write_u32_le(uint8_t *buf, size_t off, uint32_t value)
{
    buf[off] = (uint8_t)(value & 0xff);
    buf[off + 1] = (uint8_t)((value >> 8) & 0xff);
    buf[off + 2] = (uint8_t)((value >> 16) & 0xff);
    buf[off + 3] = (uint8_t)((value >> 24) & 0xff);
}

static void write_u16_be(uint8_t *buf, size_t off, uint16_t value)
{
    buf[off] = (uint8_t)((value >> 8) & 0xff);
    buf[off + 1] = (uint8_t)(value & 0xff);
}

static void write_u32_be(uint8_t *buf, size_t off, uint32_t value)
{
    buf[off] = (uint8_t)((value >> 24) & 0xff);
    buf[off + 1] = (uint8_t)((value >> 16) & 0xff);
    buf[off + 2] = (uint8_t)((value >> 8) & 0xff);
    buf[off + 3] = (uint8_t)(value & 0xff);
}

static uint16_t read_u16_be(const uint8_t *buf, size_t off)
{
    return (uint16_t)((buf[off] << 8) | buf[off + 1]);
}

static uint16_t read_u16_le(const uint8_t *buf, size_t off)
{
    return (uint16_t)(buf[off] | (buf[off + 1] << 8));
}

static uint32_t read_u32_le(const uint8_t *buf, size_t off)
{
    return (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
}

static uint32_t read_u32_be(const uint8_t *buf, size_t off)
{
    return ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) | ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
}

static void icom_passcode(const char *in, uint8_t out[16])
{
    static const uint8_t sequence[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
        0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
        0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
        0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
        0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    memset(out, 0, 16);
    if (!in) {
        return;
    }
    size_t len = strlen(in);
    if (len > 16) {
        len = 16;
    }
    for (size_t i = 0; i < len; ++i) {
        int p = (unsigned char)in[i] + (int)i;
        if (p > 126) {
            p = 32 + (p % 127);
        }
        out[i] = sequence[p];
    }
}

#if HAMVIEW_ICOM_HAS_BLE
static bool s_ble_initialized = false;
static bool s_ble_connect_pending = false;
static esp_bd_addr_t s_target_addr = {0};
static bool s_target_addr_set = false;
static esp_gatt_if_t s_gattc_if = ESP_GATT_IF_NONE;
static esp_ble_addr_type_t s_target_addr_type = BLE_ADDR_TYPE_PUBLIC;
static bool s_spp_server_started = false;
static uint16_t s_conn_id = 0;
static const char *SPP_DEVICE_NAME = "HAMVIEW ICOM METER";

static void bda_to_string(const uint8_t *addr, char *out, size_t out_len)
{
    if (!addr || !out || out_len < 18) {
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}
static uint16_t s_service_start = 0;
static uint16_t s_service_end = 0;
static uint16_t s_char_tx_handle = 0;
static uint16_t s_char_rx_handle = 0;

static const uint8_t IC705_SERVICE_UUID[16] = { 0x00, 0x00, 0x00, 0x01, 0x38, 0x2a, 0x11, 0xe9, 0xa2, 0x92, 0x00, 0x02, 0xa5, 0x00, 0x00, 0x00 };
static const uint8_t IC705_RX_UUID[16] =      { 0x00, 0x00, 0x00, 0x02, 0x38, 0x2a, 0x11, 0xe9, 0xa2, 0x92, 0x00, 0x02, 0xa5, 0x00, 0x00, 0x00 };
static const uint8_t IC705_TX_UUID[16] =      { 0x00, 0x00, 0x00, 0x03, 0x38, 0x2a, 0x11, 0xe9, 0xa2, 0x92, 0x00, 0x02, 0xa5, 0x00, 0x00, 0x00 };

static bool addr_from_string(const char *addr, esp_bd_addr_t out)
{
    if (!addr || !out) {
        return false;
    }
    unsigned int b[6] = {0};
    if (sscanf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; ++i) {
        out[i] = (uint8_t)b[i];
    }
    return true;
}

static void gattc_search_service(esp_gatt_if_t gattc_if)
{
    esp_bt_uuid_t uuid = { .len = ESP_UUID_LEN_128 };
    memcpy(uuid.uuid.uuid128, IC705_SERVICE_UUID, sizeof(IC705_SERVICE_UUID));
    esp_ble_gattc_search_service(gattc_if, s_conn_id, &uuid);
}

static void gattc_enable_notify(esp_gatt_if_t gattc_if)
{
    if (!s_char_tx_handle) {
        return;
    }
    esp_ble_gattc_register_for_notify(gattc_if, s_target_addr, s_char_tx_handle);
}

static void gattc_discover_chars(esp_gatt_if_t gattc_if)
{
    if (!s_service_start || !s_service_end) {
        return;
    }

    uint16_t count = 0;
    esp_bt_uuid_t uuid = { .len = ESP_UUID_LEN_128 };

    memcpy(uuid.uuid.uuid128, IC705_TX_UUID, sizeof(IC705_TX_UUID));
    if (esp_ble_gattc_get_attr_count(gattc_if, s_conn_id, ESP_GATT_DB_CHARACTERISTIC, s_service_start, s_service_end, 0, &count) == ESP_OK && count > 0) {
        esp_gattc_char_elem_t result[4];
        uint16_t found = count > 4 ? 4 : count;
        if (esp_ble_gattc_get_char_by_uuid(gattc_if, s_conn_id, s_service_start, s_service_end, uuid, result, &found) == ESP_OK && found > 0) {
            s_char_tx_handle = result[0].char_handle;
        }
    }

    memcpy(uuid.uuid.uuid128, IC705_RX_UUID, sizeof(IC705_RX_UUID));
    count = 0;
    if (esp_ble_gattc_get_attr_count(gattc_if, s_conn_id, ESP_GATT_DB_CHARACTERISTIC, s_service_start, s_service_end, 0, &count) == ESP_OK && count > 0) {
        esp_gattc_char_elem_t result[4];
        uint16_t found = count > 4 ? 4 : count;
        if (esp_ble_gattc_get_char_by_uuid(gattc_if, s_conn_id, s_service_start, s_service_end, uuid, result, &found) == ESP_OK && found > 0) {
            s_char_rx_handle = result[0].char_handle;
        }
    }
}

static void gattc_write_cccd(esp_gatt_if_t gattc_if)
{
    if (!s_char_tx_handle) {
        return;
    }
    esp_bt_uuid_t cccd_uuid = { .len = ESP_UUID_LEN_16 };
    cccd_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
    uint16_t count = 0;
    if (esp_ble_gattc_get_attr_count(gattc_if, s_conn_id, ESP_GATT_DB_DESCRIPTOR, s_service_start, s_service_end, s_char_tx_handle, &count) != ESP_OK || count == 0) {
        return;
    }
    esp_gattc_descr_elem_t descr[2];
    uint16_t found = count > 2 ? 2 : count;
    if (esp_ble_gattc_get_descr_by_char_handle(gattc_if, s_conn_id, s_char_tx_handle, cccd_uuid, descr, &found) != ESP_OK || found == 0) {
        return;
    }
    uint16_t notify_en = 1;
    esp_ble_gattc_write_char_descr(gattc_if, s_conn_id, descr[0].handle, sizeof(notify_en), (uint8_t *)&notify_en, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
}

static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_GATTC_REG_EVT: {
            s_gattc_if = gattc_if;
            if (s_ble_connect_pending && s_target_addr_set) {
                esp_ble_gattc_open(gattc_if, s_target_addr, s_target_addr_type, true);
            }
            break;
        }
        case ESP_GATTC_OPEN_EVT: {
            if (param->open.status == ESP_GATT_OK) {
                s_conn_id = param->open.conn_id;
                hamview_icom_set_connected(true);
                gattc_search_service(gattc_if);
            } else {
                hamview_icom_set_connected(false);
            }
            break;
        }
        case ESP_GATTC_SEARCH_RES_EVT: {
            if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
                memcmp(param->search_res.srvc_id.uuid.uuid.uuid128, IC705_SERVICE_UUID, sizeof(IC705_SERVICE_UUID)) == 0) {
                s_service_start = param->search_res.start_handle;
                s_service_end = param->search_res.end_handle;
            }
            break;
        }
        case ESP_GATTC_SEARCH_CMPL_EVT: {
            gattc_discover_chars(gattc_if);
            gattc_enable_notify(gattc_if);
            break;
        }
        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
            if (param->reg_for_notify.status == ESP_GATT_OK) {
                gattc_write_cccd(gattc_if);
            }
            break;
        }
        case ESP_GATTC_NOTIFY_EVT: {
            hamview_icom_on_civ_bytes(param->notify.value, param->notify.value_len);
            break;
        }
        case ESP_GATTC_DISCONNECT_EVT: {
            hamview_icom_set_connected(false);
            s_service_start = 0;
            s_service_end = 0;
            s_char_tx_handle = 0;
            s_char_rx_handle = 0;
            s_conn_id = 0;
            break;
        }
        default:
            break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    (void)param;
    switch (event) {
        case ESP_GAP_BLE_PASSKEY_REQ_EVT: {
            esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, 0);
            break;
        }
        default:
            break;
    }
}

static bool ble_init(void)
{
    if (s_ble_initialized) {
        return true;
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if !HAMVIEW_ICOM_HAS_SPP
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
#endif
        if (esp_bt_controller_init(&cfg) != ESP_OK) {
            return false;
        }
    }

    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
#if HAMVIEW_ICOM_HAS_SPP
        if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
#else
        if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
#endif
            return false;
        }
    }

    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        if (esp_bluedroid_init() != ESP_OK) {
            return false;
        }
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        if (esp_bluedroid_enable() != ESP_OK) {
            return false;
        }
    }
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gattc_register_callback(gattc_event_handler);
    esp_ble_gattc_app_register(0x705);
    s_ble_initialized = true;
    return true;
}
#endif

#if HAMVIEW_ICOM_HAS_SPP
static bool s_spp_initialized = false;
static bool s_spp_connect_pending = false;
static esp_bd_addr_t s_spp_target_addr = {0};
static bool s_spp_target_addr_set = false;
static uint32_t s_spp_handle = 0;
static bool s_spp_connected = false;
static const char *SPP_DEFAULT_PIN = "0000";

static void bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_BT_GAP_PIN_REQ_EVT: {
            esp_bt_pin_code_t pin_code = {0};
            size_t pin_len = strlen(SPP_DEFAULT_PIN);
            if (pin_len > 0 && pin_len <= sizeof(pin_code)) {
                memcpy(pin_code, SPP_DEFAULT_PIN, pin_len);
                esp_bt_gap_pin_reply(param->pin_req.bda, true, (uint8_t)pin_len, pin_code);
            } else {
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, pin_code);
            }
            break;
        }
        case ESP_BT_GAP_CFM_REQ_EVT: {
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        }
        case ESP_BT_GAP_KEY_NOTIF_EVT: {
            ESP_LOGI(TAG, "BT passkey: %06u", param->key_notif.passkey);
            break;
        }
        default:
            break;
    }
}

static void spp_event_handler(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_SPP_INIT_EVT: {
            s_spp_initialized = true;
            esp_bt_dev_set_device_name(SPP_DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
            esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_OUT;
            esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
            if (!s_spp_server_started) {
                if (esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "HAMVIEW_CIV") == ESP_OK) {
                    s_spp_server_started = true;
                }
            }
            break;
        }
        case ESP_SPP_DISCOVERY_COMP_EVT: {
            if (param->disc_comp.status == ESP_SPP_SUCCESS && param->disc_comp.scn != 0) {
                esp_spp_connect(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER, param->disc_comp.scn, s_spp_target_addr);
            }
            break;
        }
        case ESP_SPP_START_EVT: {
            break;
        }
        case ESP_SPP_OPEN_EVT: {
            s_spp_handle = param->open.handle;
            s_spp_connected = true;
            s_spp_connect_pending = false;
            hamview_icom_set_connected(true);
            break;
        }
        case ESP_SPP_SRV_OPEN_EVT: {
            s_spp_handle = param->srv_open.handle;
            s_spp_connected = true;
            hamview_icom_set_connected(true);
            char addr_str[18] = {0};
            bda_to_string(param->srv_open.rem_bda, addr_str, sizeof(addr_str));
            hamview_icom_set_device("IC-705", addr_str);
            break;
        }
        case ESP_SPP_DATA_IND_EVT: {
            hamview_icom_on_civ_bytes(param->data_ind.data, param->data_ind.len);
            break;
        }
        case ESP_SPP_CLOSE_EVT: {
            s_spp_connected = false;
            s_spp_handle = 0;
            hamview_icom_set_connected(false);
            break;
        }
        default:
            break;
    }
}

static bool spp_init(void)
{
    if (s_spp_initialized) {
        return true;
    }

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&cfg) != ESP_OK) {
            return false;
        }
    }

    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
            return false;
        }
    }

    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        if (esp_bluedroid_init() != ESP_OK) {
            return false;
        }
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        if (esp_bluedroid_enable() != ESP_OK) {
            return false;
        }
    }

    esp_bt_gap_register_callback(bt_gap_event_handler);
    esp_spp_register_callback(spp_event_handler);
    if (esp_spp_init(ESP_SPP_MODE_CB) != ESP_OK) {
        return false;
    }
    return true;
}
#endif

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void state_lock(void)
{
    if (s_state_mutex) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

static void state_unlock(void)
{
    if (s_state_mutex) {
        xSemaphoreGive(s_state_mutex);
    }
}

static uint64_t civ_bcd_to_hz(const uint8_t *bcd, size_t len)
{
    uint64_t value = 0;
    uint64_t place = 1;
    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = bcd[i];
        uint8_t low = byte & 0x0F;
        uint8_t high = (byte >> 4) & 0x0F;
        value += (uint64_t)low * place;
        place *= 10;
        value += (uint64_t)high * place;
        place *= 10;
    }
    return value;
}

static const char *civ_mode_to_text(uint8_t mode)
{
    switch (mode) {
        case 0x00: return "LSB";
        case 0x01: return "USB";
        case 0x02: return "AM";
        case 0x03: return "CW";
        case 0x04: return "RTTY";
        case 0x05: return "FM";
        case 0x06: return "WFM";
        case 0x07: return "DV";
        case 0x08: return "LSB-D";
        case 0x09: return "USB-D";
        case 0x0A: return "AM-D";
        case 0x0B: return "FM-D";
        default: return "";
    }
}

static void civ_process_frame(const uint8_t *frame, size_t len)
{
    if (!frame || len < 6 || frame[0] != 0xFE || frame[1] != 0xFE || frame[len - 1] != 0xFD) {
        return;
    }

    uint8_t cmd = frame[4];
    const uint8_t *data = &frame[5];
    size_t data_len = len - 6;

    state_lock();
    switch (cmd) {
        case 0x03: {
            if (data_len >= 5) {
                s_state.freq_hz = civ_bcd_to_hz(data, 5);
                s_state.last_update_ms = now_ms();
            }
            break;
        }
        case 0x04:
        case 0x05: {
            if (data_len >= 2) {
                const char *mode = civ_mode_to_text(data[0]);
                if (mode[0]) {
                    strlcpy(s_state.mode, mode, sizeof(s_state.mode));
                }
                s_state.last_update_ms = now_ms();
            }
            break;
        }
        case 0x15: {
            if (data_len >= 2 && data[0] == 0x02) {
                s_state.s_meter_raw = data[1];
                s_state.last_update_ms = now_ms();
            }
            break;
        }
        default:
            break;
    }
    state_unlock();
}

static size_t civ_build_frame(uint8_t *buf, size_t max, uint8_t dest, uint8_t src, uint8_t cmd, const uint8_t *data, size_t data_len)
{
    if (!buf || max < 6) {
        return 0;
    }
    size_t idx = 0;
    buf[idx++] = 0xFE;
    buf[idx++] = 0xFE;
    buf[idx++] = dest;
    buf[idx++] = src;
    buf[idx++] = cmd;
    if (data && data_len > 0 && (idx + data_len + 1) <= max) {
        memcpy(&buf[idx], data, data_len);
        idx += data_len;
    }
    buf[idx++] = 0xFD;
    return idx;
}

static void icom_tx_store(icom_tx_entry_t *entries, size_t entry_count, uint16_t seq, const uint8_t *data, size_t len)
{
    if (!entries || entry_count == 0 || !data || len == 0) {
        return;
    }
    size_t index = seq % entry_count;
    entries[index].seq = seq;
    entries[index].len = (uint16_t)((len > sizeof(entries[index].data)) ? sizeof(entries[index].data) : len);
    memcpy(entries[index].data, data, entries[index].len);
}

static bool icom_tx_lookup(const icom_tx_entry_t *entries, size_t entry_count, uint16_t seq, const uint8_t **data, size_t *len)
{
    if (!entries || entry_count == 0 || !data || !len) {
        return false;
    }
    size_t index = seq % entry_count;
    if (entries[index].seq != seq || entries[index].len == 0) {
        return false;
    }
    *data = entries[index].data;
    *len = entries[index].len;
    return true;
}

static void icom_send_tracked(int sock, const struct sockaddr_in *dest, uint16_t *seq,
                              icom_tx_entry_t *tx, size_t tx_count, uint8_t *packet, size_t len)
{
    if (!dest || !seq || !packet || len < 8) {
        return;
    }
    write_u16_le(packet, 6, *seq);
    icom_tx_store(tx, tx_count, *seq, packet, len);
    sendto(sock, packet, len, 0, (const struct sockaddr *)dest, sizeof(*dest));
    (*seq)++;
}

static void icom_send_control(int sock, const struct sockaddr_in *dest, uint16_t *seq,
                              icom_tx_entry_t *tx, size_t tx_count, uint32_t my_id,
                              uint32_t remote_id, bool tracked, uint16_t override_seq, uint8_t type)
{
    uint8_t packet[ICOM_CTRL_PACKET_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_CTRL_PACKET_SIZE);
    write_u16_le(packet, 4, type);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    if (tracked) {
        icom_send_tracked(sock, dest, seq, tx, tx_count, packet, sizeof(packet));
    } else {
        write_u16_le(packet, 6, override_seq);
        sendto(sock, packet, sizeof(packet), 0, (const struct sockaddr *)dest, sizeof(*dest));
    }
}

static void icom_send_ping_reply(int sock, const struct sockaddr_in *dest, uint32_t my_id,
                                 uint32_t remote_id, const uint8_t *rx)
{
    uint8_t packet[ICOM_PING_PACKET_SIZE] = {0};
    memcpy(packet, rx, sizeof(packet));
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    packet[0x10] = 0x01;
    sendto(sock, packet, sizeof(packet), 0, (const struct sockaddr *)dest, sizeof(*dest));
}

static void icom_send_ping(int sock, const struct sockaddr_in *dest, uint32_t my_id, uint32_t remote_id, uint16_t *seq)
{
    uint8_t packet[ICOM_PING_PACKET_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_PING_PACKET_SIZE);
    write_u16_le(packet, 4, 0x07);
    write_u16_le(packet, 6, *seq);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    packet[0x10] = 0x00;
    uint32_t ms = (uint32_t)(now_ms() % (24ULL * 60ULL * 60ULL * 1000ULL));
    write_u32_le(packet, 0x11, ms);
    sendto(sock, packet, sizeof(packet), 0, (const struct sockaddr *)dest, sizeof(*dest));
    (*seq)++;
}

static void icom_send_login(int sock, const struct sockaddr_in *dest, uint16_t *seq, icom_tx_entry_t *tx, size_t tx_count,
                            uint32_t my_id, uint32_t remote_id, uint16_t *auth_seq, uint16_t tok_request,
                            const char *username, const char *password)
{
    uint8_t packet[ICOM_LOGIN_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_LOGIN_SIZE);
    write_u16_le(packet, 4, 0x00);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    write_u32_be(packet, 0x10, (uint32_t)(ICOM_LOGIN_SIZE - 0x10));
    packet[0x14] = 0x01;
    packet[0x15] = 0x00;
    write_u16_be(packet, 0x16, (*auth_seq)++);
    write_u16_le(packet, 0x1a, tok_request);
    uint8_t enc_user[16];
    uint8_t enc_pass[16];
    icom_passcode(username, enc_user);
    icom_passcode(password, enc_pass);
    memcpy(&packet[0x40], enc_user, sizeof(enc_user));
    memcpy(&packet[0x50], enc_pass, sizeof(enc_pass));
    memcpy(&packet[0x60], ICOM_COMP_NAME, strlen(ICOM_COMP_NAME));
    icom_send_tracked(sock, dest, seq, tx, tx_count, packet, sizeof(packet));
}

static void icom_send_token(int sock, const struct sockaddr_in *dest, uint16_t *seq, icom_tx_entry_t *tx, size_t tx_count,
                            uint32_t my_id, uint32_t remote_id, uint16_t *auth_seq, uint16_t tok_request, uint32_t token,
                            uint16_t auth_start_id, uint8_t request_type)
{
    uint8_t packet[ICOM_TOKEN_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_TOKEN_SIZE);
    write_u16_le(packet, 4, 0x00);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    write_u32_be(packet, 0x10, (uint32_t)(ICOM_TOKEN_SIZE - 0x10));
    packet[0x14] = 0x01;
    packet[0x15] = request_type;
    write_u16_be(packet, 0x16, (*auth_seq)++);
    write_u16_le(packet, 0x1a, tok_request);
    write_u32_le(packet, 0x1c, token);
    if (auth_start_id != 0) {
        write_u16_le(packet, 0x20, auth_start_id);
    }
    write_u16_be(packet, 0x24, 0x0798);
    icom_send_tracked(sock, dest, seq, tx, tx_count, packet, sizeof(packet));
}

static void icom_send_request_stream(int sock, const struct sockaddr_in *dest, uint16_t *seq, icom_tx_entry_t *tx, size_t tx_count,
                                     uint32_t my_id, uint32_t remote_id, uint16_t *auth_seq, uint16_t tok_request, uint32_t token,
                                     uint16_t auth_start_id, const icom_radio_cap_t *radio, uint16_t civ_local_port,
                                     uint16_t audio_local_port, const char *username)
{
    uint8_t packet[ICOM_CONNINFO_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_CONNINFO_SIZE);
    write_u16_le(packet, 4, 0x00);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    write_u32_be(packet, 0x10, (uint32_t)(ICOM_CONNINFO_SIZE - 0x10));
    packet[0x14] = 0x01;
    packet[0x15] = 0x03;
    write_u16_be(packet, 0x16, (*auth_seq)++);
    write_u16_le(packet, 0x1a, tok_request);
    write_u32_le(packet, 0x1c, token);
    if (auth_start_id != 0) {
        write_u16_le(packet, 0x20, auth_start_id);
    }
    if (radio && radio->use_guid) {
        memcpy(&packet[0x20], radio->guid, sizeof(radio->guid));
    } else if (radio) {
        write_u16_le(packet, 0x27, radio->commoncap_be);
        memcpy(&packet[0x2a], radio->mac, sizeof(radio->mac));
    }
    if (radio && radio->name[0] != '\0') {
        memcpy(&packet[0x40], radio->name, strnlen(radio->name, sizeof(radio->name)));
    } else {
        const char *fallback = "IC-705";
        memcpy(&packet[0x40], fallback, strlen(fallback));
    }
    uint8_t enc_user[16];
    icom_passcode(username, enc_user);
    memcpy(&packet[0x60], enc_user, sizeof(enc_user));
    packet[0x70] = 0x01;
    packet[0x71] = 0x00;
    packet[0x72] = 0x04;
    packet[0x73] = 0x00;
    write_u32_be(packet, 0x74, 8000);
    write_u32_be(packet, 0x78, 0);
    write_u32_be(packet, 0x7c, civ_local_port);
    write_u32_be(packet, 0x80, audio_local_port);
    write_u32_be(packet, 0x84, 0);
    packet[0x88] = 0x01;
    icom_send_tracked(sock, dest, seq, tx, tx_count, packet, sizeof(packet));
}

static void icom_send_caps_request(int sock, const struct sockaddr_in *dest, uint16_t *seq, icom_tx_entry_t *tx, size_t tx_count,
                                   uint32_t my_id, uint32_t remote_id, uint16_t *auth_seq, uint16_t tok_request, uint32_t token)
{
    uint8_t packet[ICOM_CAPS_HEADER_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_CAPS_HEADER_SIZE);
    write_u16_le(packet, 4, 0x00);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    write_u32_be(packet, 0x10, (uint32_t)(ICOM_CAPS_HEADER_SIZE - 0x10));
    packet[0x14] = 0x01;
    packet[0x15] = 0x04;
    write_u16_be(packet, 0x16, (*auth_seq)++);
    write_u16_le(packet, 0x1a, tok_request);
    write_u32_le(packet, 0x1c, token);
    icom_send_tracked(sock, dest, seq, tx, tx_count, packet, sizeof(packet));
}

static void icom_send_civ_openclose(int sock, const struct sockaddr_in *dest, uint16_t *seq, icom_tx_entry_t *tx, size_t tx_count,
                                    uint32_t my_id, uint32_t remote_id, uint16_t *civ_seq, bool close)
{
    uint8_t packet[ICOM_OPENCLOSE_SIZE] = {0};
    write_u32_le(packet, 0, ICOM_OPENCLOSE_SIZE);
    write_u16_le(packet, 4, 0x00);
    write_u32_le(packet, 8, my_id);
    write_u32_le(packet, 12, remote_id);
    write_u16_le(packet, 0x10, 0x01c0);
    write_u16_le(packet, 0x13, *civ_seq);
    packet[0x15] = close ? 0x00 : 0x04;
    (*civ_seq)++;
    icom_send_tracked(sock, dest, seq, tx, tx_count, packet, sizeof(packet));
}

static void icom_send_civ_data(int sock, const struct sockaddr_in *dest, uint16_t *seq, icom_tx_entry_t *tx, size_t tx_count,
                               uint32_t my_id, uint32_t remote_id, uint16_t *civ_seq, const uint8_t *payload, size_t payload_len)
{
    if (!payload || payload_len == 0) {
        return;
    }
    uint8_t header[ICOM_CIV_HEADER_SIZE] = {0};
    write_u32_le(header, 0, (uint32_t)(ICOM_CIV_HEADER_SIZE + payload_len));
    write_u16_le(header, 4, 0xC1);
    write_u32_le(header, 8, my_id);
    write_u32_le(header, 12, remote_id);
    header[0x10] = 0x00;
    write_u16_le(header, 0x11, (uint16_t)payload_len);
    write_u16_le(header, 0x13, *civ_seq);
    (*civ_seq)++;

    uint8_t buffer[ICOM_CIV_HEADER_SIZE + CIV_MAX_FRAME];
    size_t total = ICOM_CIV_HEADER_SIZE + payload_len;
    if (total > sizeof(buffer)) {
        return;
    }
    memcpy(buffer, header, ICOM_CIV_HEADER_SIZE);
    memcpy(buffer + ICOM_CIV_HEADER_SIZE, payload, payload_len);
    icom_send_tracked(sock, dest, seq, tx, tx_count, buffer, total);

    if (s_civ_last_rx_ms == 0) {
        uint8_t alt_header[ICOM_CIV_HEADER_SIZE] = {0};
        write_u32_le(alt_header, 0, (uint32_t)(ICOM_CIV_HEADER_SIZE + payload_len));
        write_u16_le(alt_header, 4, 0x00);
        write_u32_le(alt_header, 8, my_id);
        write_u32_le(alt_header, 12, remote_id);
        alt_header[0x10] = 0xC1;
        write_u16_le(alt_header, 0x11, (uint16_t)payload_len);
        write_u16_le(alt_header, 0x13, *civ_seq);
        (*civ_seq)++;
        memcpy(buffer, alt_header, ICOM_CIV_HEADER_SIZE);
        memcpy(buffer + ICOM_CIV_HEADER_SIZE, payload, payload_len);
        icom_send_tracked(sock, dest, seq, tx, tx_count, buffer, total);
    }
}

static bool icom_open_udp_socket(int *sock_out, uint16_t *port_out)
{
    if (!sock_out || !port_out) {
        return false;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        close(sock);
        return false;
    }
    socklen_t len = sizeof(local);
    getsockname(sock, (struct sockaddr *)&local, &len);
    *port_out = ntohs(local.sin_port);
    *sock_out = sock;
    return true;
}

static void civ_wifi_task(void *arg)
{
    (void)arg;
    struct sockaddr_in ctrl_dest = {0};
    ctrl_dest.sin_family = AF_INET;
    ctrl_dest.sin_port = htons(s_civ_port);
    ctrl_dest.sin_addr.s_addr = inet_addr(s_civ_ip);

    icom_tx_entry_t *ctrl_tx = calloc(ICOM_TX_BUF_COUNT, sizeof(icom_tx_entry_t));
    icom_tx_entry_t *civ_tx = calloc(ICOM_TX_BUF_COUNT, sizeof(icom_tx_entry_t));
    if (!ctrl_tx || !civ_tx) {
        free(ctrl_tx);
        free(civ_tx);
        ESP_LOGE(TAG, "CIV WiFi failed: out of memory");
        hamview_icom_set_connected(false);
        s_civ_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint8_t local_ip[4] = {0};
    while (s_civ_wifi_enabled && !get_local_ipv4(local_ip)) {
        ESP_LOGW(TAG, "CIV WiFi waiting for local IP");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!s_civ_wifi_enabled) {
        free(ctrl_tx);
        free(civ_tx);
        s_civ_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctrl_sock < 0) {
        ESP_LOGE(TAG, "CIV UDP socket failed: %d", errno);
        free(ctrl_tx);
        free(civ_tx);
        hamview_icom_set_connected(false);
        s_civ_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = 0;
    if (bind(ctrl_sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        ESP_LOGE(TAG, "CIV UDP bind failed: %d", errno);
        close(ctrl_sock);
        free(ctrl_tx);
        free(civ_tx);
        hamview_icom_set_connected(false);
        s_civ_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    socklen_t addr_len = sizeof(local_addr);
    getsockname(ctrl_sock, (struct sockaddr *)&local_addr, &addr_len);
    uint16_t ctrl_local_port = ntohs(local_addr.sin_port);
    uint32_t my_id = make_my_id(local_ip, ctrl_local_port);

    int civ_sock = -1;
    int audio_sock = -1;
    uint16_t civ_local_port = 0;
    uint16_t audio_local_port = 0;
    uint16_t civ_remote_port = 0;
    uint32_t civ_my_id = 0;
    uint32_t civ_remote_id = 0;
    struct sockaddr_in civ_dest = {0};
    civ_dest.sin_family = AF_INET;
    civ_dest.sin_addr.s_addr = ctrl_dest.sin_addr.s_addr;

    icom_radio_cap_t radio = {0};
    icom_radio_cap_t token_radio = {0};

    uint16_t ctrl_send_seq = 1;
    uint16_t civ_send_seq = 1;
    uint16_t civ_seq = 0;
    uint16_t auth_seq = 0x30;
    uint16_t tok_request = 0;
    uint32_t token = 0;
    uint32_t remote_id = 0;
    uint16_t auth_start_id = 0;
    bool authed = false;
    bool sent_login = false;
    bool have_caps = false;
    bool stream_open = false;
    bool civ_ready = false;
    bool got_i_am_here = false;
    bool token_pending = false;
    bool token_radio_valid = false;
    uint64_t last_login_ms = 0;
    uint64_t last_token_request_ms = 0;
    uint64_t last_stream_request_ms = 0;
    uint64_t last_caps_request_ms = 0;

    uint64_t last_are_you_there = 0;
    uint64_t last_ping = 0;
    uint64_t last_idle = 0;
    uint64_t last_token = 0;
    uint64_t last_poll = 0;
    uint64_t last_civ_open = 0;

    s_civ_last_rx_ms = 0;
    hamview_icom_set_device("IC-705 WiFi", s_civ_ip);

    while (s_civ_wifi_enabled) {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(ctrl_sock, &readset);
        int maxfd = ctrl_sock;
        if (civ_sock >= 0) {
            FD_SET(civ_sock, &readset);
            if (civ_sock > maxfd) {
                maxfd = civ_sock;
            }
        }
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        int sel = select(maxfd + 1, &readset, NULL, NULL, &tv);

        if (sel > 0 && FD_ISSET(ctrl_sock, &readset)) {
            uint8_t rx_buf[512];
            int len = recvfrom(ctrl_sock, rx_buf, sizeof(rx_buf), 0, NULL, NULL);
            if (len >= ICOM_CTRL_PACKET_SIZE) {
                uint16_t type = (uint16_t)(rx_buf[4] | (rx_buf[5] << 8));
                if (len == ICOM_CTRL_PACKET_SIZE) {
                    if (type == 0x04) {
                        remote_id = read_u32_le(rx_buf, 8);
                        ESP_LOGI(TAG, "CIV control: I am here (remote_id=0x%08x)", (unsigned)remote_id);
                        got_i_am_here = true;
                        icom_send_control(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, false, 0x01, 0x06);
                        if (!sent_login) {
                            tok_request = (uint16_t)esp_random();
                            ESP_LOGI(TAG, "CIV control: sending login after I am here");
                            icom_send_login(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                            &auth_seq, tok_request, s_civ_username, s_civ_password);
                            sent_login = true;
                        }
                    } else if (type == 0x06) {
                        if (!sent_login) {
                            tok_request = (uint16_t)esp_random();
                            ESP_LOGI(TAG, "CIV control: I am ready, sending login");
                            icom_send_login(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                            &auth_seq, tok_request, s_civ_username, s_civ_password);
                            sent_login = true;
                        }
                    } else if (type == 0x01) {
                        uint16_t req_seq = (uint16_t)(rx_buf[6] | (rx_buf[7] << 8));
                        const uint8_t *data = NULL;
                        size_t data_len = 0;
                        if (icom_tx_lookup(ctrl_tx, ICOM_TX_BUF_COUNT, req_seq, &data, &data_len)) {
                            sendto(ctrl_sock, data, data_len, 0, (const struct sockaddr *)&ctrl_dest, sizeof(ctrl_dest));
                        } else {
                            icom_send_control(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, false, req_seq, 0x00);
                        }
                    }
                } else if (len == ICOM_PING_PACKET_SIZE && type == 0x07) {
                    if (rx_buf[0x10] == 0x00) {
                        icom_send_ping_reply(ctrl_sock, &ctrl_dest, my_id, remote_id, rx_buf);
                    }
                } else if ((type == 0xC1 || (type == 0x00 && rx_buf[0x10] == 0xC1)) && len >= ICOM_CIV_HEADER_SIZE) {
                    uint16_t data_len = (uint16_t)(rx_buf[0x11] | (rx_buf[0x12] << 8));
                    if (data_len > 0 && (ICOM_CIV_HEADER_SIZE + data_len) <= (size_t)len) {
                        hamview_icom_on_civ_bytes(&rx_buf[ICOM_CIV_HEADER_SIZE], data_len);
                        s_civ_last_rx_ms = now_ms();
                        hamview_icom_set_connected(true);
                    }
                } else if (len == ICOM_LOGIN_RESPONSE_SIZE) {
                    uint32_t error = read_u32_le(rx_buf, 0x30);
                    uint16_t tok_resp = read_u16_le(rx_buf, 0x1a);
                    uint32_t token_resp = read_u32_le(rx_buf, 0x1c);
                    ESP_LOGI(TAG, "CIV login rsp error=0x%08x tok=0x%04x token=0x%08x", (unsigned)error, tok_resp, (unsigned)token_resp);
                    if (error == 0xfeffffff) {
                        ESP_LOGW(TAG, "CIV login failed: invalid username/password");
                        authed = false;
                        token_pending = false;
                    } else if (!authed && tok_request == (uint16_t)(rx_buf[0x1a] | (rx_buf[0x1b] << 8))) {
                        token = read_u32_le(rx_buf, 0x1c);
                        auth_start_id = read_u16_le(rx_buf, 0x20);
                        last_login_ms = now_ms();
                        ESP_LOGI(TAG, "CIV login OK, requesting token");
                        icom_send_token(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                        &auth_seq, tok_request, token, auth_start_id, 0x02);
                        token_pending = true;
                        last_token_request_ms = now_ms();
                    }
                } else if (len >= ICOM_TOKEN_SIZE) {
                    uint8_t req_reply = rx_buf[0x14];
                    uint8_t req_type = rx_buf[0x15];
                    if (req_type == 0x02 || req_type == 0x05) {
                        uint16_t tok_resp = read_u16_le(rx_buf, 0x1a);
                        uint32_t response = read_u32_le(rx_buf, 0x30);
                        uint16_t token_auth_start = read_u16_le(rx_buf, 0x20);
                        ESP_LOGI(TAG, "CIV token rsp reply=0x%02x type=0x%02x tok=0x%04x resp=0x%08x", req_reply, req_type, tok_resp, (unsigned)response);
                        if (response == 0x00000000) {
                            authed = true;
                            token_pending = false;
                            last_token = now_ms();
                            if (token_auth_start != 0) {
                                auth_start_id = token_auth_start;
                            }
                            uint16_t commoncap = read_u16_le(rx_buf, 0x27);
                            if (!have_caps) {
                                memset(&token_radio, 0, sizeof(token_radio));
                                token_radio.use_guid = (commoncap != 0x8010);
                                token_radio.commoncap_be = commoncap;
                                if (token_radio.use_guid) {
                                    memcpy(token_radio.guid, &rx_buf[0x20], sizeof(token_radio.guid));
                                } else {
                                    memcpy(token_radio.mac, &rx_buf[0x2a], sizeof(token_radio.mac));
                                }
                                bool guid_nonzero = false;
                                bool mac_nonzero = false;
                                for (size_t i = 0; i < sizeof(token_radio.guid); ++i) {
                                    if (token_radio.guid[i] != 0) {
                                        guid_nonzero = true;
                                        break;
                                    }
                                }
                                for (size_t i = 0; i < sizeof(token_radio.mac); ++i) {
                                    if (token_radio.mac[i] != 0) {
                                        mac_nonzero = true;
                                        break;
                                    }
                                }
                                token_radio_valid = guid_nonzero || mac_nonzero;
                            }
                            ESP_LOGI(TAG, "CIV token OK");
                            if (have_caps && !stream_open) {
                                if (civ_sock < 0) {
                                    if (icom_open_udp_socket(&civ_sock, &civ_local_port)) {
                                        civ_my_id = make_my_id(local_ip, civ_local_port);
                                    }
                                }
                                if (audio_sock < 0) {
                                    icom_open_udp_socket(&audio_sock, &audio_local_port);
                                }
                                ESP_LOGI(TAG, "CIV request stream (local civ=%u audio=%u)", civ_local_port, audio_local_port);
                                icom_send_request_stream(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                                         &auth_seq, tok_request, token, auth_start_id, &radio, civ_local_port, audio_local_port, s_civ_username);
                            }
                        } else {
                            token_pending = false;
                        }
                    }
                } else if (len >= ICOM_CAPS_HEADER_SIZE && ((len - ICOM_CAPS_HEADER_SIZE) % ICOM_RADIO_CAP_SIZE == 0)) {
                    if (!have_caps) {
                        const uint8_t *rad = &rx_buf[ICOM_CAPS_HEADER_SIZE];
                        uint16_t commoncap = read_u16_le(rad, 0x07);
                        radio.use_guid = (commoncap != 0x8010);
                        radio.commoncap_be = commoncap;
                        if (radio.use_guid) {
                            memcpy(radio.guid, rad, sizeof(radio.guid));
                        } else {
                            memcpy(radio.mac, rad + 0x0a, sizeof(radio.mac));
                        }
                        memcpy(radio.name, rad + 0x10, sizeof(radio.name));
                        radio.name[sizeof(radio.name) - 1] = '\0';
                        radio.civ = rad[0x52];
                        radio.baudrate_be = read_u32_be(rad, 0x5a);
                        have_caps = true;
                        if (radio.name[0] != '\0') {
                            hamview_icom_set_device(radio.name, s_civ_ip);
                            ESP_LOGI(TAG, "CIV caps: %s civ=0x%02x", radio.name, radio.civ);
                        }
                        if (authed && !stream_open) {
                            if (civ_sock < 0) {
                                if (icom_open_udp_socket(&civ_sock, &civ_local_port)) {
                                    civ_my_id = make_my_id(local_ip, civ_local_port);
                                }
                            }
                            if (audio_sock < 0) {
                                icom_open_udp_socket(&audio_sock, &audio_local_port);
                            }
                            ESP_LOGI(TAG, "CIV request stream (local civ=%u audio=%u)", civ_local_port, audio_local_port);
                            icom_send_request_stream(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                                     &auth_seq, tok_request, token, auth_start_id, &radio, civ_local_port, audio_local_port, s_civ_username);
                        }
                    }
                } else if (len >= ICOM_STATUS_SIZE) {
                    uint32_t error = read_u32_le(rx_buf, 0x30);
                    if (error == 0xffffffff) {
                        ESP_LOGW(TAG, "CIV stream rejected by radio");
                        hamview_icom_set_connected(false);
                    } else {
                        civ_remote_port = read_u16_be(rx_buf, 0x42);
                        if (civ_remote_port != 0 && !stream_open) {
                            civ_dest.sin_port = htons(civ_remote_port);
                            stream_open = true;
                            civ_ready = false;
                            last_civ_open = 0;
                            ESP_LOGI(TAG, "CIV stream open, remote civ port=%u", civ_remote_port);
                            if (civ_my_id == 0 && civ_local_port != 0) {
                                civ_my_id = make_my_id(local_ip, civ_local_port);
                            }
                            icom_send_civ_openclose(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, false);
                        }
                    }
                }
            }
        }

        if (civ_sock >= 0 && FD_ISSET(civ_sock, &readset)) {
            uint8_t rx_buf[512];
            struct sockaddr_in src = {0};
            socklen_t src_len = sizeof(src);
            int len = recvfrom(civ_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&src, &src_len);
            if (len >= ICOM_CTRL_PACKET_SIZE) {
                if (src.sin_port != 0) {
                    bool update_dest = (civ_dest.sin_port == 0) || (civ_dest.sin_port != src.sin_port) || (civ_dest.sin_addr.s_addr != src.sin_addr.s_addr);
                    if (update_dest) {
                        civ_dest = src;
                        civ_dest.sin_family = AF_INET;
                        civ_remote_port = ntohs(src.sin_port);
                    }
                }
                uint16_t type = (uint16_t)(rx_buf[4] | (rx_buf[5] << 8));
                if (len == ICOM_CTRL_PACKET_SIZE) {
                    if (type == 0x04) {
                        civ_remote_id = read_u32_le(rx_buf, 8);
                        civ_ready = true;
                        ESP_LOGI(TAG, "CIV data: I am here (remote_id=0x%08x)", (unsigned)civ_remote_id);
                        icom_send_control(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT,
                                          civ_my_id, civ_remote_id, false, 0x01, 0x06);
                    } else if (type == 0x06) {
                        civ_ready = true;
                        ESP_LOGI(TAG, "CIV data: I am ready");
                        icom_send_civ_openclose(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, false);
                    } else if (type == 0x01) {
                        uint16_t req_seq = (uint16_t)(rx_buf[6] | (rx_buf[7] << 8));
                        const uint8_t *data = NULL;
                        size_t data_len = 0;
                        if (icom_tx_lookup(civ_tx, ICOM_TX_BUF_COUNT, req_seq, &data, &data_len)) {
                            sendto(civ_sock, data, data_len, 0, (const struct sockaddr *)&civ_dest, sizeof(civ_dest));
                        } else {
                            icom_send_control(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, false, req_seq, 0x00);
                        }
                    }
                } else if (len == ICOM_PING_PACKET_SIZE && type == 0x07) {
                    if (rx_buf[0x10] == 0x00) {
                        icom_send_ping_reply(civ_sock, &civ_dest, civ_my_id, civ_remote_id, rx_buf);
                    }
                } else if ((type == 0xC1 || (type == 0x00 && rx_buf[0x10] == 0xC1)) && len >= ICOM_CIV_HEADER_SIZE) {
                    uint16_t data_len = (uint16_t)(rx_buf[0x11] | (rx_buf[0x12] << 8));
                    if (data_len > 0 && (ICOM_CIV_HEADER_SIZE + data_len) <= (size_t)len) {
                        hamview_icom_on_civ_bytes(&rx_buf[ICOM_CIV_HEADER_SIZE], data_len);
                        s_civ_last_rx_ms = now_ms();
                        hamview_icom_set_connected(true);
                    }
                }
            }
        }

        uint64_t now = now_ms();
        if (!authed && !got_i_am_here && (now - last_are_you_there) > ICOM_ARE_YOU_THERE_MS) {
            icom_send_control(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, false, 0x00, 0x03);
            last_are_you_there = now;
        }

        if (authed && (now - last_token) > ICOM_TOKEN_RENEW_MS) {
            icom_send_token(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                            &auth_seq, tok_request, token, auth_start_id, 0x05);
            last_token = now;
        }

        if (token_pending && !authed && (now - last_token_request_ms) > 1500) {
            ESP_LOGW(TAG, "CIV token retry");
            icom_send_token(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                            &auth_seq, tok_request, token, auth_start_id, 0x02);
            last_token_request_ms = now;
        }

        if (authed && !have_caps && (now - last_caps_request_ms) > 2000) {
            ESP_LOGI(TAG, "CIV request caps");
            icom_send_caps_request(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                   &auth_seq, tok_request, token);
            last_caps_request_ms = now;
        }

        if (authed && !stream_open && (now - last_stream_request_ms) > 2000) {
            if (civ_sock < 0) {
                if (icom_open_udp_socket(&civ_sock, &civ_local_port)) {
                    civ_my_id = make_my_id(local_ip, civ_local_port);
                }
            }
            if (audio_sock < 0) {
                icom_open_udp_socket(&audio_sock, &audio_local_port);
            }
            ESP_LOGI(TAG, "CIV request stream (fallback, local civ=%u audio=%u)", civ_local_port, audio_local_port);
            const icom_radio_cap_t *stream_radio = NULL;
            if (have_caps) {
                stream_radio = &radio;
            } else if (token_radio_valid) {
                stream_radio = &token_radio;
            }
            icom_send_request_stream(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                     &auth_seq, tok_request, token, auth_start_id, stream_radio, civ_local_port, audio_local_port, s_civ_username);
            last_stream_request_ms = now;
        }

        if (!stream_open && civ_sock >= 0 && civ_remote_port == 0 && (now - last_stream_request_ms) > 1000) {
            civ_remote_port = (uint16_t)(s_civ_port + 1);
            civ_dest.sin_port = htons(civ_remote_port);
            stream_open = true;
            civ_ready = false;
            last_civ_open = 0;
            ESP_LOGW(TAG, "CIV stream open assumed, remote civ port=%u", civ_remote_port);
            if (civ_my_id == 0 && civ_local_port != 0) {
                civ_my_id = make_my_id(local_ip, civ_local_port);
            }
            icom_send_civ_openclose(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, false);
        }

        if (!authed && have_caps && !stream_open && last_login_ms != 0 && (now - last_login_ms) > 1500) {
            if (civ_sock < 0) {
                if (icom_open_udp_socket(&civ_sock, &civ_local_port)) {
                    civ_my_id = make_my_id(local_ip, civ_local_port);
                }
            }
            if (audio_sock < 0) {
                icom_open_udp_socket(&audio_sock, &audio_local_port);
            }
            ESP_LOGW(TAG, "CIV token timeout, trying stream request anyway");
            icom_send_request_stream(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id,
                                     &auth_seq, tok_request, token, auth_start_id, &radio, civ_local_port, audio_local_port, s_civ_username);
            last_login_ms = now;
        }

        if (remote_id != 0 && (now - last_ping) > ICOM_PING_PERIOD_MS) {
            icom_send_ping(ctrl_sock, &ctrl_dest, my_id, remote_id, &ctrl_send_seq);
            last_ping = now;
        }

        if (remote_id != 0 && (now - last_idle) > ICOM_IDLE_PERIOD_MS) {
            icom_send_control(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, true, 0x00, 0x00);
            last_idle = now;
        }

        if (stream_open && civ_sock >= 0 && (now - last_civ_open) > ICOM_ARE_YOU_THERE_MS) {
            icom_send_control(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, false, 0x00, 0x03);
            last_civ_open = now;
        }

        if (stream_open && civ_sock >= 0 && civ_ready && (now - last_poll) > 500) {
            uint8_t frame[CIV_MAX_FRAME];
            size_t len;
            len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, CIV_ADDR_CTRL, 0x03, NULL, 0);
            icom_send_civ_data(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, frame, len);
            len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, CIV_ADDR_CTRL, 0x04, NULL, 0);
            icom_send_civ_data(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, frame, len);
            uint8_t smeter_req[1] = {0x02};
            len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, CIV_ADDR_CTRL, 0x15, smeter_req, sizeof(smeter_req));
            icom_send_civ_data(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, frame, len);

            if (s_civ_last_rx_ms == 0) {
                len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, CIV_ADDR_CTRL, 0x03, NULL, 0);
                icom_send_civ_data(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, &civ_seq, frame, len);
                len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, CIV_ADDR_CTRL, 0x04, NULL, 0);
                icom_send_civ_data(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, &civ_seq, frame, len);
                len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, CIV_ADDR_CTRL, 0x15, smeter_req, sizeof(smeter_req));
                icom_send_civ_data(ctrl_sock, &ctrl_dest, &ctrl_send_seq, ctrl_tx, ICOM_TX_BUF_COUNT, my_id, remote_id, &civ_seq, frame, len);
            }

            if (s_civ_last_rx_ms == 0) {
                len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, 0xE0, 0x03, NULL, 0);
                icom_send_civ_data(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, frame, len);
                len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, 0xE0, 0x04, NULL, 0);
                icom_send_civ_data(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, frame, len);
                len = civ_build_frame(frame, sizeof(frame), CIV_ADDR_IC705, 0xE0, 0x15, smeter_req, sizeof(smeter_req));
                icom_send_civ_data(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, frame, len);
            }
            last_poll = now;
        }

        if (stream_open && civ_sock >= 0 && civ_ready && (now - s_civ_last_rx_ms) > ICOM_CIV_WATCHDOG_MS) {
            ESP_LOGW(TAG, "CIV watchdog: no data, requesting start");
            icom_send_civ_openclose(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, false);
            s_civ_last_rx_ms = now;
        }

        if (s_civ_last_rx_ms == 0 || (now - s_civ_last_rx_ms) > 5000) {
            hamview_icom_set_connected(false);
        }
    }

    if (civ_sock >= 0 && stream_open) {
        icom_send_civ_openclose(civ_sock, &civ_dest, &civ_send_seq, civ_tx, ICOM_TX_BUF_COUNT, civ_my_id, civ_remote_id, &civ_seq, true);
    }

    if (ctrl_sock >= 0) {
        close(ctrl_sock);
    }
    if (civ_sock >= 0) {
        close(civ_sock);
    }
    if (audio_sock >= 0) {
        close(audio_sock);
    }
    free(ctrl_tx);
    free(civ_tx);
    hamview_icom_set_connected(false);
    s_civ_task_handle = NULL;
    vTaskDelete(NULL);
}

void hamview_icom_init(void)
{
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    hamview_icom_reset();
}

void hamview_icom_reset(void)
{
    state_lock();
    memset(&s_state, 0, sizeof(s_state));
    s_state.last_update_ms = 0;
    s_state.mode[0] = '\0';
    state_unlock();
    s_civ_len = 0;
}

void hamview_icom_set_connected(bool connected)
{
    state_lock();
    s_state.connected = connected;
    state_unlock();
}

void hamview_icom_set_device(const char *name, const char *addr)
{
    state_lock();
    if (name) {
        strlcpy(s_state.device_name, name, sizeof(s_state.device_name));
    }
    if (addr) {
        strlcpy(s_state.device_addr, addr, sizeof(s_state.device_addr));
    }
    state_unlock();
}

bool hamview_icom_connect(const char *addr)
{
#if HAMVIEW_ICOM_HAS_SPP
    s_spp_connect_pending = true;
    if (!spp_init()) {
        ESP_LOGW(TAG, "SPP init failed");
        return false;
    }
    (void)addr;
    return true;
#endif
#if HAMVIEW_ICOM_HAS_BLE
    if (!addr_from_string(addr, s_target_addr)) {
        ESP_LOGW(TAG, "Invalid BLE address");
        return false;
    }
    s_target_addr_set = true;
    s_ble_connect_pending = true;
    if (!ble_init()) {
        return false;
    }
    if (s_gattc_if != ESP_GATT_IF_NONE) {
        esp_ble_gattc_open(s_gattc_if, s_target_addr, s_target_addr_type, true);
    }
    return true;
#else
    (void)addr;
    return false;
#endif
}

void hamview_icom_disconnect(void)
{
#if HAMVIEW_ICOM_HAS_SPP
    if (s_spp_handle != 0) {
        esp_spp_disconnect(s_spp_handle);
        s_spp_handle = 0;
        s_spp_connected = false;
    }
    s_spp_connect_pending = false;
#endif
#if HAMVIEW_ICOM_HAS_BLE
    if (s_gattc_if != ESP_GATT_IF_NONE && s_conn_id) {
        esp_ble_gattc_close(s_gattc_if, s_conn_id);
    }
#endif
    hamview_icom_set_connected(false);
}

void hamview_icom_set_civ_wifi(bool enable, const char *ip, uint16_t port, const char *username, const char *password)
{
    if (!enable) {
        s_civ_wifi_enabled = false;
        return;
    }

    if (!ip || ip[0] == '\0' || port == 0) {
        ESP_LOGW(TAG, "CIV WiFi disabled: missing IP/port");
        s_civ_wifi_enabled = false;
        return;
    }

    strlcpy(s_civ_ip, ip, sizeof(s_civ_ip));
    strlcpy(s_civ_username, username ? username : "", sizeof(s_civ_username));
    strlcpy(s_civ_password, password ? password : "", sizeof(s_civ_password));
    s_civ_port = port;
    s_civ_wifi_enabled = true;

    if (!s_civ_task_handle) {
        xTaskCreatePinnedToCore(civ_wifi_task, "hamview_civ", 8192, NULL, 4, &s_civ_task_handle, tskNO_AFFINITY);
    }
}

void hamview_icom_on_civ_bytes(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = data[i];
        if (s_civ_len == 0) {
            if (byte == 0xFE) {
                s_civ_buf[s_civ_len++] = byte;
            }
            continue;
        }

        if (s_civ_len == 1) {
            if (byte != 0xFE) {
                s_civ_len = 0;
                continue;
            }
            s_civ_buf[s_civ_len++] = byte;
            continue;
        }

        if (s_civ_len < CIV_MAX_FRAME) {
            s_civ_buf[s_civ_len++] = byte;
        } else {
            s_civ_len = 0;
        }

        if (byte == 0xFD && s_civ_len >= 6) {
            civ_process_frame(s_civ_buf, s_civ_len);
            s_civ_len = 0;
        }
    }
}

bool hamview_icom_get_state(hamview_icom_state_t *out)
{
    if (!out) {
        return false;
    }
    state_lock();
    *out = s_state;
    state_unlock();
    return true;
}
