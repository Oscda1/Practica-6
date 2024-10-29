#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_http_server.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

static const char *TAG = "web_server";
const char mi_ssid[] = "mi_esp_ap";

void init_spiffs(){
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "Error al inicializar SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "Error al obtener información de SPIFFS (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS - Total: %d, Usado: %d", total, used);
    }
}

void wifi_init_softap(void){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(mi_ssid),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    memcpy(wifi_config.ap.ssid, mi_ssid, strlen(mi_ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "ESP32 AP iniciado con SSID: %s, Password: %s channel: %d", wifi_config.ap.ssid, wifi_config.ap.password, wifi_config.ap.channel);
}

esp_err_t html_get_handler(httpd_req_t *req){
    FILE* file = fopen("/spiffs/index.html", "r");
    if (file == NULL){
        ESP_LOGE(TAG, "Error al abrir el archivo index.html");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)){
        httpd_resp_sendstr_chunk(req, line);
    }
    httpd_resp_sendstr_chunk(req, NULL);
    fclose(file);
    return ESP_OK;
}

esp_err_t html_post_handler(httpd_req_t *req){
    char buf[100];
    FILE* file = fopen("/spiffs/index.html", "r");
    if (file == NULL){
        ESP_LOGE(TAG, "Error al abrir el archivo index.html");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    size_t recv_size = MIN(req->content_len, sizeof(buf));
    int ret = httpd_req_recv(req, buf, recv_size);
    if (ret <= 0){
        if (ret == HTTPD_SOCK_ERR_TIMEOUT){
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Datos recibidos: %.*s", ret, buf);
    char line[256];
    while (fgets(line, sizeof(line), file)){
        httpd_resp_sendstr_chunk(req, line);
    }
    httpd_resp_sendstr_chunk(req, NULL);
    fclose(file);
    return ESP_OK;
}

void start_webserver(void){
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK){
        httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = html_get_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(server, &uri_get);

        httpd_uri_t uri_post = {
            .uri = "/",
            .method = HTTP_POST,
            .handler = html_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_post);
    }
}

void app_main(){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    init_spiffs();

    wifi_init_softap();

    start_webserver();
}