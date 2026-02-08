#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>
#include "ble_parser.h"
#include "webserver.h"
#include "setup_page.h"

static const char *TAG = "BLE_SCAN";
static const char *WIFI_TAG = "WiFi";
static const char *AIO_TAG = "AdafruitIO";

#define MAX_DEVICES 50
#define MAX_NAME_LEN 32
#define NVS_NAMESPACE "devices"
#define NVS_WIFI_NAMESPACE "wifi"
#define NVS_AIO_NAMESPACE "aio"

#define BOOT_BUTTON_GPIO 0
#define BOOT_HOLD_TIME_MS 5000
#define AIO_SEND_INTERVAL_MS (5 * 60 * 1000)  // 5 minuuttia
typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    uint32_t last_seen;
    bool visible;  // Näkyykö laite
    char name[MAX_NAME_LEN];
    bool show_mac;  // Näytetäänkö MAC-osoite
    uint16_t field_mask;  // Bitmask: mitä kenttiä näytetään (temp, hum, bat, batMv, rssi)
    bool has_sensor_data;
    float temperature;
    uint8_t humidity;
    uint8_t battery_pct;
    uint16_t battery_mv;
} ble_device_t;

// Field mask bitit
#define FIELD_TEMP   (1 << 0)
#define FIELD_HUM    (1 << 1)
#define FIELD_BAT    (1 << 2)
#define FIELD_BATMV  (1 << 3)
#define FIELD_RSSI   (1 << 4)
#define FIELD_ALL    0xFFFF  // Kaikki kentät oletuksena

static ble_device_t devices[MAX_DEVICES];
static int device_count = 0;
static httpd_handle_t server = NULL;
static bool setup_mode = false;
static char wifi_ssid[64] = {0};
static char wifi_password[64] = {0};
static char aio_username[64] = {0};
static char aio_key[128] = {0};
static bool aio_enabled = false;
static esp_timer_handle_t aio_timer = NULL;

// Skannauksen hallinta
// HUOM: Skannaus pyörii JATKUVASTI, mutta uusia laitteita lisätään vain discovery-moden aikana
static bool allow_new_devices = false;  // Sallitaanko uusien laitteiden lisääminen (discovery mode)
static esp_timer_handle_t discovery_timer = NULL;  // Ajastin discovery-moden lopettamiseen
#define DISCOVERY_DURATION_MS 30000  // Discovery-moden kesto millisekunteina (30s)

// Tallenna laitteen asetukset NVS:ään
static void save_device_settings(uint8_t *addr, const char *name, bool show_mac, uint16_t field_mask) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        char base_key[20];
        snprintf(base_key, sizeof(base_key), "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        
        // Tallenna nimi
        if (name && strlen(name) > 0) {
            char name_key[24];
            snprintf(name_key, sizeof(name_key), "%s_n", base_key);
            nvs_set_str(nvs, name_key, name);
        }
        
        // Tallenna show_mac
        char mac_key[24];
        snprintf(mac_key, sizeof(mac_key), "%s_m", base_key);
        nvs_set_u8(nvs, mac_key, show_mac ? 1 : 0);
        
        // Tallenna field_mask
        char field_key[24];
        snprintf(field_key, sizeof(field_key), "%s_f", base_key);
        nvs_set_u16(nvs, field_key, field_mask);
        
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Tallennettu asetukset: %s, name=%s, show_mac=%d, fields=0x%04X", 
                 base_key, name, show_mac, field_mask);
    }
}

// Lataa laitteen asetukset NVS:stä
static void load_device_settings(uint8_t *addr, char *name_out, bool *show_mac_out, uint16_t *field_mask_out) {
    nvs_handle_t nvs;
    // Oletusarvot
    name_out[0] = '\0';
    *show_mac_out = true;
    *field_mask_out = FIELD_ALL;
    
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        char base_key[20];
        snprintf(base_key, sizeof(base_key), "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        
        // Lataa nimi
        char name_key[24];
        snprintf(name_key, sizeof(name_key), "%s_n", base_key);
        size_t name_len = MAX_NAME_LEN;
        nvs_get_str(nvs, name_key, name_out, &name_len);
        
        // Lataa show_mac
        char mac_key[24];
        snprintf(mac_key, sizeof(mac_key), "%s_m", base_key);
        uint8_t show_mac_val = 1;
        if (nvs_get_u8(nvs, mac_key, &show_mac_val) == ESP_OK) {
            *show_mac_out = show_mac_val ? true : false;
        }
        
        // Lataa field_mask
        char field_key[24];
        snprintf(field_key, sizeof(field_key), "%s_f", base_key);
        nvs_get_u16(nvs, field_key, field_mask_out);
        
        nvs_close(nvs);
    }
}

// Tallenna laitteen näkyvyysasetus NVS:ään
static void save_visibility(uint8_t *addr, bool visible) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        char key[20];
        snprintf(key, sizeof(key), "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        uint8_t val = visible ? 1 : 0;
        ESP_LOGI(TAG, "NVS tallennetaan visibility: %s -> %d", key, val);
        nvs_set_u8(nvs, key, val);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}
static bool load_visibility(uint8_t *addr) {
    nvs_handle_t nvs;
    uint8_t val = 0;  // Oletuksena PIILOTETTU (ei ole vielä valittu päänäkymään)
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        char key[20];
        snprintf(key, sizeof(key), "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
        esp_err_t err = nvs_get_u8(nvs, key, &val);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NVS ladattu: %s -> visible=%d", key, val);
        } else {
            ESP_LOGI(TAG, "NVS: %s ei löytynyt (err=%d), oletusarvo visible=0", key, err);
        }
        nvs_close(nvs);
    }
    return val ? true : false;
}

// Lataa kaikki NVS:ään tallennetut laitteet käynnistyksessä
static void load_all_devices_from_nvs(void) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "NVS ei vielä ole käytössä tai ei laitteita");
        return;
    }
    
    ESP_LOGI(TAG, "Ladataan kaikki laitteet NVS:stä...");
    
    // Käy läpi kaikki NVS-avaimet
    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);
    
    while (res == ESP_OK && device_count < MAX_DEVICES) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        
        // Tarkistetaan onko tämä visibility-avain (12 hex-merkkiä ilman _-päätettä)
        if (strlen(info.key) == 12) {
            bool is_hex = true;
            for (int i = 0; i < 12; i++) {
                if (!((info.key[i] >= '0' && info.key[i] <= '9') ||
                      (info.key[i] >= 'A' && info.key[i] <= 'F'))) {
                    is_hex = false;
                    break;
                }
            }
            
            if (is_hex) {
                // Parsitaan MAC-osoite
                uint8_t addr[6];
                for (int i = 0; i < 6; i++) {
                    char byte_str[3] = {info.key[i*2], info.key[i*2+1], 0};
                    addr[5-i] = (uint8_t)strtol(byte_str, NULL, 16);
                }
                
                // Lisää laite listaan
                memcpy(devices[device_count].addr, addr, 6);
                devices[device_count].visible = load_visibility(addr);
                devices[device_count].rssi = 0;
                devices[device_count].last_seen = 0;
                devices[device_count].has_sensor_data = false;
                
                // Lataa muut asetukset
                load_device_settings(addr, devices[device_count].name,
                                   &devices[device_count].show_mac,
                                   &devices[device_count].field_mask);
                
                ESP_LOGI(TAG, "  Ladattu laite %d: %02X:%02X:%02X:%02X:%02X:%02X, name=%s",
                        device_count,
                        addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
                        devices[device_count].name);
                
                device_count++;
            }
        }
        
        res = nvs_entry_next(&it);
    }
    
    nvs_release_iterator(it);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Ladattu yhteensä %d laitetta NVS:stä", device_count);
}


static int find_or_add_device(uint8_t *addr, bool allow_adding_new) {
    // Etsi onko laite jo listassa
    for (int i = 0; i < device_count; i++) {
        if (memcmp(devices[i].addr, addr, 6) == 0) {
            return i;
        }
    }
    
    // Lisää uusi laite vain jos sallittu (discovery mode päällä)
    if (!allow_adding_new) {
        return -1;  // Ei lisätä uusia laitteita monitoring-moden aikana
    }
    
    // Lisää uusi laite - oletuksena PIILOTETTU (näkyy vain discovery-popupissa)
    if (device_count < MAX_DEVICES) {
        memcpy(devices[device_count].addr, addr, 6);
        devices[device_count].visible = false;  // Piilotettu kunnes käyttäjä lisää päänäkymään
        devices[device_count].rssi = 0;
        devices[device_count].last_seen = 0;
        devices[device_count].has_sensor_data = false;
        
        // Lataa tallennetut asetukset (nimi, show_mac, field_mask)
        load_device_settings(addr, devices[device_count].name, 
                           &devices[device_count].show_mac,
                           &devices[device_count].field_mask);
        
        // Lataa visibility NVS:stä (jos tallennettu)
        devices[device_count].visible = load_visibility(addr);
        
        ESP_LOGI(TAG, "Uusi laite löydetty: %02X:%02X:%02X:%02X:%02X:%02X, name=%s, visible=%d",
                 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
                 devices[device_count].name, devices[device_count].visible);
        return device_count++;
    }
    return -1;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        // Discovery mode: lisää uusia + päivitä kaikkia
        // Monitoring mode: päivitä vain visible=true laitteita
        int idx = find_or_add_device(event->disc.addr.val, allow_new_devices);
        
        // Jos laite ei ole listassa tai ei ole näkyvissä (ja ei olla discovery-modessa), ohita
        if (idx < 0 || (!allow_new_devices && !devices[idx].visible)) {
            return 0;  // Ei päivitetä piilotettuja laitteita monitoring-modessa
        }
        
        if (idx >= 0) {
            devices[idx].rssi = event->disc.rssi;
            devices[idx].last_seen = xTaskGetTickCount();
            
            // Parsitaan mainosdata
            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
                
                // Laitteen nimi (kopioidaan vain jos käyttäjä ei ole asettanut omaa nimeä)
                if (fields.name != NULL && fields.name_len > 0) {
                    if (devices[idx].name[0] == '\0') {
                        int copy_len = (fields.name_len < MAX_NAME_LEN - 1) ? fields.name_len : MAX_NAME_LEN - 1;
                        memcpy(devices[idx].name, fields.name, copy_len);
                        devices[idx].name[copy_len] = '\0';
                        ESP_LOGI(TAG, "BLE-nimi kopioitu: %s", devices[idx].name);
                    }
                } else if (devices[idx].name[0] == '\0') {
                    ESP_LOGI(TAG, "BLE-advertsissä ei nimeä tälle laitteelle");
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

// Timer callback: Lopettaa discovery moden (uusien laitteiden etsinnän)
// Skannaus jatkuu edelleen olemassa olevien laitteiden arvojen päivittämiseksi
static void stop_discovery_timer_callback(void* arg) {
    ESP_LOGI(TAG, "Discovery-ajastin laukesi, lopetetaan uusien laitteiden etsintä");
    ESP_LOGI(TAG, "Skannaus JATKUU olemassa olevien laitteiden arvojen päivittämiseksi");
    allow_new_devices = false;
}

// BLE-stack valmis, käynnistetään jatkuva skannaus
static void ble_app_on_sync(void) {
    ESP_LOGI(TAG, "BLE-stack synkronoitu ja valmis");
    ESP_LOGI(TAG, "Käynnistetään JATKUVA skannaus olemassa olevien laitteiden seurantaan");
    
    // Käynnistä jatkuva skannaus
    struct ble_gap_disc_params disc_params = {0};
    disc_params.itvl = 0x10;
    disc_params.window = 0x10;
    disc_params.passive = 0;  // Active scan jotta saadaan scan response (nimi)
    
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
    
    if (rc != 0) {
        ESP_LOGE(TAG, "Jatkuvan skannauksen käynnistys epäonnistui: %d", rc);
    } else {
        ESP_LOGI(TAG, "✓ Jatkuva skannaus käynnissä (uusia laitteita EI lisätä ennen /api/scan kutsua)");
    }
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

// WiFi NVS -funktiot
static bool load_wifi_config(void) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGW(WIFI_TAG, "WiFi-asetuksia ei löydy NVS:stä");
        return false;
    }
    
    size_t ssid_len = sizeof(wifi_ssid);
    size_t pass_len = sizeof(wifi_password);
    
    esp_err_t err_ssid = nvs_get_str(nvs, "ssid", wifi_ssid, &ssid_len);
    esp_err_t err_pass = nvs_get_str(nvs, "password", wifi_password, &pass_len);
    
    nvs_close(nvs);
    
    if (err_ssid == ESP_OK && err_pass == ESP_OK && strlen(wifi_ssid) > 0) {
        ESP_LOGI(WIFI_TAG, "WiFi-asetukset ladattu: %s", wifi_ssid);
        return true;
    }
    
    return false;
}

static void save_wifi_config(const char* ssid, const char* password) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "NVS:n avaus epäonnistui");
        return;
    }
    
    nvs_set_str(nvs, "ssid", ssid);
    nvs_set_str(nvs, "password", password);
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(WIFI_TAG, "WiFi-asetukset tallennettu");
}

static void clear_wifi_config(void) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(WIFI_TAG, "WiFi-asetukset nollattu");
    }
}

static void check_boot_button(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        ESP_LOGI(WIFI_TAG, "BOOT-nappi pohjassa, tarkistetaan...");
        vTaskDelay(pdMS_TO_TICKS(100));
        
        uint32_t start = esp_timer_get_time() / 1000;
        while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            uint32_t elapsed = (esp_timer_get_time() / 1000) - start;
            if (elapsed >= BOOT_HOLD_TIME_MS) {
                ESP_LOGW(WIFI_TAG, "🔄 BOOT-nappi pidetty 5s - Nollataan WiFi!");
                clear_wifi_config();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(WIFI_TAG, "BOOT-nappi vapautettu liian aikaisin");
    }
}

// Adafruit IO NVS -funktiot
static bool load_aio_config(void) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_AIO_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGW(AIO_TAG, "Adafruit IO -asetuksia ei löydy");
        return false;
    }
    
    size_t user_len = sizeof(aio_username);
    size_t key_len = sizeof(aio_key);
    uint8_t enabled = 0;
    
    esp_err_t err_user = nvs_get_str(nvs, "username", aio_username, &user_len);
    esp_err_t err_key = nvs_get_str(nvs, "key", aio_key, &key_len);
    nvs_get_u8(nvs, "enabled", &enabled);
    
    nvs_close(nvs);
    
    if (err_user == ESP_OK && err_key == ESP_OK && strlen(aio_username) > 0 && strlen(aio_key) > 0) {
        aio_enabled = (enabled != 0);
        ESP_LOGI(AIO_TAG, "Asetukset ladattu: %s, enabled=%d", aio_username, aio_enabled);
        return true;
    }
    
    return false;
}

static void save_aio_config(const char* username, const char* key, bool enabled) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_AIO_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(AIO_TAG, "NVS:n avaus epäonnistui");
        return;
    }
    
    nvs_set_str(nvs, "username", username);
    nvs_set_str(nvs, "key", key);
    nvs_set_u8(nvs, "enabled", enabled ? 1 : 0);
    nvs_commit(nvs);
    nvs_close(nvs);
    
    strncpy(aio_username, username, sizeof(aio_username) - 1);
    strncpy(aio_key, key, sizeof(aio_key) - 1);
    aio_enabled = enabled;
    
    ESP_LOGI(AIO_TAG, "Asetukset tallennettu");
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    
    // Tarkista onko WiFi määritetty
    if (!load_wifi_config()) {
        // EI WiFi-asetuksia -> AP-tila
        ESP_LOGI(WIFI_TAG, "🔧 Setup-tila: Käynnistetään AP-tila");
        setup_mode = true;
        
        esp_netif_create_default_wifi_ap();
        
        wifi_config_t ap_config = {
            .ap = {
                .ssid = "BLE-Monitor-Setup",
                .ssid_len = strlen("BLE-Monitor-Setup"),
                .channel = 1,
                .password = "",
                .max_connection = 4,
                .authmode = WIFI_AUTH_OPEN,
            },
        };
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI(WIFI_TAG, "✓ AP käynnistetty: BLE-Monitor-Setup");
        ESP_LOGI(WIFI_TAG, "Avaa selaimessa: http://192.168.4.1");
    } else {
        // WiFi määritetty -> STA-tila
        ESP_LOGI(WIFI_TAG, "Yhdistetään verkkoon: %s", wifi_ssid);
        setup_mode = false;
        
        esp_netif_create_default_wifi_sta();
        
        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password));
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

// ============================================
// ADAFRUIT IO DATA UPLOAD
// ============================================

static void send_device_to_aio(const ble_device_t *dev) {
    if (!aio_enabled || !dev->has_sensor_data) return;
    
    char url[256];
    char payload[256];
    
    // Feed key: käytä nimeä jos on, muuten MAC
    char feed_key[64];
    char mac_str[20];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
    
    if (strlen(dev->name) > 0) {
        // Muunna nimi feed keyksi: lowercase, välilyönnit → viivat
        char safe_name[MAX_NAME_LEN];
        int j = 0;
        char prev = '\0';
        for (int i = 0; i < strlen(dev->name) && j < sizeof(safe_name) - 1; i++) {
            char c = dev->name[i];
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                safe_name[j++] = c;
                prev = c;
            } else if (c >= 'A' && c <= 'Z') {
                safe_name[j++] = c + 32;  // Lowercase
                prev = c + 32;
            } else if (c == ' ' || c == '_' || c == '-') {
                // Lisää viiva vain jos edellinen ei ollut viiva
                if (prev != '-' && j > 0) {
                    safe_name[j++] = '-';
                    prev = '-';
                }
            }
        }
        safe_name[j] = '\0';
        snprintf(feed_key, sizeof(feed_key), "%s-%s", safe_name, mac_str + 8);  // nimi + 4 viimeistä MAC:ia
    } else {
        snprintf(feed_key, sizeof(feed_key), "%s", mac_str);
    }
    
    // Lähetä lämpötila
    if (dev->field_mask & FIELD_TEMP) {
        char feed_name[80];
        snprintf(feed_name, sizeof(feed_name), "%s-temp", feed_key);
        snprintf(url, sizeof(url), "https://io.adafruit.com/api/v2/%s/feeds/%s/data", aio_username, feed_name);
        
        // Lisää metadata: laitteen nimi ja MAC
        if (strlen(dev->name) > 0) {
            snprintf(payload, sizeof(payload), "{\"value\":\"%.2f\",\"feed_key\":\"%s\",\"metadata\":\"%s (%02X:%02X:%02X:%02X:%02X:%02X)\"}",
                     dev->temperature, feed_name, dev->name,
                     dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
        } else {
            snprintf(payload, sizeof(payload), "{\"value\":\"%.2f\",\"feed_key\":\"%s\",\"metadata\":\"MAC: %02X:%02X:%02X:%02X:%02X:%02X\"}",
                     dev->temperature, feed_name,
                     dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
        }
        
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "X-AIO-Key", aio_key);
        esp_http_client_set_post_field(client, payload, strlen(payload));
        
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (err == ESP_OK && status == 200) {
            ESP_LOGI(AIO_TAG, "Temp lähetetty: %s = %.2f", feed_key, dev->temperature);
        } else {
            ESP_LOGE(AIO_TAG, "Temp epäonnistui: %s, HTTP %d", esp_err_to_name(err), status);
        }
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(100)); // Pieni viive requestien välillä
    }
    
    // Lähetä kosteus
    if (dev->field_mask & FIELD_HUM) {
        snprintf(url, sizeof(url), "https://io.adafruit.com/api/v2/%s/feeds/%s-hum/data", aio_username, feed_key);
        
        if (strlen(dev->name) > 0) {
            snprintf(payload, sizeof(payload), "{\"value\":\"%d\",\"metadata\":\"%s (%02X:%02X:%02X:%02X:%02X:%02X)\"}",
                     dev->humidity, dev->name,
                     dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
        } else {
            snprintf(payload, sizeof(payload), "{\"value\":\"%d\",\"metadata\":\"MAC: %02X:%02X:%02X:%02X:%02X:%02X\"}",
                     dev->humidity,
                     dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
        }
        
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "X-AIO-Key", aio_key);
        esp_http_client_set_post_field(client, payload, strlen(payload));
        
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (err == ESP_OK && status == 200) {
            ESP_LOGI(AIO_TAG, "Hum lähetetty: %s = %d", feed_key, dev->humidity);
        } else {
            ESP_LOGE(AIO_TAG, "Hum epäonnistui: %s, HTTP %d", esp_err_to_name(err), status);
        }
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Lähetä akun taso
    if (dev->field_mask & FIELD_BAT) {
        snprintf(url, sizeof(url), "https://io.adafruit.com/api/v2/%s/feeds/%s-bat/data", aio_username, feed_key);
        
        if (strlen(dev->name) > 0) {
            snprintf(payload, sizeof(payload), "{\"value\":\"%d\",\"metadata\":\"%s (%02X:%02X:%02X:%02X:%02X:%02X)\"}",
                     dev->battery_pct, dev->name,
                     dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
        } else {
            snprintf(payload, sizeof(payload), "{\"value\":\"%d\",\"metadata\":\"MAC: %02X:%02X:%02X:%02X:%02X:%02X\"}",
                     dev->battery_pct,
                     dev->addr[0], dev->addr[1], dev->addr[2], dev->addr[3], dev->addr[4], dev->addr[5]);
        }
        
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "X-AIO-Key", aio_key);
        esp_http_client_set_post_field(client, payload, strlen(payload));
        
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(AIO_TAG, "Bat lähetetty: %s = %d", feed_key, dev->battery_pct);
        }
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void aio_upload_task(void *arg) {
    ESP_LOGI(AIO_TAG, "Aloitetaan datan lähetys Adafruit IO:lle...");
    
    int sent_count = 0;
    for (int i = 0; i < device_count; i++) {
        if (devices[i].visible && devices[i].has_sensor_data) {
            send_device_to_aio(&devices[i]);
            sent_count++;
        }
    }
    
    ESP_LOGI(AIO_TAG, "Lähetettiin %d laitetta", sent_count);
    vTaskDelete(NULL);
}

static void aio_timer_callback(void* arg) {
    if (!aio_enabled) return;
    
    // Luo taski joka lähettää datan (ei blokoi timeria)
    xTaskCreate(aio_upload_task, "aio_upload", 8192, NULL, 5, NULL);
}

// API: Lähetä data nyt (testaus)
static esp_err_t api_aio_send_now_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    if (!aio_enabled || strlen(aio_username) == 0 || strlen(aio_key) == 0) {
        const char* resp = "{\"ok\":false,\"error\":\"Adafruit IO ei käytössä\"}";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Luo taski joka lähettää datan
    xTaskCreate(aio_upload_task, "aio_upload", 8192, NULL, 5, NULL);
    
    const char* resp = "{\"ok\":true,\"message\":\"Lähetys aloitettu\"}";
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// ============================================
// WEB-KÄYTTÖLIITTYMÄ
// ============================================

static esp_err_t root_get_handler(httpd_req_t *req) {
    if (setup_mode) {
        httpd_resp_send(req, SETUP_HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

// API: Setup WiFi-asetukset
static esp_err_t api_setup_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parseroidaan JSON (yksinkertainen toteutus)
    char *ssid_start = strstr(buf, "\"ssid\":\"");
    char *pass_start = strstr(buf, "\"password\":\"");
    
    if (!ssid_start || !pass_start) {
        const char* resp = "{\"ok\":false,\"error\":\"Invalid JSON\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    ssid_start += 8;  // Skip "ssid":"
    pass_start += 12; // Skip "password":"
    
    char ssid[64] = {0};
    char password[64] = {0};
    
    char *ssid_end = strchr(ssid_start, '"');
    char *pass_end = strchr(pass_start, '"');
    
    if (ssid_end && pass_end) {
        int ssid_len = ssid_end - ssid_start;
        int pass_len = pass_end - pass_start;
        
        if (ssid_len > 0 && ssid_len < 64) {
            strncpy(ssid, ssid_start, ssid_len);
        }
        if (pass_len >= 0 && pass_len < 64) {
            strncpy(password, pass_start, pass_len);
        }
        
        // Tallenna ja käynnistä uudelleen
        save_wifi_config(ssid, password);
        
        const char* resp = "{\"ok\":true}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        
        ESP_LOGI(WIFI_TAG, "WiFi määritelty, käynnistetään uudelleen...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        
        return ESP_OK;
    }
    
    const char* resp = "{\"ok\":false,\"error\":\"Parse error\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// API: Adafruit IO asetukset
static esp_err_t api_aio_config_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parseroidaan JSON
    char *user_start = strstr(buf, "\"username\":\"");
    char *key_start = strstr(buf, "\"key\":\"");
    char *enabled_start = strstr(buf, "\"enabled\":");
    
    if (!user_start || !key_start) {
        const char* resp = "{\"ok\":false,\"error\":\"Invalid JSON\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    user_start += 12;  // Skip "username":"
    key_start += 7;     // Skip "key":"
    
    char username[64] = {0};
    char key[128] = {0};
    bool enabled = true;
    
    char *user_end = strchr(user_start, '"');
    char *key_end = strchr(key_start, '"');
    
    if (user_end && key_end) {
        int user_len = user_end - user_start;
        int key_len = key_end - key_start;
        
        if (user_len > 0 && user_len < 64) {
            strncpy(username, user_start, user_len);
        }
        if (key_len > 0 && key_len < 128) {
            strncpy(key, key_start, key_len);
        }
        
        if (enabled_start) {
            enabled_start += 10;  // Skip "enabled":
            enabled = (strncmp(enabled_start, "true", 4) == 0);
        }
        
        save_aio_config(username, key, enabled);
        
        const char* resp = "{\"ok\":true}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        
        return ESP_OK;
    }
    
    const char* resp = "{\"ok\":false,\"error\":\"Parse error\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// API: Hae Adafruit IO asetukset
static esp_err_t api_aio_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    char response[256];
    snprintf(response, sizeof(response), 
             "{\"ok\":true,\"username\":\"%s\",\"enabled\":%s,\"has_key\":%s}",
             aio_username,
             aio_enabled ? "true" : "false",
             strlen(aio_key) > 0 ? "true" : "false");
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

// API: Käynnistä discovery mode (uusien laitteiden etsintä) 30 sekunniksi
// HUOM: Skannaus pyörii jo jatkuvasti, tämä vain sallii uusien laitteiden lisäämisen
static esp_err_t api_start_scan_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    if (allow_new_devices) {
        ESP_LOGW(TAG, "Discovery mode on jo käynnissä");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Discovery mode jo käynnissä\"}");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Käynnistetään DISCOVERY MODE %d sekunnin ajaksi (skannaus pyörii jo taustalla)", DISCOVERY_DURATION_MS / 1000);
    
    // Salli uusien laitteiden lisääminen
    allow_new_devices = true;
    
    // Luo ajastin jos ei vielä ole
    if (discovery_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = stop_discovery_timer_callback,
            .name = "discovery_timer"
        };
        esp_timer_create(&timer_args, &discovery_timer);
    }
    
    // Käynnistä ajastin
    esp_timer_start_once(discovery_timer, DISCOVERY_DURATION_MS * 1000);  // Microsekunteja
    
    char response[128];
    snprintf(response, sizeof(response), "{\"ok\":true,\"duration\":%d}", DISCOVERY_DURATION_MS / 1000);
    httpd_resp_sendstr(req, response);
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
        // Oletuksena vain visible=true, show_all=1 näyttää kaikki (discovery-popup)
        if (!show_all && !devices[i].visible) {
            continue;
        }
        
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
            devices[i].addr[0], devices[i].addr[1], devices[i].addr[2],
            devices[i].addr[3], devices[i].addr[4], devices[i].addr[5]);
        
        char item[512];
        if (devices[i].has_sensor_data) {
            // Määritä mitä kenttiä laite tukee
            uint16_t available = 0;
            if (devices[i].temperature != 0) available |= FIELD_TEMP;
            if (devices[i].humidity != 0) available |= FIELD_HUM;
            if (devices[i].battery_pct != 0) available |= FIELD_BAT;
            if (devices[i].battery_mv != 0) available |= FIELD_BATMV;
            available |= FIELD_RSSI; // RSSI aina saatavilla
            
            snprintf(item, sizeof(item),
                "%s{\"addr\":\"%s\",\"name\":\"%s\",\"rssi\":%d,"
                "\"hasSensor\":true,\"temp\":%.1f,\"hum\":%d,\"bat\":%d,\"batMv\":%d,"
                "\"saved\":%s,\"showMac\":%s,\"fieldMask\":%d,\"availableFields\":%d}",
                first ? "" : ",",
                addr_str,
                devices[i].name[0] ? devices[i].name : "Unknown",
                devices[i].rssi,
                devices[i].temperature,
                devices[i].humidity,
                devices[i].battery_pct,
                devices[i].battery_mv,
                devices[i].visible ? "true" : "false",
                devices[i].show_mac ? "true" : "false",
                devices[i].field_mask,
                available);
        } else {
            snprintf(item, sizeof(item),
                "%s{\"addr\":\"%s\",\"name\":\"%s\",\"rssi\":%d,"
                "\"hasSensor\":false,\"saved\":%s,\"showMac\":%s,\"fieldMask\":%d,\"availableFields\":%d}",
                first ? "" : ",",
                addr_str,
                devices[i].name[0] ? devices[i].name : "Unknown",
                devices[i].rssi,
                devices[i].visible ? "true" : "false",
                devices[i].show_mac ? "true" : "false",
                devices[i].field_mask,
                FIELD_RSSI); // Vain RSSI saatavilla
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

// Tunnista laitteen "signatuuri" (mitkä kentät sillä on)
static uint16_t get_device_signature(const ble_device_t *dev) {
    uint16_t sig = 0;
    if (dev->has_sensor_data) {
        sig |= (1 << 0); // On sensor-laite
        if (dev->temperature != 0) sig |= (1 << 1);
        if (dev->humidity != 0) sig |= (1 << 2);
        if (dev->battery_pct != 0) sig |= (1 << 3);
        if (dev->battery_mv != 0) sig |= (1 << 4);
    }
    return sig;
}

static esp_err_t api_update_settings_handler(httpd_req_t *req) {
    char buf[512];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG, "Update settings request: %s", buf);
    
    // Parsitaan parametrit: addr, name, show_mac, field_mask, apply_to_similar
    char addr_str[64] = {0};
    char name[MAX_NAME_LEN] = {0};
    int show_mac = 1;
    int field_mask = FIELD_ALL;
    int apply_to_similar = 0;
    
    char *addr_param = strstr(buf, "addr=");
    char *name_param = strstr(buf, "&name=");
    char *show_mac_param = strstr(buf, "&show_mac=");
    char *field_mask_param = strstr(buf, "&field_mask=");
    char *apply_param = strstr(buf, "&apply_to_similar=");
    
    if (addr_param) {
        sscanf(addr_param, "addr=%60[^&]", addr_str);
        
        // URL-dekoodaus
        char decoded[64];
        int j = 0;
        for (int i = 0; addr_str[i] && j < 63; i++) {
            if (addr_str[i] == '%' && addr_str[i+1] && addr_str[i+2]) {
                char hex[3] = {addr_str[i+1], addr_str[i+2], 0};
                // Tue sekä isoja että pieniä kirjaimia (%3A ja %3a)
                char c;
                if (hex[0] >= 'a') hex[0] -= 32;
                if (hex[1] >= 'a') hex[1] -= 32;
                sscanf(hex, "%hhX", &c);
                decoded[j++] = c;
                i += 2;
            } else if (addr_str[i] == '+') {
                decoded[j++] = ' ';
            } else {
                decoded[j++] = addr_str[i];
            }
        }
        decoded[j] = '\0';
        strcpy(addr_str, decoded);
    }
    
    if (name_param) {
        sscanf(name_param, "&name=%60[^&]", name);
        // URL-dekoodaus nimelle
        char decoded[MAX_NAME_LEN];
        int j = 0;
        for (int i = 0; name[i] && j < MAX_NAME_LEN-1; i++) {
            if (name[i] == '%' && name[i+1] && name[i+2]) {
                char hex[3] = {name[i+1], name[i+2], 0};
                char c;
                if (hex[0] >= 'a') hex[0] -= 32;
                if (hex[1] >= 'a') hex[1] -= 32;
                sscanf(hex, "%hhX", &c);
                decoded[j++] = c;
                i += 2;
            } else if (name[i] == '+') {
                decoded[j++] = ' ';
            } else {
                decoded[j++] = name[i];
            }
        }
        decoded[j] = '\0';
        strcpy(name, decoded);
    }
    
    if (show_mac_param) {
        sscanf(show_mac_param, "&show_mac=%d", &show_mac);
    }
    
    if (field_mask_param) {
        sscanf(field_mask_param, "&field_mask=%d", &field_mask);
    }
    
    if (apply_param) {
        sscanf(apply_param, "&apply_to_similar=%d", &apply_to_similar);
    }
    
    ESP_LOGI(TAG, "Päivitetään laite: %s, name='%s', show_mac=%d, fields=0x%04X, apply=%d",
             addr_str, name, show_mac, field_mask, apply_to_similar);
    
    // Etsi laite
    int target_idx = -1;
    for (int i = 0; i < device_count; i++) {
        char dev_addr[18];
        snprintf(dev_addr, sizeof(dev_addr), "%02X:%02X:%02X:%02X:%02X:%02X",
            devices[i].addr[0], devices[i].addr[1], devices[i].addr[2],
            devices[i].addr[3], devices[i].addr[4], devices[i].addr[5]);
        
        if (strcmp(dev_addr, addr_str) == 0) {
            target_idx = i;
            break;
        }
    }
    
    if (target_idx == -1) {
        ESP_LOGW(TAG, "Laitetta ei löytynyt: %s", addr_str);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Device not found");
        return ESP_FAIL;
    }
    
    // Päivitä laitteen asetukset
    strncpy(devices[target_idx].name, name, MAX_NAME_LEN - 1);
    devices[target_idx].show_mac = (show_mac != 0);
    devices[target_idx].field_mask = field_mask;
    
    // Tallenna asetukset
    save_device_settings(devices[target_idx].addr, name, devices[target_idx].show_mac, field_mask);
    
    // Jos apply_to_similar on asetettu, etsi samanlaiset laitteet
    int updated_count = 1;
    if (apply_to_similar) {
        uint16_t target_sig = get_device_signature(&devices[target_idx]);
        ESP_LOGI(TAG, "Sovelletaan asetuksia samanlaisiin laitteisiin (signatuuri: 0x%04X)", target_sig);
        
        for (int i = 0; i < device_count; i++) {
            if (i == target_idx) continue;
            
            uint16_t sig = get_device_signature(&devices[i]);
            if (sig == target_sig) {
                // Sovella vain show_mac ja field_mask, ei nimeä!
                devices[i].show_mac = devices[target_idx].show_mac;
                devices[i].field_mask = devices[target_idx].field_mask;
                save_device_settings(devices[i].addr, devices[i].name, devices[i].show_mac, field_mask);
                updated_count++;
                ESP_LOGI(TAG, "  Päivitetty: %02X:%02X:...", devices[i].addr[0], devices[i].addr[1]);
            }
        }
    }
    
    ESP_LOGI(TAG, "Päivitetty %d laitetta", updated_count);
    
    char response[128];
    snprintf(response, sizeof(response), "{\"ok\":true,\"updated\":%d}", updated_count);
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
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
        
        httpd_uri_t api_update_settings = {
            .uri = "/api/update-settings",
            .method = HTTP_POST,
            .handler = api_update_settings_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_update_settings);
        
        httpd_uri_t api_start_scan = {
            .uri = "/api/start-scan",
            .method = HTTP_POST,
            .handler = api_start_scan_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_start_scan);
        
        httpd_uri_t api_setup = {
            .uri = "/api/setup",
            .method = HTTP_POST,
            .handler = api_setup_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_setup);
        
        httpd_uri_t api_aio_config = {
            .uri = "/api/aio/config",
            .method = HTTP_POST,
            .handler = api_aio_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_aio_config);
        
        httpd_uri_t api_aio_get = {
            .uri = "/api/aio/config",
            .method = HTTP_GET,
            .handler = api_aio_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_aio_get);
        
        httpd_uri_t api_aio_send = {
            .uri = "/api/aio/send_now",
            .method = HTTP_POST,
            .handler = api_aio_send_now_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_aio_send);
        
        ESP_LOGI(TAG, "HTTP-palvelin käynnistetty");
    }
}

void app_main() {
    ESP_LOGI(TAG, "BLE Scanner + Web UI käynnistyy");
    
    // NVS alustus
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
    
    // Tarkista BOOT-nappi WiFi-nollausta varten
    check_boot_button();
    
    // Lataa tallennetut laitteet NVS:stä
    load_all_devices_from_nvs();
    ESP_LOGI(TAG, "Ladattu %d tallennettua laitetta NVS:stä", device_count);
    
    wifi_init();
    start_webserver();
    
    // Lataa Adafruit IO asetukset
    load_aio_config();
    
    // Käynnistä Adafruit IO ajastin jos asetukset on määritelty
    if (aio_enabled && strlen(aio_username) > 0 && strlen(aio_key) > 0) {
        esp_timer_create_args_t aio_timer_args = {
            .callback = aio_timer_callback,
            .name = "aio_timer"
        };
        esp_timer_create(&aio_timer_args, &aio_timer);
        esp_timer_start_periodic(aio_timer, AIO_SEND_INTERVAL_MS * 1000);  // Microsekunteja
        ESP_LOGI(AIO_TAG, "Ajastin käynnistetty, lähetys %d min välein", AIO_SEND_INTERVAL_MS / 60000);
    }
    
    // BLE vain normaalitilassa, ei setup-modessa
    if (!setup_mode) {
        // Käynnistä BLE-stack, mutta EI aloita skannausta automaattisesti
        nimble_port_init();
        ble_hs_cfg.sync_cb = ble_app_on_sync;
        nimble_port_freertos_init(host_task);
        
        ESP_LOGI(TAG, "Järjestelmä valmis. Skannaus odottaa käyttäjän komentoa.");
    } else {
        ESP_LOGI(TAG, "Setup-tila aktiivinen. BLE ei käytössä.");
    }
}
