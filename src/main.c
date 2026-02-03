#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>
#include "ble_parser.h"
#include "webserver.h"

static const char *TAG = "BLE_SCAN";
static const char *WIFI_TAG = "WiFi";

#define MAX_DEVICES 50
#define WIFI_SSID "Kontu"
#define WIFI_PASS "8765432A1"
#define MAX_NAME_LEN 32
#define NVS_NAMESPACE "devices"

typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    uint32_t last_seen;
    bool visible;  // Näkyykö laite
    char name[MAX_NAME_LEN];
    bool has_sensor_data;
    float temperature;
    uint8_t humidity;
    uint8_t battery_pct;
    uint16_t battery_mv;
} ble_device_t;

static ble_device_t devices[MAX_DEVICES];
static int device_count = 0;
static httpd_handle_t server = NULL;

// Tallenna laitteen näkyvyysasetus NVS:ään
static void save_visibility(uint8_t *addr, bool visible) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        char key[20];
        snprintf(key, sizeof(key), "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        uint8_t val = visible ? 1 : 0;
        ESP_LOGI(TAG, "NVS tallennetaan: %s -> %d", key, val);
        nvs_set_u8(nvs, key, val);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// Lataa laitteen näkyvyysasetus NVS:stä (oletuksena näkyvissä)
static bool load_visibility(uint8_t *addr) {
    nvs_handle_t nvs;
    uint8_t val = 1;  // Oletuksena näkyvissä
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        char key[20];
        snprintf(key, sizeof(key), "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        esp_err_t err = nvs_get_u8(nvs, key, &val);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS ladattu: %s -> visible=%d", key, val);
        } else {
            ESP_LOGI(TAG, "NVS: %s ei löytynyt (err=%d), oletusarvo visible=1", key, err);
        }
        nvs_close(nvs);
    }
    return val ? true : false;
}


static int find_or_add_device(uint8_t *addr) {
    // Etsi onko laite jo listassa
    for (int i = 0; i < device_count; i++) {
        if (memcmp(devices[i].addr, addr, 6) == 0) {
            return i;
        }
    }
    
    // Lisää uusi laite
    if (device_count < MAX_DEVICES) {
        memcpy(devices[device_count].addr, addr, 6);
        devices[device_count].visible = load_visibility(addr);
        memset(devices[device_count].name, 0, MAX_NAME_LEN);
        devices[device_count].has_sensor_data = false;
        ESP_LOGI(TAG, "Uusi laite löydetty");
        return device_count++;
    }
    return -1;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        int idx = find_or_add_device(event->disc.addr.val);
        if (idx >= 0) {
            devices[idx].rssi = event->disc.rssi;
            devices[idx].last_seen = xTaskGetTickCount();
            
            // Parsitaan mainosdata
            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
                
                // Laitteen nimi
                if (fields.name != NULL && fields.name_len > 0) {
                    int copy_len = (fields.name_len < MAX_NAME_LEN - 1) ? fields.name_len : MAX_NAME_LEN - 1;
                    memcpy(devices[idx].name, fields.name, copy_len);
                    devices[idx].name[copy_len] = '\0';
                }
                
                // Sensoridata (pvvx/ATC-muoto)
                if (fields.svc_data_uuid16 != NULL && fields.svc_data_uuid16_len >= 13) {
                    uint16_t uuid = fields.svc_data_uuid16[0] | (fields.svc_data_uuid16[1] << 8);
                    
                    if (uuid == 0x181A) {
                        ble_sensor_data_t sensor_data;
                        bool parsed = false;
                        
                        if (fields.svc_data_uuid16_len >= 17) {
                            parsed = ble_parse_pvvx_format(fields.svc_data_uuid16, fields.svc_data_uuid16_len, &sensor_data);
                        } else if (fields.svc_data_uuid16_len >= 15) {
                            parsed = ble_parse_atc_format(fields.svc_data_uuid16, fields.svc_data_uuid16_len, &sensor_data);
                        }
                        
                        if (parsed) {
                            devices[idx].temperature = sensor_data.temperature;
                            devices[idx].humidity = sensor_data.humidity;
                            devices[idx].battery_pct = sensor_data.battery_pct;
                            devices[idx].battery_mv = sensor_data.battery_mv;
                            devices[idx].has_sensor_data = true;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static void ble_app_on_sync(void) {
    struct ble_gap_disc_params disc_params = {0};
    disc_params.itvl = 0x10;
    disc_params.window = 0x10;
    disc_params.passive = 1;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
}

static void host_task(void *param) {
    nimble_port_run();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(WIFI_TAG, "WiFi käynnistetty, yhdistetään...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(WIFI_TAG, "WiFi katkesi, yhdistetään uudelleen...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "✓ Yhdistetty! IP-osoite: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(WIFI_TAG, "Avaa selaimessa: http://" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void) {
    ESP_LOGI(WIFI_TAG, "Alustetaan WiFi...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_LOGI(WIFI_TAG, "Yhdistetään verkkoon: %s", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ============================================
// WEB-KÄYTTÖLIITTYMÄ
// ============================================

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// API: Palauta kaikki NÄKYVÄT laitteet JSON-muodossa (tai kaikki jos ?all=1)
static esp_err_t api_devices_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    // Tarkista onko parametri ?all=1
    bool show_all = false;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char all_param[8];
        if (httpd_query_key_value(query, "all", all_param, sizeof(all_param)) == ESP_OK) {
            show_all = (strcmp(all_param, "1") == 0);
        }
    }
    
    ESP_LOGI(TAG, "API /api/devices kutsuttu, laitteet yhteensä: %d, show_all=%d", device_count, show_all);
    
    char *json = malloc(16384);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    strcpy(json, "[");
    bool first = true;
    
    for (int i = 0; i < device_count; i++) {
        // Näytä vain näkyvät laitteet (paitsi jos show_all=true)
        if (!show_all && !devices[i].visible) {
            ESP_LOGI(TAG, "Laite %d piilotettu, ohitetaan", i);
            continue;
        }
        
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
            devices[i].addr[0], devices[i].addr[1], devices[i].addr[2],
            devices[i].addr[3], devices[i].addr[4], devices[i].addr[5]);
        
        char item[512];
        if (devices[i].has_sensor_data) {
            snprintf(item, sizeof(item),
                "%s{\"addr\":\"%s\",\"name\":\"%s\",\"rssi\":%d,"
                "\"hasSensor\":true,\"temp\":%.1f,\"hum\":%d,\"bat\":%d,\"batMv\":%d,\"visible\":%s}",
                first ? "" : ",",
                addr_str,
                devices[i].name[0] ? devices[i].name : "Unknown",
                devices[i].rssi,
                devices[i].temperature,
                devices[i].humidity,
                devices[i].battery_pct,
                devices[i].battery_mv,
                devices[i].visible ? "true" : "false");
        } else {
            snprintf(item, sizeof(item),
                "%s{\"addr\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"hasSensor\":false,\"visible\":%s}",
                first ? "" : ",",
                addr_str,
                devices[i].name[0] ? devices[i].name : "Unknown",
                devices[i].rssi,
                devices[i].visible ? "true" : "false");
        }
        strcat(json, item);
        first = false;
    }
    strcat(json, "]");
    
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// API: Vaihda laitteen näkyvyys
static esp_err_t api_toggle_visibility_handler(httpd_req_t *req) {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parsitaan addr ja visible parametrit
    char addr_str[64] = {0};  // Tarpeeksi iso URL-enkoodatulle osoitteelle
    int visible = 1;
    
    char *addr_param = strstr(content, "addr=");
    char *visible_param = strstr(content, "visible=");
    
    if (addr_param) {
        sscanf(addr_param, "addr=%60[^&]", addr_str);  // Kasvatettu 18 -> 60
        
        ESP_LOGI(TAG, "URL-enkoodattu osoite: %s", addr_str);
        
        // URL-dekoodaus (muuta %3A tai %3a takaisin :)
        char decoded[20];
        int j = 0;
        for (int i = 0; addr_str[i] && j < sizeof(decoded) - 1; i++) {
            if (addr_str[i] == '%' && addr_str[i+1] && addr_str[i+2]) {
                if ((addr_str[i+1] == '3' || addr_str[i+1] == '3') && 
                    (addr_str[i+2] == 'A' || addr_str[i+2] == 'a')) {
                    decoded[j++] = ':';
                    i += 2;
                } else {
                    decoded[j++] = addr_str[i];
                }
            } else {
                decoded[j++] = addr_str[i];
            }
        }
        decoded[j] = '\0';
        strcpy(addr_str, decoded);
        
        ESP_LOGI(TAG, "Dekoodattu osoite: %s", addr_str);
    }
    
    if (visible_param) {
        sscanf(visible_param, "visible=%d", &visible);
    }
    
    ESP_LOGI(TAG, "API pyydetään piilottamaan: %s -> visible=%d", addr_str, visible);
    
    // Etsi laite ja päivitä sen tila
    for (int i = 0; i < device_count; i++) {
        char dev_addr[18];
        snprintf(dev_addr, sizeof(dev_addr), "%02X:%02X:%02X:%02X:%02X:%02X",
            devices[i].addr[0], devices[i].addr[1], devices[i].addr[2],
            devices[i].addr[3], devices[i].addr[4], devices[i].addr[5]);
        
        if (strcmp(dev_addr, addr_str) == 0) {
            ESP_LOGI(TAG, "Laite löytyi indeksistä %d, vanha visible=%d", i, devices[i].visible);
            devices[i].visible = visible ? true : false;
            save_visibility(devices[i].addr, devices[i].visible);
            ESP_LOGI(TAG, "✓ Laitteen %d näkyvyys päivitetty -> %d", i, devices[i].visible);
            break;
        }
    }
    
    ESP_LOGI(TAG, "Vastaus lähetetty");
    
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root);
        
        httpd_uri_t api_devices = {
            .uri = "/api/devices",
            .method = HTTP_GET,
            .handler = api_devices_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_devices);
        
        httpd_uri_t api_toggle_visibility = {
            .uri = "/api/toggle-visibility",
            .method = HTTP_POST,
            .handler = api_toggle_visibility_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_toggle_visibility);
        
        ESP_LOGI(TAG, "HTTP-palvelin käynnistetty");
    }
}

void app_main() {
    ESP_LOGI(TAG, "BLE Scanner + Web UI käynnistyy");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_close(nvs);
    }
    
    wifi_init();
    start_webserver();
    
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);
}
