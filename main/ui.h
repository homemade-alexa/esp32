#define UI_LBL_LN1  (1 << 0)
#define UI_LBL_LN2  (1 << 1)
#define UI_LBL_LN3  (1 << 2)
#define UI_LBL_LN4  (1 << 3)
#define UI_LBL_LN5  (1 << 4)
#define UI_LBL_BTN  (1 << 5)
#define UI_LBL_ALL  (0x3F)

void init_ui(void);
void ui_enter_idle(void);
void ui_enter_listen(const char *title, const char *text);
void ui_enter_request(void);
void ui_labels_set_visible(uint8_t mask, bool visible);
void ui_display_text(const char *ln1, const char *ln2, const char *ln3, const char *ln4, const char *ln5);
void ui_pr_err(char *ln3, char *ln4);
void ui_set_status(const char *msg);