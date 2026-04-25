#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "audio.h"
#include "display.h"
#include "shared.h"
#include "slvgl.h"
#include "tasks.h"
#include "timer.h"

static const char *TAG = "ALEXA/TIMER";
esp_timer_handle_t hdl_display_timer = NULL, hdl_sess_timer = NULL;

static esp_timer_handle_t hdl_dots_timer    = NULL;
static esp_timer_handle_t hdl_request_timer = NULL;
static int dots_state = 0;
static const char *dots_labels[] = {
    ".", ". .", ". . .", ". . . .", ". . . . .", ". . . .", ". . .", ". ."
};
static const int ANIM_FRAMES_COUNT = 8;

static void timer_stop(esp_timer_handle_t hdl)
{
    if (esp_timer_is_active(hdl)) {
        esp_timer_stop(hdl);
    }
}

static esp_err_t create_timer(esp_timer_cb_t cb, const char *name, esp_timer_handle_t *hdl)
{
    const esp_timer_create_args_t cfg = { .callback = cb, .name = name };
    return esp_timer_create(&cfg, hdl);
}

static void cb_display_timer(void *data)
{
    ESP_LOGI(TAG, "Wake LCD timeout, turning off LCD");
    display_set_backlight(false, false);
}

static void cb_session_timer(void *data)
{
    if (recording) {
        ESP_LOGI(TAG, "session timer expired - forcing end stream");
        audio_recorder_trigger_stop(hdl_ar);
        int msg = MSG_STOP;
        xQueueSend(q_rec, &msg, 0);
    }
}

static void cb_dots_timer(void *data)
{
    dots_state = (dots_state + 1) % ANIM_FRAMES_COUNT;
    if (lvgl_port_lock(lvgl_lock_timeout)) {
        lv_label_set_text(lbl_ln3, dots_labels[dots_state]);
        lvgl_port_unlock();
    }
}

static void cb_request_timer(void *data)
{
    ESP_LOGW(TAG, "REQUEST mode timeout — forcing IDLE");
    audio_go_idle();
}

esp_err_t init_display_timer(void) { return create_timer(cb_display_timer, "display_timer", &hdl_display_timer); }
esp_err_t init_session_timer(void) { return create_timer(cb_session_timer, "session_timer", &hdl_sess_timer);    }
esp_err_t init_dots_timer(void)    { return create_timer(cb_dots_timer,    "dots_timer",    &hdl_dots_timer);    }
esp_err_t init_request_timer(void) { return create_timer(cb_request_timer, "request_timer", &hdl_request_timer); }

void start_dots_anim(void)
{
    dots_state = 0;
    if (lvgl_port_lock(lvgl_lock_timeout)) {
        lv_label_set_text(lbl_ln3, dots_labels[0]);
        lvgl_port_unlock();
    }
    timer_stop(hdl_dots_timer);
    esp_timer_start_periodic(hdl_dots_timer, 500 * 1000);
}

void stop_dots_anim(void)        { timer_stop(hdl_dots_timer);    }
void start_request_timeout(void) { reset_timer(hdl_request_timer, REQUEST_TIMEOUT_S, false); }
void stop_request_timeout(void)  { timer_stop(hdl_request_timer); }

esp_err_t reset_timer(esp_timer_handle_t hdl, int timeout, bool pause)
{
    timer_stop(hdl);
    if (pause) {
        return ESP_OK;
    }
    return esp_timer_start_once(hdl, timeout * 1000 * 1000);
}
