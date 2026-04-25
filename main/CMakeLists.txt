set(COMPONENT_SRCS
    "audio.c"
    "config.c"
    "display.c"
    "http.c"
    "input.c"
    "log.c"
    "lv_img_hand_left.c"
    "main.c"
    "network.c"
    "ota.c"
    "server.c"
    "slvgl.c"
    "system.c"
    "tasks.c"
    "timer.c"
    "ui.c"
)
set(COMPONENT_ADD_INCLUDEDIRS .)
set(COMPONENT_PRIV_REQUIRES
    app_update
    audio_board
    audio_hal
    audio_pipeline
    audio_recorder
    audio_sal
    audio_stream
    esp_http_client
    esp_peripherals
    esp_websocket_client
    input_key_service
    spiffs
)

register_component()

spiffs_create_partition_image(user ../spiffs/user FLASH_IN_PROJECT)

string(APPEND CMAKE_C_FLAGS -Werror)
