#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "config.h"
#include "shared.h"
#include "slvgl.h"
#include "ui.h"

static const char *TAG = "ALEXA/UI";

void ui_labels_set_visible(uint8_t mask, bool visible)
{
    lv_obj_t *objs[] = {lbl_ln1, lbl_ln2, lbl_ln3, lbl_ln4, lbl_ln5, btn_cancel};
    for (int i = 0; i < 6; i++) {
        if (mask & (1 << i)) {
            if (visible) lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_display_text(const char *ln1, const char *ln2, const char *ln3, const char *ln4, const char *ln5)
{
    if (!lvgl_port_lock(lvgl_lock_timeout)) {
        return;
    }
    const char *texts[] = {ln1, ln2, ln3, ln4, ln5};
    lv_obj_t *objs[]    = {lbl_ln1, lbl_ln2, lbl_ln3, lbl_ln4, lbl_ln5};
    for (int i = 0; i < 5; i++) {
        if (texts[i] != NULL) {
            lv_label_set_text(objs[i], texts[i]);
            lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}

void init_ui(void)
{
    if (ld == NULL) {
        ESP_LOGE(TAG, "lv_disp_t ld is NULL!!!!");
    } else {
        if (lvgl_port_lock(lvgl_lock_timeout)) {
            lv_obj_t *scr_act = lv_disp_get_scr_act(ld);
            lbl_hdr = lv_label_create(scr_act);
            btn_cancel = lv_btn_create(scr_act);
            lbl_btn_cancel = lv_label_create(btn_cancel);
            lbl_ln1 = lv_label_create(scr_act);
            lbl_ln2 = lv_label_create(scr_act);
            lbl_ln3 = lv_label_create(scr_act);
            lbl_ln4 = lv_label_create(scr_act);
            lbl_ln5 = lv_label_create(scr_act);
            lv_obj_add_event_cb(scr_act, cb_scr, LV_EVENT_ALL, NULL);

            lv_obj_set_style_bg_color(scr_act, lv_color_hex(0x583759), LV_PART_MAIN);
            lv_obj_set_style_text_color(scr_act, lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xfbe870), LV_PART_MAIN);
            lv_obj_set_style_text_color(btn_cancel, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl_btn_cancel, lv_color_hex(0x000000), LV_PART_MAIN);

#ifdef CONFIG_LV_USE_FS_POSIX
            lv_font_t *lv_font_alexa = lv_font_load("A/spiffs/user/font/tonnelier.bin");
            ESP_LOGI(TAG, "lv_font_load: %s", lv_font_alexa ? "OK" : "FAILED - using default font");
            if (lv_font_alexa != NULL) {
                static lv_style_t lv_st_alexa;
                lv_style_init(&lv_st_alexa);
                lv_style_set_text_font(&lv_st_alexa, lv_font_alexa);
                lv_obj_add_style(lbl_hdr, &lv_st_alexa, 0);
                lv_obj_add_style(lbl_ln1, &lv_st_alexa, 0);
                lv_obj_add_style(lbl_ln2, &lv_st_alexa, 0);
                lv_obj_add_style(lbl_ln3, &lv_st_alexa, 0);
                lv_obj_add_style(lbl_ln4, &lv_st_alexa, 0);
                lv_obj_add_style(lbl_ln5, &lv_st_alexa, 0);
                lv_obj_add_style(lbl_btn_cancel, &lv_st_alexa, 0);
            }
#endif

            lv_label_set_text_static(lbl_btn_cancel, "Anuluj");
            lv_label_set_text_static(lbl_hdr, "A L E X A");
            ui_labels_set_visible(UI_LBL_LN1 | UI_LBL_LN2 | UI_LBL_LN4 | UI_LBL_LN5 | UI_LBL_BTN, false);
            lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_align(lbl_btn_cancel, LV_ALIGN_CENTER, 0, 0);
            lv_obj_align(lbl_hdr, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_align(lbl_ln1, LV_ALIGN_TOP_LEFT, 10, 40);
            lv_obj_align(lbl_ln2, LV_ALIGN_TOP_LEFT, 10, 70);
            lv_obj_align(lbl_ln3, LV_ALIGN_CENTER, 0, -30);
            lv_obj_align(lbl_ln4, LV_ALIGN_TOP_LEFT, 10, 130);
            lv_obj_set_style_text_align(lbl_ln4, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl_ln5, LV_ALIGN_TOP_MID, 0, 130);
            lv_obj_set_style_text_align(lbl_ln5, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(lbl_ln2, LV_LABEL_LONG_SCROLL);
            lv_obj_set_width(lbl_ln1, 300);
            lv_obj_set_width(lbl_ln2, 300);
            lv_obj_set_width(lbl_ln3, 300);
            lv_obj_set_width(lbl_ln4, 300);
            lv_obj_set_width(lbl_ln5, 300);
            lv_obj_set_style_text_align(lbl_ln3, LV_TEXT_ALIGN_CENTER, 0);

            lv_label_set_text_static(lbl_ln3, "Uruchamianie...");
            lv_obj_clear_flag(lbl_ln3, LV_OBJ_FLAG_HIDDEN);

            lvgl_port_unlock();
        }
    }
}

void ui_enter_idle(void)
{
    if (!lvgl_port_lock(lvgl_lock_timeout)) {
        return;
    }
    lv_label_set_text_static(lbl_hdr, "A L E X A");
    lv_label_set_text_static(lbl_ln4, "Powiedz 'Alexa', by zacząć rozmowę");
    ui_labels_set_visible(UI_LBL_LN1 | UI_LBL_LN2 | UI_LBL_LN3 | UI_LBL_LN5 | UI_LBL_BTN, false);
    ui_labels_set_visible(UI_LBL_LN4, true);
    lvgl_port_unlock();
}

void ui_enter_request(void)
{
    if (!lvgl_port_lock(lvgl_lock_timeout)) {
        return;
    }
    lv_label_set_text_static(lbl_hdr, "A L E X A");
    ui_labels_set_visible(UI_LBL_LN1 | UI_LBL_LN2 | UI_LBL_LN4 | UI_LBL_LN5 | UI_LBL_BTN, false);
    ui_labels_set_visible(UI_LBL_LN3, true);
    lvgl_port_unlock();
}

void ui_enter_listen(const char *title, const char *text)
{
    if (!lvgl_port_lock(lvgl_lock_timeout)) {
        return;
    }
    lv_label_set_text_static(lbl_hdr, "(( A L E X A ))");
    ui_labels_set_visible(UI_LBL_LN4 | UI_LBL_LN5, false);
    ui_labels_set_visible(UI_LBL_BTN, true);
    lv_obj_add_event_cb(btn_cancel, cb_btn_cancel, LV_EVENT_PRESSED, NULL);
    bool has_content = (title && title[0]) || (text && text[0]);
    if (title && title[0]) lv_label_set_text(lbl_ln1, title);
    ui_labels_set_visible(UI_LBL_LN1, title && title[0]);
    if (text && text[0]) lv_label_set_text(lbl_ln2, text);
    ui_labels_set_visible(UI_LBL_LN2, text && text[0]);
    if (!has_content) lv_label_set_text_static(lbl_ln3, "Słucham...");
    ui_labels_set_visible(UI_LBL_LN3, !has_content);
    lvgl_port_unlock();
}

void ui_set_status(const char *msg)
{
    if (ld == NULL || msg == NULL) {
        return;
    }
    if (lvgl_port_lock(lvgl_lock_timeout)) {
        lv_label_set_text(lbl_ln3, msg);
        ui_labels_set_visible(UI_LBL_LN3, true);
        lvgl_port_unlock();
    }
}

void ui_pr_err(char *ln3, char *ln4)
{
    if (ld == NULL) {
        ESP_LOGE(TAG, "display not initialized");
        if (ln3 != NULL) {
            ESP_LOGE(TAG, "%s", ln3);
        }
        if (ln4 != NULL) {
            ESP_LOGE(TAG, "%s", ln4);
        }
        return;
    }

    if (lvgl_port_lock(lvgl_lock_timeout)) {
        ui_labels_set_visible(UI_LBL_LN1 | UI_LBL_LN2 | UI_LBL_LN5, false);

        if (ln3 != NULL) {
            lv_label_set_text(lbl_ln3, ln3);
            lv_obj_set_style_text_align(lbl_ln3, LV_TEXT_ALIGN_CENTER, 0);
        }
        ui_labels_set_visible(UI_LBL_LN3, ln3 != NULL);

        if (ln4 != NULL) {
            lv_label_set_text(lbl_ln4, ln4);
            lv_obj_set_style_text_align(lbl_ln4, LV_TEXT_ALIGN_CENTER, 0);
        }
        ui_labels_set_visible(UI_LBL_LN4, ln4 != NULL);

        lvgl_port_unlock();
    }
}
