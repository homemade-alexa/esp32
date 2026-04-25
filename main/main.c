#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "periph_spiffs.h"
#include "sdkconfig.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "log.h"
#include "network.h"
#include "server.h"
#include "shared.h"
#include "slvgl.h"
#include "system.h"
#include "tasks.h"
#include "timer.h"
#include "ui.h"

#define PARTLABEL_USER     "user"
#define DEFAULT_SERVER_URL "ws://YOUR_SERVER_IP:8080/ws/internal"

char server_url[2048];
static const char *TAG = "ALEXA/MAIN";
enum willow_state state;

esp_periph_set_handle_t hdl_pset;

static esp_err_t init_spiffs_user(void)
{
    esp_err_t ret = ESP_OK;
    periph_spiffs_cfg_t pcfg_spiffs_user = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .partition_label = PARTLABEL_USER,
        .root = "/spiffs/user",
    };
    esp_periph_handle_t phdl_spiffs_user = periph_spiffs_init(&pcfg_spiffs_user);
    ret = esp_periph_start(hdl_pset, phdl_spiffs_user);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start spiffs user peripheral: %s", esp_err_to_name(ret));
        return ret;
    }

    while (!periph_spiffs_is_mounted(phdl_spiffs_user)) {
        ESP_LOGI(TAG, "Waiting on SPIFFS mount...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "SPIFFS mounted");
    return ret;
}

void app_main(void)
{
    state = STATE_INIT;
    esp_err_t err;

    init_logging();
    ESP_LOGI(TAG, "Alexa starting up...");

    esp_periph_config_t pcfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    hdl_pset = esp_periph_set_init(&pcfg);

    init_system();
    init_spiffs_user();
    config_parse();
    init_display();
    init_lvgl_display();
    init_ui();

    ESP_ERROR_CHECK(esp_netif_init());

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    nvs_handle_t hdl_nvs;
    char psk[64] = CONFIG_WIFI_PASSWORD;
    char ssid[33] = CONFIG_WIFI_SSID;
    size_t sz;

    err = nvs_open("WIFI", NVS_READONLY, &hdl_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WIFI not in NVS, using Kconfig defaults (SSID: %s)", ssid);
    } else {
        sz = sizeof(psk);
        if (nvs_get_str(hdl_nvs, "PSK", psk, &sz) != ESP_OK) {
            ESP_LOGW(TAG, "WIFI PSK not in NVS, using Kconfig default");
        }
        sz = sizeof(ssid);
        if (nvs_get_str(hdl_nvs, "SSID", ssid, &sz) != ESP_OK) {
            ESP_LOGW(TAG, "WIFI SSID not in NVS, using Kconfig default");
        }
    }
    init_wifi(psk, ssid);

    err = nvs_open("SERVER", NVS_READONLY, &hdl_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SERVER not in NVS, using default: %s", DEFAULT_SERVER_URL);
        strncpy(server_url, DEFAULT_SERVER_URL, sizeof(server_url) - 1);
    } else {
        sz = sizeof(server_url);
        if (nvs_get_str(hdl_nvs, "URL", server_url, &sz) != ESP_OK) {
            ESP_LOGW(TAG, "server URL not in NVS, using default: %s", DEFAULT_SERVER_URL);
            strncpy(server_url, DEFAULT_SERVER_URL, sizeof(server_url) - 1);
        }
    }

    state = STATE_NVS_OK;
    err = init_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize server connection");
        ui_pr_err("Fatal error!", "Server connection failed.");
    }

    if (!config_valid) {
        // wait for config from server
        vTaskDelay(portMAX_DELAY);
    }

    if (state < STATE_NVS_OK) {
        ui_pr_err("Fatal error!", "Failed to read NVS partition.");
        vTaskDelay(portMAX_DELAY);
    }

    ui_set_status("Inicjalizacja audio...");
    init_buttons();
    init_input_key_service();
    init_display_timer();
    init_audio();
    init_lvgl_touch();

    get_mac_address();

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Startup complete! Hardware: %s. Version: %s. Waiting for 'Alexa'.", str_hw_type(hw_type),
             app_desc->version);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_mark_app_valid_cancel_rollback());

#ifdef CONFIG_WILLOW_DEBUG_RUNTIME_STATS
    xTaskCreate(&task_debug_runtime_stats, "dbg_runtime_stats", 4 * 1024, NULL, 0, NULL);
#endif

    while (true) {
#ifdef CONFIG_WILLOW_DEBUG_MEM
        printf("MALLOC_CAP_INTERNAL:\n");
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
        printf("MALLOC_CAP_SPIRAM:\n");
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
#endif
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
