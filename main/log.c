#include "esp_log.h"

#ifdef CONFIG_WILLOW_DEBUG_LOG
#define WILLOW_LOG_LEVEL ESP_LOG_DEBUG
#else
#define WILLOW_LOG_LEVEL ESP_LOG_INFO
#endif

void init_logging(void)
{
#ifdef CONFIG_WILLOW_DEBUG_LOG
    esp_log_level_set("*", ESP_LOG_DEBUG);
#else
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_RECORDER", ESP_LOG_INFO);
#endif

    esp_log_level_set("ALEXA/AUDIO", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/CONFIG", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/DISPLAY", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/ETHERNET", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/HASS", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/HTTP", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/INPUT", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/LVGL", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/MAIN", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/NETWORK", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/OPENHAB", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/OTA", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/REST", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/SYSTEM", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/TIMER", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/UI", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/WAS", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/AUDIO", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/MAIN", WILLOW_LOG_LEVEL);
    esp_log_level_set("ALEXA/UI", WILLOW_LOG_LEVEL);
}
