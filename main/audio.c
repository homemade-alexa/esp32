#include "audio_hal.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_thread.h"
#include "board.h"
#include "es7210.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "filter_resample.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "lvgl.h"
#include "model_path.h"
#include "raw_stream.h"
#include "recorder_sr.h"
#include "sdkconfig.h"
#include "spiffs_stream.h"
#include "esp_decoder.h"

#include "cJSON.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "server.h"
#include "shared.h"
#include "slvgl.h"
#include "timer.h"
#include "ui.h"

#if !defined(CONFIG_TASK_WDT_PANIC)
#define CONFIG_TASK_WDT_PANIC 10
#endif

#define DEFAULT_AUDIO_CODEC     "PCM"
#define DEFAULT_RECORD_BUFFER   12
#define DEFAULT_SPEAKER_VOLUME  100
#define DEFAULT_SPEECH_REC_MODE "WIS"
#define DEFAULT_STREAM_TIMEOUT  DEFAULT_LISTEN_TIMEOUT
#define DEFAULT_VAD_MODE        2
#define DEFAULT_VAD_TIMEOUT     750
#define DEFAULT_WAKE_MODE       "2CH_95"
#define DEFAULT_WAKE_WORD       "alexa"
#define DEFAULT_STT_URL         "http://YOUR_SERVER_IP:8080/api/internal/stt"

#define HTTP_STREAM_TIMEOUT_MS              2 * 1000
#define HTTP_STREAM_TIMEOUT_MS_POST_REQUEST 30 * 1000

#define STR_WAKE_LEN 32

QueueHandle_t q_rec;
audio_hal_handle_t hdl_aha = NULL, hdl_ahc = NULL;
audio_rec_handle_t hdl_ar = NULL;
esp_audio_handle_t hdl_ea = NULL;
volatile bool recording = false;
static audio_element_handle_t hdl_ae_hs, hdl_ae_rs_from_i2s, hdl_ae_rs_to_api = NULL;
static audio_pipeline_handle_t hdl_ap, hdl_ap_to_api;
static audio_thread_t hdl_at = NULL;
static bool stream_to_api = false;
static volatile bool manual_trigger = false;
static volatile bool post_stt_display = false;
static char stt_title[64];
static char stt_text[256];
static const char *TAG = "ALEXA/AUDIO";
static int total_write = 0;

void audio_go_idle(void)
{
    stop_request_timeout();
    stop_dots_anim();
    ui_enter_idle();
    display_wake(false);
}

static void play_audio_file(const char *path)
{
    gpio_set_level(get_pa_enable_gpio(), 1);
    esp_audio_play(hdl_ea, AUDIO_CODEC_TYPE_DECODER, path, 0);
}

void play_audio_ok(void *data)
{
    play_audio_file("spiffs://spiffs/user/audio/success.wav");
}

void audio_set_manual_trigger(bool val)
{
    manual_trigger = val;
}

static void play_audio_err(void *data)
{
    play_audio_file("spiffs://spiffs/user/audio/error.wav");
}

static void cb_ea(esp_audio_state_t *state, void *data)
{
    if (state->status > AUDIO_STATUS_RUNNING) {
        gpio_set_level(get_pa_enable_gpio(), 0);
    }
}

static esp_err_t cb_ae_hs(audio_element_handle_t el, audio_event_iface_msg_t *ev, void *data)
{
    if (ev->cmd == AEL_MSG_CMD_REPORT_STATUS) {
        int ae_status = (int)ev->data;
        if (ae_status == 0 || ae_status > 7) {
            return ESP_OK;
        }
        play_audio_err(NULL);
        ESP_LOGE(TAG, "HTTP stream error (%d)", ae_status);
        ui_pr_err("Cannot reach STT server", "Check server & settings");
    }
    return ESP_OK;
}

static void http_stream_setup_timeouts(esp_http_client_handle_t http, http_stream_event_id_t event_id)
{
    if (event_id == HTTP_STREAM_PRE_REQUEST) {
        esp_http_client_set_authtype(http, HTTP_AUTH_TYPE_BASIC);
        esp_http_client_set_timeout_ms(http, HTTP_STREAM_TIMEOUT_MS);
    } else if (event_id == HTTP_STREAM_POST_REQUEST) {
        esp_http_client_set_timeout_ms(http, HTTP_STREAM_TIMEOUT_MS_POST_REQUEST);
    }
}

static esp_err_t hdl_ev_hs_esp_audio(http_stream_event_msg_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    http_stream_setup_timeouts((esp_http_client_handle_t)msg->http_client, msg->event_id);
    return ESP_OK;
}

static void init_esp_audio(void)
{
    audio_err_t ret = ESP_OK;
    esp_audio_cfg_t cfg_ea = {
        .cb_ctx = NULL,
        .cb_func = cb_ea,
        .component_select = ESP_AUDIO_COMPONENT_SELECT_DEFAULT,
        .evt_que = NULL,
        .in_stream_buf_size = 10 * 1024,
        .out_stream_buf_size = 4 * 1024,
        .prefer_type = ESP_AUDIO_PREFER_SPEED,
        .resample_rate = 16000,
        .task_prio = 6,
        .task_stack = 4 * 1024,
        .vol_get = (audio_volume_get)audio_hal_get_volume,
        .vol_handle = hdl_ahc,
        .vol_set = (audio_volume_set)audio_hal_set_volume,
    };

    hdl_ea = esp_audio_create(&cfg_ea);

    http_stream_cfg_t cfg_hs = HTTP_STREAM_CFG_DEFAULT();
    cfg_hs.event_handle = hdl_ev_hs_esp_audio;

    audio_element_handle_t hdl_ae_hs_ea = http_stream_init(&cfg_hs);
    audio_element_set_event_callback(hdl_ea, cb_ae_hs, NULL);

    ret = esp_audio_input_stream_add(hdl_ea, hdl_ae_hs_ea);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add HTTP input stream to ESP Audio");
    }

    spiffs_stream_cfg_t cfg_ss = SPIFFS_STREAM_CFG_DEFAULT();
    cfg_ss.type = AUDIO_STREAM_READER;

    ret = esp_audio_input_stream_add(hdl_ea, spiffs_stream_init(&cfg_ss));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add SPIFFS input stream to ESP Audio");
    }

    // Decoder for WAV (chimes)
    audio_decoder_t ad[] = {
        DEFAULT_ESP_WAV_DECODER_CONFIG(),
    };

    esp_decoder_cfg_t cfg_dec = {
        .out_rb_size = ESP_DECODER_RINGBUFFER_SIZE,
        .plus_enable = true,
        .stack_in_ext = true,
        .task_core = 0,
        .task_prio = ESP_DECODER_TASK_PRIO,
        .task_stack = ESP_DECODER_TASK_STACK_SIZE,
    };

    ret = esp_audio_codec_lib_add(hdl_ea, AUDIO_CODEC_TYPE_DECODER,
                                  esp_decoder_init(&cfg_dec, ad, sizeof(ad) / sizeof(audio_decoder_t)));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add decoder to ESP Audio");
    }

    i2s_stream_cfg_t cfg_is = I2S_STREAM_CFG_DEFAULT_WITH_PARA(CODEC_ADC_I2S_PORT, 16000, I2S_DATA_BIT_WIDTH_32BIT, AUDIO_STREAM_WRITER);
    cfg_is.buffer_len = I2S_STREAM_BUF_SIZE;
    cfg_is.need_expand = true;
    cfg_is.expand_src_bits = I2S_DATA_BIT_WIDTH_16BIT;
    cfg_is.out_rb_size = 8 * 1024;
    cfg_is.uninstall_drv = true;

    ret = esp_audio_output_stream_add(hdl_ea, i2s_stream_init(&cfg_is));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to add I2S output stream to ESP Audio");
    }
    esp_audio_vol_set(hdl_ea, config_get_int("speaker_volume", DEFAULT_SPEAKER_VOLUME));
    ESP_LOGI(TAG, "audio player initialized");
}

static esp_err_t cb_ar_event(audio_rec_evt_t *are, void *data)
{
    int msg = -1;

    switch (are->type) {
        case AUDIO_REC_VAD_END:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_END");
            if (esp_timer_is_active(hdl_sess_timer)) {
                esp_timer_stop(hdl_sess_timer);
            }
            break;

        case AUDIO_REC_VAD_START:
            ESP_LOGI(TAG, "AUDIO_REC_VAD_START");
            if (recording) {
                break;
            }
            recording = true;
            msg = MSG_START;
            xQueueSend(q_rec, &msg, 0);
            break;

        case AUDIO_REC_WAKEUP_END:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_END");
            if (recording) {
                msg = MSG_STOP;
                xQueueSend(q_rec, &msg, 0);
            } else {
                audio_go_idle();
            }
            break;

        case AUDIO_REC_WAKEUP_START:
            ESP_LOGI(TAG, "AUDIO_REC_WAKEUP_START");
            if (recording) {
                break;
            }

            if (!manual_trigger) {
                server_send_wakeword();

                // Play wake confirmation chime only on real wake word (not server trigger)
                if (config_get_bool("wake_confirmation", DEFAULT_WAKE_CONFIRMATION)) {
                    play_audio_ok(NULL);
                }
            }

            manual_trigger = false;
            // Enter LISTENING_MODE
            reset_timer(hdl_sess_timer, config_get_int("listen_timeout", DEFAULT_LISTEN_TIMEOUT), false);
            ui_enter_listen(post_stt_display ? stt_title : NULL,
                            post_stt_display ? stt_text  : NULL);
            post_stt_display = false;
            display_wake(true);
            break;

        default:
            ESP_LOGI(TAG, "cb_ar_event: unhandled event: %d", are->type);
            break;
    }

    return ESP_OK;
}

static int feed_afe(int16_t *buf, int len, void *ctx, TickType_t ticks)
{
    if (buf == NULL || hdl_ae_rs_from_i2s == NULL) {
        return -1;
    }
    return raw_stream_read(hdl_ae_rs_from_i2s, (char *)buf, len);
}

static esp_err_t hdl_ev_hs_to_stt(http_stream_event_msg_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[16];
    int wlen = 0;

    switch (msg->event_id) {
        case HTTP_STREAM_PRE_REQUEST:
            ESP_LOGI(TAG, "STT HTTP stream starting");
            http_stream_setup_timeouts(http, msg->event_id);
            esp_http_client_set_method(http, HTTP_METHOD_POST);
            // Audio format headers
            char dat[10] = {0};
            snprintf(dat, sizeof(dat), "%d", 16000);
            esp_http_client_set_header(http, "x-audio-sample-rate", dat);
            memset(dat, 0, sizeof(dat));
            snprintf(dat, sizeof(dat), "%d", 16);
            esp_http_client_set_header(http, "x-audio-bits", dat);
            memset(dat, 0, sizeof(dat));
            snprintf(dat, sizeof(dat), "%d", 1);
            esp_http_client_set_header(http, "x-audio-channel", dat);
            esp_http_client_set_header(http, "x-audio-codec", "pcm");
            total_write = 0;
            return ESP_OK;

        case HTTP_STREAM_ON_REQUEST:
            wlen = sprintf(len_buf, "%x\r\n", msg->buffer_len);
            if (esp_http_client_write(http, len_buf, wlen) <= 0) {
                return ESP_FAIL;
            }
            if (esp_http_client_write(http, msg->buffer, msg->buffer_len) <= 0) {
                return ESP_FAIL;
            }
            if (esp_http_client_write(http, "\r\n", 2) <= 0) {
                return ESP_FAIL;
            }
            total_write += msg->buffer_len;
            return msg->buffer_len;

        case HTTP_STREAM_POST_REQUEST:
            ESP_LOGI(TAG, "STT HTTP_STREAM_POST_REQUEST, sending end chunk");
            http_stream_setup_timeouts(http, msg->event_id);
            if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
                return ESP_FAIL;
            }
            return ESP_OK;

        case HTTP_STREAM_FINISH_REQUEST:
            ESP_LOGI(TAG, "STT HTTP_STREAM_FINISH_REQUEST - audio sent (%d bytes)", total_write);
            int http_status = esp_http_client_get_status_code(http);
            if (http_status != 200) {
                ESP_LOGE(TAG, "STT server returned HTTP %d", http_status);
                ui_pr_err("STT error", NULL);
                play_audio_err(NULL);
                audio_go_idle();
                return ESP_OK;
            }
            char resp_buf[256] = {0};
            esp_http_client_read_response(http, resp_buf, sizeof(resp_buf) - 1);
            ESP_LOGI(TAG, "STT accepted: %s", resp_buf);
            cJSON *resp_json = cJSON_Parse(resp_buf);
            if (resp_json) {
                cJSON *json_command = cJSON_GetObjectItemCaseSensitive(resp_json, "command");
                if (cJSON_IsString(json_command) && json_command->valuestring[0]) {
                    if (lvgl_port_lock(lvgl_lock_timeout)) {
                        lv_label_set_text(lbl_ln5, json_command->valuestring);
                        ui_labels_set_visible(UI_LBL_LN5, true);
                        lvgl_port_unlock();
                    }
                }
                cJSON_Delete(resp_json);
            }
            return ESP_OK;

        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t init_ap_to_stt(void)
{
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();
    hdl_ap_to_api = audio_pipeline_init(&cfg_ap);

    http_stream_cfg_t cfg_hs = HTTP_STREAM_CFG_DEFAULT();
    cfg_hs.event_handle = hdl_ev_hs_to_stt;
    cfg_hs.task_stack = 8 * 1024;
    cfg_hs.type = AUDIO_STREAM_WRITER;
    cfg_hs.user_agent = WILLOW_USER_AGENT;
    hdl_ae_hs = http_stream_init(&cfg_hs);

    audio_element_set_event_callback(hdl_ae_hs, cb_ae_hs, NULL);

    raw_stream_cfg_t cfg_rs = RAW_STREAM_CFG_DEFAULT();
    cfg_rs.out_rb_size = 64 * 1024;
    cfg_rs.type = AUDIO_STREAM_WRITER;
    hdl_ae_rs_to_api = raw_stream_init(&cfg_rs);

    audio_pipeline_register(hdl_ap_to_api, hdl_ae_hs, "http_stream_writer");
    audio_pipeline_register(hdl_ap_to_api, hdl_ae_rs_to_api, "raw_stream_writer_to_api");

    char *stt_url = config_get_char("stt_url", DEFAULT_STT_URL);
    const char *tag_link[2] = {"raw_stream_writer_to_api", "http_stream_writer"};
    audio_pipeline_link(hdl_ap_to_api, &tag_link[0], 2);
    audio_element_set_uri(hdl_ae_hs, stt_url);
    ESP_LOGI(TAG, "STT URL: %s", stt_url);
    free(stt_url);

    return ESP_OK;
}

static esp_err_t start_rec(void)
{
    audio_element_handle_t hdl_ae_is;
    audio_pipeline_cfg_t cfg_ap = DEFAULT_AUDIO_PIPELINE_CONFIG();
    esp_err_t ret = ESP_OK;

    hdl_ap = audio_pipeline_init(&cfg_ap);
    if (hdl_ap == NULL) {
        return ESP_FAIL;
    }

    i2s_stream_cfg_t cfg_is = {
        .buffer_len = I2S_STREAM_BUF_SIZE,
        .chan_cfg = {
            .auto_clear = true,
            .dma_desc_num = 3,
            .dma_frame_num = 312,
            .id = CODEC_ADC_I2S_PORT,
            .role = I2S_ROLE_MASTER,
        },
        .expand_src_bits = I2S_DATA_BIT_WIDTH_16BIT,
        .multi_out_num = 0,
        .need_expand = false,
        .out_rb_size = 8 * 1024,
        .stack_in_ext = false,
        .std_cfg = {
            .clk_cfg = {
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .sample_rate_hz = 44100,
            },
            .gpio_cfg = {
                .invert_flags = {
                    .bclk_inv = false,
                    .mclk_inv = false,
                },
            },
            .slot_cfg = {
                .big_endian = false,
                .bit_order_lsb = false,
                .bit_shift = true,
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .left_align = true,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mask = I2S_STD_SLOT_BOTH,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .ws_pol = false,
                .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            },
        },
        .task_core = I2S_STREAM_TASK_CORE,
        .task_prio = I2S_STREAM_TASK_PRIO,
        .task_stack = I2S_STREAM_TASK_STACK,
        .type = AUDIO_STREAM_READER,
        .uninstall_drv = true,
        .use_alc = false,
        .volume = 0,
    };
    hdl_ae_is = i2s_stream_init(&cfg_is);
    i2s_stream_set_clk(hdl_ae_is, 16000, 32, 2);

    raw_stream_cfg_t cfg_rs = RAW_STREAM_CFG_DEFAULT();
    cfg_rs.type = AUDIO_STREAM_READER;
    hdl_ae_rs_from_i2s = raw_stream_init(&cfg_rs);

    audio_pipeline_register(hdl_ap, hdl_ae_is, "i2s_stream_reader");
    audio_pipeline_register(hdl_ap, hdl_ae_rs_from_i2s, "raw_stream_reader");

    const char *tag_link[2] = {"i2s_stream_reader", "raw_stream_reader"};
    audio_pipeline_link(hdl_ap, &tag_link[0], 2);
    audio_pipeline_run(hdl_ap);

    char *wake_mode = config_get_char("wake_mode", DEFAULT_WAKE_MODE);
    int wakenet_mode = -1;
    if (strcmp(wake_mode, "2CH_90") == 0) {
        wakenet_mode = DET_MODE_2CH_90;
    } else if (strcmp(wake_mode, "2CH_95") == 0) {
        wakenet_mode = DET_MODE_2CH_95;
    } else if (strcmp(wake_mode, "1CH_90") == 0) {
        wakenet_mode = DET_MODE_90;
    } else if (strcmp(wake_mode, "1CH_95") == 0) {
        wakenet_mode = DET_MODE_95;
    } else if (strcmp(wake_mode, "3CH_90") == 0) {
        wakenet_mode = DET_MODE_3CH_90;
    } else if (strcmp(wake_mode, "3CH_95") == 0) {
        wakenet_mode = DET_MODE_3CH_95;
    }
    free(wake_mode);

    afe_config_t cfg_afe = {
        .aec_init = config_get_bool("aec", true),
        .se_init = config_get_bool("bss", false),
        .vad_init = true,
        .wakenet_init = true,
        .voice_communication_init = false,
        .voice_communication_agc_init = false,
        .voice_communication_agc_gain = 15,
        .vad_mode = config_get_int("vad_mode", DEFAULT_VAD_MODE),
        .wakenet_mode = wakenet_mode,
        .wakenet_model_name = NULL,
        .afe_linear_gain = 1.0,
        .afe_mode = SR_MODE_HIGH_PERF,
        .afe_perferred_core = 1,
        .afe_perferred_priority = 5,
        .afe_ringbuf_size = 50,
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_3,
        .pcm_config.total_ch_num = 3,
        .pcm_config.mic_num = 2,
        .pcm_config.ref_num = 1,
        .pcm_config.sample_rate = 16000,
        .debug_init = false,
        .debug_hook = {{AFE_DEBUG_HOOK_MASE_TASK_IN, NULL}, {AFE_DEBUG_HOOK_FETCH_TASK_IN, NULL}},
    };

    char *wake_word = config_get_char("wake_word", DEFAULT_WAKE_WORD);
    recorder_sr_cfg_t cfg_srr = {
        .afe_cfg = cfg_afe,
        .input_order = INPUT_ORDER_DEFAULT(),
        .multinet_init = false,
        .feed_task_core = FEED_TASK_PINNED_CORE,
        .feed_task_prio = FEED_TASK_PRIO,
        .feed_task_stack = FEED_TASK_STACK_SZ,
        .fetch_task_core = FETCH_TASK_PINNED_CORE,
        .fetch_task_prio = FETCH_TASK_PRIO,
        .fetch_task_stack = FETCH_TASK_STACK_SZ,
        .rb_size = config_get_int("record_buffer", DEFAULT_RECORD_BUFFER) * 1024,
        .partition_label = "model",
        .wn_wakeword = wake_word,
    };

    audio_rec_cfg_t cfg_ar = {
        .pinned_core = AUDIO_REC_DEF_TASK_CORE,
        .task_prio = AUDIO_REC_DEF_TASK_PRIO,
        .task_size = AUDIO_REC_DEF_TASK_SZ,
        .event_cb = cb_ar_event,
        .user_data = NULL,
        .read = (recorder_data_read_t)&feed_afe,
        .sr_handle = NULL,
        .sr_iface = NULL,
        .wakeup_time = AUDIO_REC_DEF_WAKEUP_TM,
        .vad_start = AUDIO_REC_VAD_START_SPEECH_MS,
        .vad_off = config_get_int("vad_timeout", DEFAULT_VAD_TIMEOUT),
        .wakeup_end = 1,
        .encoder_handle = NULL,
        .encoder_iface = NULL,
    };
    cfg_ar.sr_handle = recorder_sr_create(&cfg_srr, &cfg_ar.sr_iface);
    free(wake_word);

    if (cfg_ar.sr_handle == NULL) {
        ESP_LOGE(TAG, "failed to init SR recorder");
        ui_pr_err("Recorder init failed", "Check logs");
        return ESP_FAIL;
    }

    hdl_ar = audio_recorder_create(&cfg_ar);
    return ret;
}

static void reset_stt_pipeline(void)
{
    audio_pipeline_stop(hdl_ap_to_api);
    audio_pipeline_wait_for_stop(hdl_ap_to_api);
    audio_pipeline_reset_ringbuffer(hdl_ap_to_api);
    audio_pipeline_reset_elements(hdl_ap_to_api);
}

static void at_read(void *data)
{
    const int len = 2 * 1024;
    char *buf = audio_calloc(1, len);
    int msg = -1, ret = 0;
    TickType_t delay = portMAX_DELAY;

    while (true) {
        if (xQueueReceive(q_rec, &msg, delay) == pdTRUE) {
            switch (msg) {
                case MSG_START:
                    delay = 0;
                    recording = true;
                    reset_stt_pipeline();
                    audio_pipeline_run(hdl_ap_to_api);
                    stream_to_api = true;
                    ESP_LOGI(TAG, "Recording started, streaming to: %s", audio_element_get_uri(hdl_ae_hs));
                    break;

                case MSG_STOP:
                    delay = portMAX_DELAY;
                    audio_element_set_ringbuf_done(hdl_ae_rs_to_api);
                    recording = false;
                    stream_to_api = false;
                    // Keep screen on and freeze timers for the duration of the HTTP request
                    reset_timer(hdl_sess_timer, 0, true);
                    display_wake(true);
                    ui_enter_request();
                    start_dots_anim();
                    start_request_timeout();
                    break;

                case MSG_CANCEL:
                    delay = portMAX_DELAY;
                    recording = false;
                    stream_to_api = false;
                    reset_stt_pipeline();
                    audio_go_idle();
                    break;

                case MSG_LISTEN:
                    delay = portMAX_DELAY;
                    // Pause from at_read context (safe, no deadlock unlike from HTTP callback)
                    // MSG_START will then do the full stop+reset+run as in the original flow
                    audio_pipeline_pause(hdl_ap_to_api);
                    audio_recorder_trigger_start(hdl_ar);
                    break;

                default:
                    ESP_LOGW(TAG, "at_read(): unknown msg %d", msg);
                    break;
            }
        }

        if (stream_to_api) {
            ret = audio_recorder_data_read(hdl_ar, buf, len, portMAX_DELAY);
            if (ret > 0) {
                raw_stream_write(hdl_ae_rs_to_api, buf, ret);
            }
        }
    }

    free(buf);
    vTaskDelete(NULL);
}

esp_err_t volume_set(int volume)
{
    if (volume < 0) {
        volume = config_get_int("speaker_volume", DEFAULT_SPEAKER_VOLUME);
    }
    return audio_hal_set_volume(hdl_ahc, volume);
}

esp_err_t init_audio(void)
{
    esp_err_t ret = ESP_OK;
    int gpio_level;

    hdl_ahc = audio_board_codec_init();
    hdl_aha = audio_board_adc_init();
    gpio_set_level(get_pa_enable_gpio(), 0);
    ret = audio_hal_ctrl_codec(hdl_ahc, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    ESP_LOGI(TAG, "audio_hal_ctrl_codec: %s", esp_err_to_name(ret));
    init_esp_audio();
    volume_set(-1);

    gpio_level = gpio_get_level(GPIO_NUM_1);
    if (gpio_level == 0) {
        ESP_LOGW(TAG, "mute is activated, please unmute to continue startup");
        ui_pr_err("Mute Activated", "Unmute to continue");
        while (gpio_get_level(GPIO_NUM_1) == 0) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    init_session_timer();
    init_dots_timer();
    init_request_timer();
    init_ap_to_stt();
    ESP_RETURN_ON_ERROR(start_rec(), TAG, "start_rec failed");
    es7210_adc_set_gain(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 | ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4,
                        (es7210_gain_value_t)config_get_int("mic_gain", DEFAULT_MIC_GAIN));

    q_rec = xQueueCreate(3, sizeof(int));
    audio_thread_create(&hdl_at, "at_read", at_read, NULL, 4 * 1024, 5, true, 0);

    if (ld != NULL) {
        audio_go_idle();
    }

    ESP_LOGI(TAG, "init_audio() complete, waiting for wake word");
    return ret;
}

void deinit_audio(void)
{
    if (hdl_ar != NULL) {
        audio_recorder_destroy(hdl_ar);
    }
    if (hdl_at != NULL) {
        vTaskDelete(hdl_at);
    }
    if (hdl_ap != NULL) {
        audio_pipeline_stop(hdl_ap);
        audio_pipeline_wait_for_stop(hdl_ap);
        audio_pipeline_terminate(hdl_ap);
    }
    if (hdl_ap_to_api != NULL) {
        audio_pipeline_stop(hdl_ap_to_api);
        audio_pipeline_wait_for_stop(hdl_ap_to_api);
        audio_pipeline_terminate(hdl_ap_to_api);
    }
    if (hdl_ea != NULL) {
        esp_audio_destroy(hdl_ea);
    }
}
