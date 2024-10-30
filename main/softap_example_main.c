#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <string.h>


#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define BARCOS 20
#define TURNOS_INIT 30
#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN 1024
#define INPUT 12
#define mensaje_inicio "Bienvenido a Battleship"
#define mensaje_repetido "Ya has disparado en esta posición"
#define mensaje_finbtn "Fin de la partida por boton"
#define mensaje_fin_ganaste "Felicidades, has ganado"
#define mensaje_fin_turnos "Se acabaron los turnos"
#define mensaje_agua "Agua"
#define mensaje_barco "Barco"


uint32_t actualInput1 = 0;
volatile uint8_t FLAG = 0;
static const char *TAG = "web_server";
const char mi_ssid[] = "BATTLESHIP";
char* caracter;
char mensaje[35]={0};
uint8_t tablero[10][10] = {0};
uint8_t puntuacion = 0;
uint8_t turnos = TURNOS_INIT;
uint8_t estadoJuego = 0;


void IRAM_ATTR INPUT_ISR(void *args){
    uint8_t estadoBoton = gpio_get_level(INPUT);
    if(estadoBoton){
        actualInput1 = esp_timer_get_time();
    }else{
        if((esp_timer_get_time() - actualInput1) > 9000){
            FLAG =1;
        }
    }
}
 

 esp_err_t init_ports(){ 
    gpio_reset_pin(INPUT);
    gpio_set_direction(INPUT, GPIO_MODE_INPUT);
    gpio_pullup_dis(INPUT);
    gpio_pulldown_en(INPUT);
    gpio_set_intr_type(INPUT, GPIO_INTR_ANYEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(INPUT, INPUT_ISR, NULL);
    return ESP_OK;
 }

char* imprimirMensaje(){
    if (estadoJuego == 0){
        return mensaje_inicio;
    } else if(estadoJuego==1){
        return mensaje_agua;
    } else if(estadoJuego==2){
        return mensaje_barco;
    } else if(estadoJuego==3){
        return mensaje_repetido;
    } else if(estadoJuego==4){
        return mensaje_finbtn;
    } else if(estadoJuego==5){
        return mensaje_fin_ganaste;
    } else if(estadoJuego==6){
        return mensaje_fin_turnos;
    }
    return mensaje_inicio;
}

/*
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]
 [0,0,0,0,0,0,0,0,0,0]

 0 = agua
 1 = barco
 2 = agua / hit
 3 = barco / hit
*/

void init_juego(){
    uint8_t barcos = BARCOS;
    for(uint8_t i = 0; i < 10; i++){
        for(uint8_t j = 0; j < 10; j++){
            tablero[i][j] = 0;
        }
    }
    while(barcos > 0){
        uint8_t x = rand() % 10;
        uint8_t y = rand() % 10;
        if(tablero[x][y] == 0){
            tablero[x][y] = 1;
            barcos--;
            ESP_LOGI(TAG, "Barco en %d, %d", x, y);
        }
    }
    puntuacion = 0;
    turnos = TURNOS_INIT;
    FLAG = 0;
    mensaje[0] = '\0';
}

 char imprimirVariables(uint8_t x, uint8_t y){
    if(tablero[x][y] == 0 || tablero[x][y]==1){
            caracter= " ";
            return (*caracter);
        } else if(tablero[x][y]==2){
            caracter= "O";
            return (*caracter);
        } else {
            caracter= "X";
            return (*caracter);
        }
 }

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

void imprimir_tablero(httpd_req_t *req){
    FILE *file = fopen("/spiffs/index.html", "r");
    char* response = (char*)malloc(sizeof(file));
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        char *file_content = (char *)malloc(file_size + 1);

        fread(file_content, 1, file_size, file);
        file_content[file_size] = '\0'; // Null-terminate the string
        fclose(file);
        
        asprintf(&response, file_content,
        imprimirVariables(0,0),imprimirVariables(0,1),imprimirVariables(0,2),imprimirVariables(0,3),imprimirVariables(0,4),imprimirVariables(0,5),imprimirVariables(0,6),imprimirVariables(0,7),imprimirVariables(0,8),imprimirVariables(0,9),
        imprimirVariables(1,0),imprimirVariables(1,1),imprimirVariables(1,2),imprimirVariables(1,3),imprimirVariables(1,4),imprimirVariables(1,5),imprimirVariables(1,6),imprimirVariables(1,7),imprimirVariables(1,8),imprimirVariables(1,9),
        imprimirVariables(2,0),imprimirVariables(2,1),imprimirVariables(2,2),imprimirVariables(2,3),imprimirVariables(2,4),imprimirVariables(2,5),imprimirVariables(2,6),imprimirVariables(2,7),imprimirVariables(2,8),imprimirVariables(2,9),
        imprimirVariables(3,0),imprimirVariables(3,1),imprimirVariables(3,2),imprimirVariables(3,3),imprimirVariables(3,4),imprimirVariables(3,5),imprimirVariables(3,6),imprimirVariables(3,7),imprimirVariables(3,8),imprimirVariables(3,9),
        imprimirVariables(4,0),imprimirVariables(4,1),imprimirVariables(4,2),imprimirVariables(4,3),imprimirVariables(4,4),imprimirVariables(4,5),imprimirVariables(4,6),imprimirVariables(4,7),imprimirVariables(4,8),imprimirVariables(4,9),
        imprimirVariables(5,0),imprimirVariables(5,1),imprimirVariables(5,2),imprimirVariables(5,3),imprimirVariables(5,4),imprimirVariables(5,5),imprimirVariables(5,6),imprimirVariables(5,7),imprimirVariables(5,8),imprimirVariables(5,9),
        imprimirVariables(6,0),imprimirVariables(6,1),imprimirVariables(6,2),imprimirVariables(6,3),imprimirVariables(6,4),imprimirVariables(6,5),imprimirVariables(6,6),imprimirVariables(6,7),imprimirVariables(6,8),imprimirVariables(6,9),
        imprimirVariables(7,0),imprimirVariables(7,1),imprimirVariables(7,2),imprimirVariables(7,3),imprimirVariables(7,4),imprimirVariables(7,5),imprimirVariables(7,6),imprimirVariables(7,7),imprimirVariables(7,8),imprimirVariables(7,9),
        imprimirVariables(8,0),imprimirVariables(8,1),imprimirVariables(8,2),imprimirVariables(8,3),imprimirVariables(8,4),imprimirVariables(8,5),imprimirVariables(8,6),imprimirVariables(8,7),imprimirVariables(8,8),imprimirVariables(8,9),
        imprimirVariables(9,0),imprimirVariables(9,1),imprimirVariables(9,2),imprimirVariables(9,3),imprimirVariables(9,4),imprimirVariables(9,5),imprimirVariables(9,6),imprimirVariables(9,7),imprimirVariables(9,8),imprimirVariables(9,9),
        puntuacion,turnos,imprimirMensaje());
        httpd_resp_send_chunk(req, response, strlen(response));
        httpd_resp_send_chunk(req, NULL, 0);
        free(response);
}


void hit(uint8_t x , uint8_t y ){
    if(tablero[x][y] == 2 || tablero[x][y] == 3){
        estadoJuego = 3;
        return;
    }
    if(tablero[x][y] == 0){
        turnos--;
        tablero[x][y] = 2;
        estadoJuego = 1;
    }
    if(tablero[x][y] == 1){
        puntuacion++;
        tablero[x][y] = 3;
        estadoJuego = 2;
    }
    ESP_LOGI(TAG, "%c", imprimirVariables(x,y));
    if(puntuacion == BARCOS){
        estadoJuego = 5;
        return;
    } else if (turnos == 0){
        estadoJuego = 6;
        return;
    }
    return;
    //Si el tablero en la posicion x,y es igual a 0 significa que es agua y esto disminuye el contador de turnos y cambia esa posicion a un 2
    //Si el tablero en la posicion x,y es igual a 1 significa que es un barco y esto aumenta la puntuacion y cambia esa posicion a un 3
}

esp_err_t html_get_handler(httpd_req_t *req){
    if(FLAG==0){
        imprimir_tablero(req);
    }else{
        init_juego();
        estadoJuego=4;
        imprimir_tablero(req);
    }
    
    
    return ESP_OK;
}

esp_err_t html_post_handler(httpd_req_t *req){
    char buf[100];
    size_t recv_size = MIN(req->content_len, sizeof(buf));
    if(recv_size >= 8){
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    int ret = httpd_req_recv(req, buf, recv_size);
    if (ret <= 0){
        if (ret == HTTPD_SOCK_ERR_TIMEOUT){
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    char x = buf[2];
    char y = buf[6];
    ESP_LOGI(TAG, "Datos recibidos: %c y %c", x, y);
    hit((uint8_t)x-48, (uint8_t)y-48);
    imprimir_tablero(req);
    if((estadoJuego==5)||(estadoJuego==6)){
        estadoJuego=0;
        init_juego();
    }
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
    init_ports();
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    init_juego();

    init_spiffs();

    wifi_init_softap();

    start_webserver();
}