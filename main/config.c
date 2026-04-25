#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "server.h"
#include "shared.h"
#include "slvgl.h"
#include "system.h"
#include "timer.h"
#include "ui.h"

#define CONFIG_PATH "/spiffs/user/config/alexa.json"

static const char *TAG = "ALEXA/CONFIG";

bool config_valid = false;
cJSON *wc = NULL;

static char *config_read(void)
{
    char *config = NULL;

    struct stat fs;
    if (stat(CONFIG_PATH, &fs)) {
        if (errno == ENOENT) {
            ESP_LOGI(TAG, "%s does not exist, will be requested from server", CONFIG_PATH);
        } else {
            ESP_LOGE(TAG, "failed to get file status for %s: %s", CONFIG_PATH, strerror(errno));
        }
        return NULL;
    }

    ESP_LOGI(TAG, "opening %s", CONFIG_PATH);
    FILE *f = fopen(CONFIG_PATH, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "failed to open %s", CONFIG_PATH);
        return NULL;
    }

    ESP_LOGI(TAG, "config file size: %ld", fs.st_size);

    config = calloc(sizeof(char), fs.st_size + 1);
    size_t rlen = fread(config, 1, fs.st_size, f);
    ESP_LOGI(TAG, "fread: %d", rlen);
    config[fs.st_size] = '\0';
    ESP_LOGI(TAG, "config file content: %s", config);
    fclose(f);

    return config;
}

bool config_get_bool(char *key, const bool default_value)
{
    bool ret = default_value;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (val != NULL && cJSON_IsBool(val)) {
        ret = cJSON_IsTrue(val) ? true : false;
    } else {
        ret = default_value;
    }
    ESP_LOGD(TAG, "config_get_bool(%s): %s", key, ret ? "true" : "false");
    return ret;
}

char *config_get_char(const char *key, const char *default_value)
{
    char *ret = NULL;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (val != NULL && cJSON_IsString(val) && val->valuestring != NULL) {
        ret = strndup(val->valuestring, strlen(val->valuestring));
    } else {
        ret = default_value == NULL ? NULL : strndup(default_value, strlen(default_value));
    }
    ESP_LOGD(TAG, "config_get_char(%s): %s", key, ret);
    return ret;
}

int config_get_int(char *key, const int default_value)
{
    int ret = -1;
    cJSON *val = cJSON_GetObjectItemCaseSensitive(wc, key);
    if (cJSON_IsNumber(val)) {
        ret = val->valueint;
    } else {
        ret = default_value;
    }
    ESP_LOGD(TAG, "config_get_int(%s): %d", key, ret);
    return ret;
}

void config_parse(void)
{
    char *config = config_read();
    char *json = NULL;

    config_valid = true;

    if (config == NULL) {
        return;
    }

    wc = cJSON_Parse(config);
    if (wc != NULL) {
        config_valid = true;
        json = cJSON_Print(wc);
        ESP_LOGI(TAG, "parsed config file:");
        printf("%s\n", json);
        cJSON_free(json);
    } else {
        const char *eptr = cJSON_GetErrorPtr();
        if (eptr != NULL) {
            ESP_LOGE(TAG, "error parsing config file: %s\n", eptr);
        }
    }
    free(config);
}

void config_write(const char *data)
{
    deinit_server();
    deinit_audio();

    FILE *f = fopen(CONFIG_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "failed to open %s", CONFIG_PATH);
        return;
    }
    fputs(data, f);
    fclose(f);

    ESP_LOGI(TAG, "%s updated, restarting", CONFIG_PATH);
    ui_display_text(NULL, NULL, "Konfiguracja zaktualizowana", NULL, NULL);
    display_wake(true);
    restart_delayed();
}
