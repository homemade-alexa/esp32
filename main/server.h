extern char server_url[2048];

void deinit_server(void);
esp_err_t init_server(void);
void server_send_wakeword(void);
void server_trigger_listen(void);
