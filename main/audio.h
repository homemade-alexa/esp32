#include "audio_recorder.h"
#include "esp_audio.h"

#define DEFAULT_WAKE_CONFIRMATION false

typedef enum {
    MSG_STOP,
    MSG_START,
    MSG_CANCEL,
    MSG_LISTEN,
} q_msg;

extern audio_rec_handle_t hdl_ar;
extern volatile bool recording;
extern esp_audio_handle_t hdl_ea;
extern QueueHandle_t q_rec;

void audio_go_idle(void);
void audio_set_manual_trigger(bool val);
void deinit_audio(void);
esp_err_t init_audio(void);
void play_audio_ok(void *data);
esp_err_t volume_set(int volume);
