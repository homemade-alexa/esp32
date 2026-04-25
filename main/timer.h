#include "esp_timer.h"

#define DEFAULT_DISPLAY_TIMEOUT 10

extern esp_timer_handle_t hdl_display_timer, hdl_sess_timer;

#define REQUEST_TIMEOUT_S 30

esp_err_t init_display_timer(void);
esp_err_t init_dots_timer(void);
esp_err_t init_request_timer(void);
esp_err_t init_session_timer(void);
esp_err_t reset_timer(esp_timer_handle_t hdl, int timeout, bool pause);
void start_dots_anim(void);
void stop_dots_anim(void);
void start_request_timeout(void);
void stop_request_timeout(void);
