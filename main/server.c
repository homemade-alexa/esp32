#include "cJSON.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "network.h"
#include "ota.h"
#include "server.h"
#include "shared.h"
#include "slvgl.h"
#include "system.h"
#include "timer.h"
#include "ui.h"

#define SERVER_RECONNECT_TIMEOUT_MS 10 * 1000

static const char *TAG = "ALEXA/SERVER";
static esp_websocket_client_handle_t hdl_wc = NULL;

esp_netif_t *hdl_netif;

static void send_hello(void);
static bool server_is_connected(void);

static void ws_send_json(cJSON *root)
{
    char *json = cJSON_Print(root);
    esp_websocket_client_send_text(hdl_wc, json, strlen(json), 2000 / portTICK_PERIOD_MS);
    cJSON_free(json);
    cJSON_Delete(root);
}

// Trigger LISTENING_MODE without wake chime (called from server command)
void server_send_wakeword(void)
{
    if (!server_is_connected()) {
        return;
    }
    cJSON *cjson = cJSON_CreateObject();
    cJSON_AddStringToObject(cjson, "event", "wakeword");
    ws_send_json(cjson);
}

void server_trigger_listen(void)
{
    stop_request_timeout();
    stop_dots_anim();
    ui_enter_listen(NULL, NULL);
    display_wake(true);
    reset_timer(hdl_sess_timer, config_get_int("listen_timeout", DEFAULT_LISTEN_TIMEOUT), false);
    audio_set_manual_trigger(true);
    audio_recorder_trigger_start(hdl_ar);
}

static void handle_ws_command(cJSON *cjson)
{
    cJSON *json_cmd = cJSON_GetObjectItemCaseSensitive(cjson, "cmd");
    if (!cJSON_IsString(json_cmd) || json_cmd->valuestring == NULL) {
        return;
    }

    ESP_LOGI(TAG, "command: %s", json_cmd->valuestring);

    if (strcmp(json_cmd->valuestring, "listen") == 0) {
        server_trigger_listen();
    }
    else if (strcmp(json_cmd->valuestring, "display") == 0) {
        cJSON *json_data = cJSON_GetObjectItemCaseSensitive(cjson, "data");
        if (cJSON_IsObject(json_data)) {
            cJSON *json_text  = cJSON_GetObjectItemCaseSensitive(json_data, "text");
            cJSON *json_title = cJSON_GetObjectItemCaseSensitive(json_data, "title");

            if (lvgl_port_lock(lvgl_lock_timeout)) {
                bool has_title = cJSON_IsString(json_title) && json_title->valuestring != NULL;
                bool has_text  = cJSON_IsString(json_text)  && json_text->valuestring  != NULL;
                if (has_title) lv_label_set_text(lbl_ln1, json_title->valuestring);
                if (has_text)  lv_label_set_text(lbl_ln2, json_text->valuestring);
                ui_labels_set_visible(UI_LBL_LN1, has_title);
                ui_labels_set_visible(UI_LBL_LN2, has_text);
                ui_labels_set_visible(UI_LBL_LN3 | UI_LBL_LN4 | UI_LBL_LN5 | UI_LBL_BTN, false);
                lvgl_port_unlock();
            }
            display_wake(false);
        }
    }
    else if (strcmp(json_cmd->valuestring, "ota_start") == 0) {
        cJSON *json_ota_url = cJSON_GetObjectItemCaseSensitive(cjson, "ota_url");
        if (cJSON_IsString(json_ota_url) && json_ota_url->valuestring != NULL) {
            char *ota_url = strndup(json_ota_url->valuestring, strlen(json_ota_url->valuestring));
            ESP_LOGI(TAG, "OTA URL: %s", ota_url);
            ota_start(ota_url);
        }
    }
    else if (strcmp(json_cmd->valuestring, "idle") == 0) {
        ESP_LOGI(TAG, "idle command received");
        audio_go_idle();
    }
    else if (strcmp(json_cmd->valuestring, "restart") == 0) {
        ESP_LOGI(TAG, "restart command received");
        deinit_server();
        restart_delayed();
    }
}

static void IRAM_ATTR cb_ws_event(const void *arg_evh, const esp_event_base_t *base_ev, const int32_t id_ev,
                                  const void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;

    switch (id_ev) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            send_hello();
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code != WS_TRANSPORT_OPCODES_TEXT) {
                break;
            }

            char *resp = strndup((char *)data->data_ptr, data->data_len);
            ESP_LOGI(TAG, "received: %s", resp);
            cJSON *cjson = cJSON_Parse(resp);
            handle_ws_command(cjson);
            cJSON_Delete(cjson);
            free(resp);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            break;

        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGI(TAG, "WebSocket closed - reconnecting");
            init_server();
            break;

        default:
            ESP_LOGD(TAG, "unhandled WebSocket event: %" PRIu32, id_ev);
            break;
    }
}

static bool server_is_connected(void)
{
    return esp_websocket_client_is_connected(hdl_wc);
}

static void send_hello(void)
{
    if (!server_is_connected()) {
        return;
    }

    const char *hostname;
    uint8_t mac[6];
    esp_err_t ret;

    ret = esp_netif_get_hostname(hdl_netif, &hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get hostname");
        return;
    }

    ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to get MAC address");
        return;
    }

    cJSON *cjson = cJSON_CreateObject();
    cJSON *hello = cJSON_CreateObject();
    cJSON *mac_arr = cJSON_CreateArray();

    for (int i = 0; i < 6; i++) {
        cJSON_AddItemToArray(mac_arr, cJSON_CreateNumber(mac[i]));
    }

    cJSON_AddStringToObject(hello, "hostname", hostname);
    cJSON_AddStringToObject(hello, "hw_type", str_hw_type(hw_type));
    cJSON_AddItemToObjectCS(hello, "mac_addr", mac_arr);
    cJSON_AddItemToObjectCS(cjson, "hello", hello);

    ws_send_json(cjson);
}

static void server_deinit_task(void *data)
{
    esp_websocket_client_close(hdl_wc, 5000 / portTICK_PERIOD_MS);
    esp_websocket_client_stop(hdl_wc);
    vTaskDelete(NULL);
}

void deinit_server(void)
{
    restarting = true;
    xTaskCreate(&server_deinit_task, "server_deinit_task", 4096, NULL, 5, NULL);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
}

esp_err_t init_server(void)
{
    if (restarting) {
        return ESP_OK;
    }

    const esp_websocket_client_config_t cfg_wc = {
        .buffer_size = 4096,
        .reconnect_timeout_ms = SERVER_RECONNECT_TIMEOUT_MS,
        .uri = server_url,
        .user_agent = WILLOW_USER_AGENT,
    };

    ui_display_text(NULL, NULL, "Łączenie z serwerem...", NULL, NULL);

    ESP_LOGI(TAG, "initializing WebSocket client (%s)", server_url);

    hdl_wc = esp_websocket_client_init(&cfg_wc);

    esp_err_t err = esp_websocket_client_destroy_on_exit(hdl_wc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to enable destroy on exit: %s", esp_err_to_name(err));
    }

    esp_websocket_register_events(hdl_wc, WEBSOCKET_EVENT_ANY, (esp_event_handler_t)cb_ws_event, NULL);
    err = esp_websocket_client_start(hdl_wc);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
    }
    return err;
}
