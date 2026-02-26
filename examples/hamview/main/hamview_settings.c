#include "hamview_settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "hamview_settings";
static const char *NAMESPACE = "hamview";

static hamview_settings_t s_settings;
static bool s_loaded = false;

static void ensure_defaults(void)
{
    if (strlen(s_settings.filter_band) == 0) {
        strlcpy(s_settings.filter_band, "", sizeof(s_settings.filter_band));
    }
    if (strlen(s_settings.filter_callsign) == 0) {
        strlcpy(s_settings.filter_callsign, "", sizeof(s_settings.filter_callsign));
    }
    if (strlen(s_settings.weather_zip) == 0) {
        strlcpy(s_settings.weather_zip, "", sizeof(s_settings.weather_zip));
    }
    if (strlen(s_settings.icom_wifi_ip) == 0) {
        strlcpy(s_settings.icom_wifi_ip, "", sizeof(s_settings.icom_wifi_ip));
    }
    if (strlen(s_settings.icom_wifi_user) == 0) {
        strlcpy(s_settings.icom_wifi_user, "", sizeof(s_settings.icom_wifi_user));
    }
    if (strlen(s_settings.icom_wifi_pass) == 0) {
        strlcpy(s_settings.icom_wifi_pass, "", sizeof(s_settings.icom_wifi_pass));
    }
    if (s_settings.icom_wifi_port == 0) {
        s_settings.icom_wifi_port = 50001;
    }
    if (strlen(s_settings.alert_callsigns) == 0) {
        strlcpy(s_settings.alert_callsigns, "", sizeof(s_settings.alert_callsigns));
    }
    if (strlen(s_settings.alert_states) == 0) {
        strlcpy(s_settings.alert_states, "", sizeof(s_settings.alert_states));
    }
    if (strlen(s_settings.alert_countries) == 0) {
        strlcpy(s_settings.alert_countries, "", sizeof(s_settings.alert_countries));
    }
    if (s_settings.spot_ttl_minutes == 0 || s_settings.spot_ttl_minutes > 720) {
        s_settings.spot_ttl_minutes = 30;
    }
    if (s_settings.spot_age_filter_minutes > 720) {
        s_settings.spot_age_filter_minutes = 0;
    }
    if (s_settings.screen_timeout_minutes > 720) {
        s_settings.screen_timeout_minutes = 0;
    }
    if (s_settings.screen_brightness_percent == 0 || s_settings.screen_brightness_percent > 100) {
        s_settings.screen_brightness_percent = 100;
    }
}

esp_err_t hamview_settings_init(void)
{
    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.alert_sound_enabled = false;
    s_settings.screen_brightness_percent = 100;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = sizeof(s_settings.username);
    err = nvs_get_str(handle, "username", s_settings.username, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.username[0] = '\0';
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "load username failed: %s", esp_err_to_name(err));
        s_settings.username[0] = '\0';
        err = ESP_OK;
    }

    len = sizeof(s_settings.password);
    esp_err_t err_pass = nvs_get_str(handle, "password", s_settings.password, &len);
    if (err_pass == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.password[0] = '\0';
    } else if (err_pass != ESP_OK) {
        ESP_LOGW(TAG, "load password failed: %s", esp_err_to_name(err_pass));
        s_settings.password[0] = '\0';
    }

    len = sizeof(s_settings.filter_callsign);
    esp_err_t err_call = nvs_get_str(handle, "filter_call", s_settings.filter_callsign, &len);
    if (err_call == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.filter_callsign[0] = '\0';
    } else if (err_call != ESP_OK) {
        ESP_LOGW(TAG, "load filter_call failed: %s", esp_err_to_name(err_call));
        s_settings.filter_callsign[0] = '\0';
    }

    len = sizeof(s_settings.filter_band);
    esp_err_t err_band = nvs_get_str(handle, "filter_band", s_settings.filter_band, &len);
    if (err_band == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.filter_band[0] = '\0';
    } else if (err_band != ESP_OK) {
        ESP_LOGW(TAG, "load filter_band failed: %s", esp_err_to_name(err_band));
        s_settings.filter_band[0] = '\0';
    }

    len = sizeof(s_settings.weather_zip);
    esp_err_t err_zip = nvs_get_str(handle, "weather_zip", s_settings.weather_zip, &len);
    if (err_zip == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.weather_zip[0] = '\0';
    } else if (err_zip != ESP_OK) {
        ESP_LOGW(TAG, "load weather_zip failed: %s", esp_err_to_name(err_zip));
        s_settings.weather_zip[0] = '\0';
    }

    len = sizeof(s_settings.icom_wifi_ip);
    esp_err_t err_icom_ip = nvs_get_str(handle, "icom_ip", s_settings.icom_wifi_ip, &len);
    if (err_icom_ip == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.icom_wifi_ip[0] = '\0';
    } else if (err_icom_ip != ESP_OK) {
        ESP_LOGW(TAG, "load icom_ip failed: %s", esp_err_to_name(err_icom_ip));
        s_settings.icom_wifi_ip[0] = '\0';
    }

    len = sizeof(s_settings.icom_wifi_user);
    esp_err_t err_icom_user = nvs_get_str(handle, "icom_user", s_settings.icom_wifi_user, &len);
    if (err_icom_user == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.icom_wifi_user[0] = '\0';
    } else if (err_icom_user != ESP_OK) {
        ESP_LOGW(TAG, "load icom_user failed: %s", esp_err_to_name(err_icom_user));
        s_settings.icom_wifi_user[0] = '\0';
    }

    len = sizeof(s_settings.icom_wifi_pass);
    esp_err_t err_icom_pass = nvs_get_str(handle, "icom_pass", s_settings.icom_wifi_pass, &len);
    if (err_icom_pass == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.icom_wifi_pass[0] = '\0';
    } else if (err_icom_pass != ESP_OK) {
        ESP_LOGW(TAG, "load icom_pass failed: %s", esp_err_to_name(err_icom_pass));
        s_settings.icom_wifi_pass[0] = '\0';
    }

    uint16_t icom_port = 0;
    esp_err_t err_icom_port = nvs_get_u16(handle, "icom_port", &icom_port);
    if (err_icom_port == ESP_OK) {
        s_settings.icom_wifi_port = icom_port;
    } else if (err_icom_port == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.icom_wifi_port = 0;
    } else {
        ESP_LOGW(TAG, "load icom_port failed: %s", esp_err_to_name(err_icom_port));
        s_settings.icom_wifi_port = 0;
    }

    uint8_t icom_wifi_en = s_settings.icom_wifi_enabled ? 1 : 0;
    esp_err_t err_icom_en = nvs_get_u8(handle, "icom_wifi_en", &icom_wifi_en);
    if (err_icom_en == ESP_OK) {
        s_settings.icom_wifi_enabled = icom_wifi_en != 0;
    } else if (err_icom_en != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load icom_wifi_en failed: %s", esp_err_to_name(err_icom_en));
        s_settings.icom_wifi_enabled = false;
    }

    uint8_t alert_sound = s_settings.alert_sound_enabled ? 1 : 0;
    esp_err_t err_sound = nvs_get_u8(handle, "alert_sound", &alert_sound);
    if (err_sound == ESP_OK) {
        s_settings.alert_sound_enabled = false;
    } else if (err_sound != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load alert_sound failed: %s", esp_err_to_name(err_sound));
    }

    uint8_t brightness = s_settings.screen_brightness_percent;
    esp_err_t err_bright = nvs_get_u8(handle, "screen_bright", &brightness);
    if (err_bright == ESP_OK) {
        s_settings.screen_brightness_percent = brightness;
    } else if (err_bright != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load screen_bright failed: %s", esp_err_to_name(err_bright));
    }

    size_t len_alert = sizeof(s_settings.alert_callsigns);
    esp_err_t err_calls = nvs_get_str(handle, "alert_calls", s_settings.alert_callsigns, &len_alert);
    if (err_calls == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.alert_callsigns[0] = '\0';
    } else if (err_calls != ESP_OK) {
        ESP_LOGW(TAG, "load alert_calls failed: %s", esp_err_to_name(err_calls));
        s_settings.alert_callsigns[0] = '\0';
    }

    len_alert = sizeof(s_settings.alert_states);
    esp_err_t err_states = nvs_get_str(handle, "alert_states", s_settings.alert_states, &len_alert);
    if (err_states == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.alert_states[0] = '\0';
    } else if (err_states != ESP_OK) {
        ESP_LOGW(TAG, "load alert_states failed: %s", esp_err_to_name(err_states));
        s_settings.alert_states[0] = '\0';
    }

    len_alert = sizeof(s_settings.alert_countries);
    esp_err_t err_countries = nvs_get_str(handle, "alert_countries", s_settings.alert_countries, &len_alert);
    if (err_countries == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.alert_countries[0] = '\0';
    } else if (err_countries != ESP_OK) {
        ESP_LOGW(TAG, "load alert_countries failed: %s", esp_err_to_name(err_countries));
        s_settings.alert_countries[0] = '\0';
    }

    uint16_t spot_ttl = 0;
    esp_err_t err_ttl = nvs_get_u16(handle, "spot_ttl", &spot_ttl);
    if (err_ttl == ESP_OK) {
        s_settings.spot_ttl_minutes = spot_ttl;
    } else if (err_ttl == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.spot_ttl_minutes = 0;
    } else {
        ESP_LOGW(TAG, "load spot_ttl failed: %s", esp_err_to_name(err_ttl));
        s_settings.spot_ttl_minutes = 0;
    }

    uint16_t spot_age = 0;
    esp_err_t err_age = nvs_get_u16(handle, "spot_age_filter", &spot_age);
    if (err_age == ESP_OK) {
        s_settings.spot_age_filter_minutes = spot_age;
    } else if (err_age == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.spot_age_filter_minutes = 0;
    } else {
        ESP_LOGW(TAG, "load spot_age_filter failed: %s", esp_err_to_name(err_age));
        s_settings.spot_age_filter_minutes = 0;
    }

    uint16_t screen_timeout = 0;
    esp_err_t err_screen = nvs_get_u16(handle, "screen_timeout", &screen_timeout);
    if (err_screen == ESP_OK) {
        s_settings.screen_timeout_minutes = screen_timeout;
    } else if (err_screen == ESP_ERR_NVS_NOT_FOUND) {
        s_settings.screen_timeout_minutes = 0;
    } else {
        ESP_LOGW(TAG, "load screen_timeout failed: %s", esp_err_to_name(err_screen));
        s_settings.screen_timeout_minutes = 0;
    }

    nvs_close(handle);
    ensure_defaults();
    s_loaded = true;
    ESP_LOGI(TAG, "settings loaded (user=%s)", s_settings.username);
    return err;
}

void hamview_settings_get(hamview_settings_t *out)
{
    if (!s_loaded) {
        hamview_settings_init();
    }
    if (out) {
        *out = s_settings;
    }
}

esp_err_t hamview_settings_save(const hamview_settings_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, "username", settings->username);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "password", settings->password);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "filter_call", settings->filter_callsign);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "filter_band", settings->filter_band);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "weather_zip", settings->weather_zip);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "icom_ip", settings->icom_wifi_ip);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "icom_user", settings->icom_wifi_user);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "icom_pass", settings->icom_wifi_pass);
    if (err != ESP_OK) goto done;

    err = nvs_set_u16(handle, "icom_port", settings->icom_wifi_port);
    if (err != ESP_OK) goto done;

    err = nvs_set_u8(handle, "icom_wifi_en", settings->icom_wifi_enabled ? 1 : 0);
    if (err != ESP_OK) goto done;

    err = nvs_set_u16(handle, "spot_ttl", settings->spot_ttl_minutes);
    if (err != ESP_OK) goto done;

    err = nvs_set_u16(handle, "spot_age_filter", settings->spot_age_filter_minutes);
    if (err != ESP_OK) goto done;

    err = nvs_set_u16(handle, "screen_timeout", settings->screen_timeout_minutes);
    if (err != ESP_OK) goto done;

    err = nvs_set_u8(handle, "alert_sound", settings->alert_sound_enabled ? 1 : 0);
    if (err != ESP_OK) goto done;

    err = nvs_set_u8(handle, "screen_bright", settings->screen_brightness_percent);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "alert_calls", settings->alert_callsigns);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "alert_states", settings->alert_states);
    if (err != ESP_OK) goto done;

    err = nvs_set_str(handle, "alert_countries", settings->alert_countries);
    if (err != ESP_OK) goto done;

    err = nvs_commit(handle);
    if (err == ESP_OK) {
        s_settings = *settings;
        ensure_defaults();
        s_loaded = true;
    }

 done:
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs save failed: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
    return err;
}
