#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG = "BLE_SCAN";
static const char *WIFI_TAG = "WiFi";

#define MAX_DEVICES 50
#define WIFI_SSID "Kontu"
#define WIFI_PASS "8765432A1"
#define MAX_NAME_LEN 32
#define MAX_MFG_DATA_LEN 64

typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    uint32_t last_seen;
    bool tracked;
    char name[MAX_NAME_LEN];
    uint8_t mfg_data[MAX_MFG_DATA_LEN];
    uint8_t mfg_data_len;
    uint16_t company_id;
    int8_t tx_power;
    bool has_tx_power;
    // ATC_MiThermometer specific
    bool has_atc_data;
    float temperature;
    uint8_t humidity;
    uint8_t battery_pct;
    uint16_t battery_mv;
    // Service UUID info
    uint16_t service_uuid;
    bool has_service_uuid;
    // Raw advertising data
    uint8_t raw_data[62];
    uint8_t raw_data_len;
    char device_type[32];  // "Xiaomi", "ATC", "pvvx", "Unknown"
} ble_device_t;

static ble_device_t devices[MAX_DEVICES];
static int device_count = 0;
static httpd_handle_t server = NULL;

static int find_or_add_device(uint8_t *addr) {
    for (int i = 0; i < device_count; i++) {
        if (memcmp(devices[i].addr, addr, 6) == 0) {
            return i;
        }
    }
    if (device_count < MAX_DEVICES) {
        memcpy(devices[device_count].addr, addr, 6);
        devices[device_count].tracked = false;
        memset(devices[device_count].name, 0, MAX_NAME_LEN);
        memset(devices[device_count].mfg_data, 0, MAX_MFG_DATA_LEN);
        memset(devices[device_count].device_type, 0, 32);
        strcpy(devices[device_count].device_type, "Unknown");
        devices[device_count].mfg_data_len = 0;
        devices[device_count].company_id = 0;
        devices[device_count].has_tx_power = false;
        devices[device_count].has_atc_data = false;
        devices[device_count].has_service_uuid = false;
        devices[device_count].raw_data_len = 0;
        return device_count++;
    }
    return -1;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            int idx = find_or_add_device(event->disc.addr.val);
            if (idx >= 0) {
                devices[idx].rssi = event->disc.rssi;
                devices[idx].last_seen = xTaskGetTickCount();
                
                // Tallenna raaka mainosdata
                int raw_len = event->disc.length_data;
                if (raw_len > 62) raw_len = 62;
                memcpy(devices[idx].raw_data, event->disc.data, raw_len);
                devices[idx].raw_data_len = raw_len;
                
                // Parsitaan mainosdata
                struct ble_hs_adv_fields fields;
                if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
                    // Laitteen nimi
                    if (fields.name != NULL && fields.name_len > 0) {
                        int copy_len = (fields.name_len < MAX_NAME_LEN - 1) ? fields.name_len : MAX_NAME_LEN - 1;
                        memcpy(devices[idx].name, fields.name, copy_len);
                        devices[idx].name[copy_len] = '\0';
                    }
                    
                    // Manufacturer data
                    if (fields.mfg_data != NULL && fields.mfg_data_len > 2) {
                        devices[idx].company_id = fields.mfg_data[0] | (fields.mfg_data[1] << 8);
                        int data_len = fields.mfg_data_len - 2;
                        if (data_len > MAX_MFG_DATA_LEN) data_len = MAX_MFG_DATA_LEN;
                        memcpy(devices[idx].mfg_data, fields.mfg_data + 2, data_len);
                        devices[idx].mfg_data_len = data_len;
                    }
                    
                    // TX Power
                    if (fields.tx_pwr_lvl_is_present) {
                        devices[idx].tx_power = fields.tx_pwr_lvl;
                        devices[idx].has_tx_power = true;
                    }
                    
                    // ATC_MiThermometer format (UUID 0x181A)
                    // Supports both atc1441 (13+ bytes) and pvvx custom (17+ bytes) formats
                    if (fields.svc_data_uuid16 != NULL && fields.svc_data_uuid16_len >= 13) {
                        uint16_t uuid = fields.svc_data_uuid16[0] | (fields.svc_data_uuid16[1] << 8);
                        devices[idx].service_uuid = uuid;
                        devices[idx].has_service_uuid = true;
                        
                        if (uuid == 0x181A) {
                            // Check if this is pvvx custom format (19 bytes total, 17 data bytes after UUID)
                            if (fields.svc_data_uuid16_len >= 17) {
                                // pvvx Custom format (all little-endian, 0.01 units precision)
                                strcpy(devices[idx].device_type, "pvvx Custom");
                                
                                int16_t temp_raw = fields.svc_data_uuid16[8] | (fields.svc_data_uuid16[9] << 8);
                                uint16_t humi_raw = fields.svc_data_uuid16[10] | (fields.svc_data_uuid16[11] << 8);
                                devices[idx].temperature = temp_raw / 100.0f;  // 0.01°C precision
                                devices[idx].humidity = humi_raw / 100;  // Convert to whole %
                                devices[idx].battery_mv = fields.svc_data_uuid16[12] | (fields.svc_data_uuid16[13] << 8);
                                devices[idx].battery_pct = fields.svc_data_uuid16[14];
                                devices[idx].has_atc_data = true;
                            } 
                            else if (fields.svc_data_uuid16_len >= 15) {
                                // atc1441 format (big-endian, 0.1 units precision)
                                strcpy(devices[idx].device_type, "atc1441");
                                
                                int16_t temp_raw = (fields.svc_data_uuid16[8] << 8) | fields.svc_data_uuid16[9];
                                devices[idx].temperature = temp_raw / 10.0f;  // 0.1°C precision
                                devices[idx].humidity = fields.svc_data_uuid16[10];
                                devices[idx].battery_pct = fields.svc_data_uuid16[11];
                                devices[idx].battery_mv = (fields.svc_data_uuid16[12] << 8) | fields.svc_data_uuid16[13];
                                devices[idx].has_atc_data = true;
                            }
                        }
                        else if (uuid == 0xFCD2) {
                            // Xiaomi MiBeacon UUID
                            strcpy(devices[idx].device_type, "Xiaomi MiBeacon");
                            
                            // Parsitaan Xiaomi MiBeacon data
                            // Format: [UUID 2B][Frame Control 2B][Product ID 2B][Frame Cnt 1B][MAC 6B][Capability 1B][Object...]
                            // Object: [Object ID 2B LE][Length 1B][Data ...]
                            if (fields.svc_data_uuid16_len >= 5) {
                                // Etsi object data (vaihtelee frame controlin mukaan)
                                int offset = 5; // Skip UUID(2) + Frame Control(2) + Product ID starts
                                
                                // Yksinkertainen parsinta: etsi temperature/humidity objektit
                                // Object ID 0x0D10 = Temp+Humi (4 bytes: temp int16 + humi uint16)
                                // Object ID 0x0410 = Battery (1 byte)
                                while (offset + 3 < fields.svc_data_uuid16_len) {
                                    uint16_t obj_id = fields.svc_data_uuid16[offset] | (fields.svc_data_uuid16[offset+1] << 8);
                                    uint8_t obj_len = fields.svc_data_uuid16[offset+2];
                                    
                                    if (offset + 3 + obj_len > fields.svc_data_uuid16_len) break;
                                    
                                    if (obj_id == 0x100D && obj_len == 4) {
                                        // Temperature + Humidity
                                        int16_t temp = fields.svc_data_uuid16[offset+3] | (fields.svc_data_uuid16[offset+4] << 8);
                                        uint16_t humi = fields.svc_data_uuid16[offset+5] | (fields.svc_data_uuid16[offset+6] << 8);
                                        devices[idx].temperature = temp / 10.0f;
                                        devices[idx].humidity = humi / 10;
                                        devices[idx].has_atc_data = true;
                                    }
                                    else if (obj_id == 0x1006 && obj_len == 2) {
                                        // Temperature only
                                        int16_t temp = fields.svc_data_uuid16[offset+3] | (fields.svc_data_uuid16[offset+4] << 8);
                                        devices[idx].temperature = temp / 10.0f;
                                        devices[idx].has_atc_data = true;
                                    }
                                    else if (obj_id == 0x1004 && obj_len == 2) {
                                        // Humidity only
                                        uint16_t humi = fields.svc_data_uuid16[offset+3] | (fields.svc_data_uuid16[offset+4] << 8);
                                        devices[idx].humidity = humi / 10;
                                    }
                                    else if (obj_id == 0x100A && obj_len == 1) {
                                        // Battery %
                                        devices[idx].battery_pct = fields.svc_data_uuid16[offset+3];
                                    }
                                    
                                    offset += 3 + obj_len;
                                }
                            }
                        }
                        else if (uuid == 0xFE95) {
                            // Xiaomi Service Data UUID
                            strcpy(devices[idx].device_type, "Xiaomi Service");
                        }
                    }
                    
                    // Tunnista Xiaomi laitteet company ID:n perusteella
                    if (devices[idx].company_id == 0x038F) {  // Xiaomi Inc
                        if (strcmp(devices[idx].device_type, "Unknown") == 0) {
                            strcpy(devices[idx].device_type, "Xiaomi Mfg");
                        }
                    }
                    
                    // Tunnista muita yleisiä UUID:ita
                    if (devices[idx].has_service_uuid) {
                        if (devices[idx].service_uuid == 0xFE9F && strcmp(devices[idx].device_type, "Unknown") == 0) {
                            strcpy(devices[idx].device_type, "Google");
                        }
                        else if (devices[idx].service_uuid == 0xFEF3 && strcmp(devices[idx].device_type, "Unknown") == 0) {
                            strcpy(devices[idx].device_type, "Google Nearby");
                        }
                    }
                    
                    // Tunnista Apple laitteet
                    if (devices[idx].company_id == 0x004C) {  // Apple Inc
                        if (strcmp(devices[idx].device_type, "Unknown") == 0) {
                            strcpy(devices[idx].device_type, "Apple");
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
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

static esp_err_t root_get_handler(httpd_req_t *req) {
    const char *html = 
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>BLE Scanner - Asetukset</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:0;background:#f0f0f0}"
        ".nav{background:#007bff;color:white;padding:15px 20px;box-shadow:0 2px 5px rgba(0,0,0,0.1);display:flex;align-items:center}"
        ".nav-title{font-size:20px;font-weight:bold;margin-right:30px}"
        ".nav a{color:white;text-decoration:none;margin:0 10px;padding:8px 16px;border-radius:4px;background:rgba(255,255,255,0.1)}"
        ".nav a:hover{background:rgba(255,255,255,0.2)}"
        ".nav a.active{background:rgba(255,255,255,0.3);font-weight:bold}"
        ".container{max-width:1200px;margin:20px auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1,h2{color:#333;margin-top:0}"
        "h1{border-bottom:2px solid #007bff;padding-bottom:10px}"
        "h2{font-size:18px;margin:20px 0 10px 0;color:#007bff}"
        ".controls{background:#f8f9fa;padding:15px;margin-bottom:20px;border-radius:5px;border:1px solid #dee2e6}"
        ".filter-group{margin:10px 0;display:flex;flex-wrap:wrap;gap:15px}"
        ".filter-group label{cursor:pointer;user-select:none}"
        ".filter-group input[type=checkbox]{margin-right:5px;cursor:pointer}"
        ".device{background:#f8f9fa;margin:10px 0;padding:15px;border-radius:5px;border-left:4px solid #007bff}"
        ".device.tracked{border-left-color:#28a745;background:#d4edda}"
        ".addr{font-family:monospace;font-size:16px;font-weight:bold}"
        ".rssi{color:#666;margin-left:10px}"
        ".type{display:inline-block;background:#007bff;color:white;padding:2px 8px;border-radius:3px;font-size:11px;margin-left:10px}"
        ".data-line{margin:5px 0;font-size:13px}"
        ".data-label{font-weight:bold;color:#555;min-width:120px;display:inline-block}"
        ".debug{font-family:monospace;font-size:11px;color:#666;background:#f0f0f0;padding:8px;margin-top:5px;border-radius:3px;word-break:break-all;line-height:1.4}"
        "button{background:#007bff;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;margin:5px 5px 5px 0;font-size:14px}"
        "button:hover{background:#0056b3}"
        "button.tracked{background:#28a745}"
        "button.tracked:hover{background:#218838}"
        ".refresh{background:#17a2b8}"
        ".refresh:hover{background:#138496}"
        ".info-box{background:#e7f3ff;border-left:4px solid#2196F3;padding:15px;margin:15px 0;border-radius:5px}"
        ".info-box h3{margin-top:0;color:#2196F3;font-size:16px}"
        ".info-box a{color:#007bff;text-decoration:none;font-weight:bold}"
        ".info-box a:hover{text-decoration:underline}"
        ".info-box ul{margin:10px 0;padding-left:20px}"
        ".info-box li{margin:5px 0}"
        "</style>"
        "<script>"
        "let filters={showName:true,showType:true,showRSSI:true,showTemp:true,showHumi:true,showBattPct:true,showBattMv:true,showTxPower:true,showServiceUUID:true,showCompanyId:true,showMfgData:true,showRawData:true,deviceFilter:'all'};"
        "function loadFilters(){let s=localStorage.getItem('bleFilters');if(s)filters=JSON.parse(s);Object.keys(filters).forEach(k=>{let el=document.getElementById(k);if(el)el.checked=filters[k]||filters[k]===undefined;});}"
        "function saveFilters(){localStorage.setItem('bleFilters',JSON.stringify(filters));}"
        "function toggleFilter(key,value){filters[key]=value;saveFilters();refresh();}"
        "function refresh(){"
        "fetch('/api/devices').then(r=>r.json()).then(data=>{"
        "data.sort((a,b)=>{"
        "if((a.temperature!==undefined)&&(b.temperature===undefined))return -1;"
        "if((a.temperature===undefined)&&(b.temperature!==undefined))return 1;"
        "return b.rssi-a.rssi;"
        "});"
        "let html='';"
        "data.forEach(d=>{"
        "if(filters.deviceFilter==='sensors'&&d.temperature===undefined)return;"
        "if(filters.deviceFilter==='tracked'&&!d.tracked)return;"
        "if(filters.deviceFilter==='google'&&d.type.indexOf('Google')===-1)return;"
        "if(filters.deviceFilter==='apple'&&d.type.indexOf('Apple')===-1)return;"
        "if(filters.deviceFilter==='xiaomi'&&d.type.indexOf('Xiaomi')===-1&&d.type.indexOf('atc')===-1&&d.type.indexOf('pvvx')===-1)return;"
        "let tracked=d.tracked?'tracked':'';"
        "let btnText=d.tracked?'Seurataan':'Seuraa';"
        "let btnClass=d.tracked?'tracked':'';"
        "html+=`<div class='device ${tracked}'>`;"
        "html+=`<span class='addr'>${d.addr}</span>`;"
        "if(filters.showRSSI)html+=`<span class='rssi'>RSSI: ${d.rssi} dBm</span>`;"
        "if(filters.showType)html+=`<span class='type'>${d.type}</span>`;"
        "if(filters.showName&&d.name)html+=`<div class='data-line'><span class='data-label'>Nimi:</span> ${d.name}</div>`;"
        "if(filters.showTemp&&d.temperature!==undefined){"
        "html+=`<div class='data-line'>`;"
        "html+=`<span class='data-label'>Lampotila:</span> ${d.temperature.toFixed(2)} C`;"
        "if(filters.showHumi)html+=` | <span class='data-label'>Kosteus:</span> ${d.humidity} %`;"
        "if(filters.showBattPct||filters.showBattMv){"
        "html+=` | <span class='data-label'>Akku:</span> `;"
        "if(filters.showBattPct)html+=`${d.batteryPct}%`;"
        "if(filters.showBattPct&&filters.showBattMv)html+=` (`;"
        "if(filters.showBattMv)html+=`${d.batteryMv}mV`;"
        "if(filters.showBattPct&&filters.showBattMv)html+=`)`;"
        "}"
        "html+=`</div>`;"
        "}"
        "if(filters.showTxPower&&d.txPower!==undefined)html+=`<div class='data-line'><span class='data-label'>TX Power:</span> ${d.txPower} dBm</div>`;"
        "if(filters.showServiceUUID&&d.serviceUUID)html+=`<div class='data-line'><span class='data-label'>Service UUID:</span> ${d.serviceUUID}</div>`;"
        "if(filters.showCompanyId&&d.companyId!==undefined){"
        "html+=`<div class='data-line'><span class='data-label'>Company ID:</span> 0x${d.companyId.toString(16).padStart(4,'0')}`;"
        "if(filters.showMfgData&&d.mfgData)html+=` | <span class='data-label'>Mfg Data:</span> ${d.mfgData}`;"
        "html+=`</div>`;"
        "}"
        "if(filters.showRawData&&d.rawData)html+=`<div class='debug'><strong>RAW (${d.rawLen}B):</strong> ${d.rawData}</div>`;"
        "html+=`<button class='${btnClass}' onclick='track(\"${d.addr}\")'>${btnText}</button>`;"
        "html+='</div>';"
        "});"
        "document.getElementById('devices').innerHTML=html;"
        "});"
        "}"
        "function track(addr){"
        "fetch('/api/track?addr='+encodeURIComponent(addr),{method:'POST'})"
        ".then(()=>refresh());"
        "}"
        "window.onload=function(){loadFilters();setInterval(refresh,2000);refresh();};"
        "</script>"
        "</head><body>"
        "<div class='nav'>"
        "<div class='nav-title'>BLE Skanneri</div>"
        "<a href='/' class='active'>Asetukset</a>"
        "<a href='#' onclick='alert(\"Tulossa pian!\");return false'>Mittarit</a>"
        "</div>"
        "<div class='container'>"
        "<h1>Asetukset ja Laitteiden Hallinta</h1>"
        "<div class='controls'>"
        "<h2>Nayta Tiedot</h2>"
        "<div class='filter-group'>"
        "<label><input type='checkbox' id='showName' checked onchange='toggleFilter(\"showName\",this.checked)'>Nimi</label>"
        "<label><input type='checkbox' id='showType' checked onchange='toggleFilter(\"showType\",this.checked)'>Laitetyyppi</label>"
        "<label><input type='checkbox' id='showRSSI' checked onchange='toggleFilter(\"showRSSI\",this.checked)'>RSSI Signaali</label>"
        "<label><input type='checkbox' id='showTemp' checked onchange='toggleFilter(\"showTemp\",this.checked)'>Lampotila</label>"
        "<label><input type='checkbox' id='showHumi' checked onchange='toggleFilter(\"showHumi\",this.checked)'>Kosteus</label>"
        "<label><input type='checkbox' id='showBattPct' checked onchange='toggleFilter(\"showBattPct\",this.checked)'>Akku %</label>"
        "<label><input type='checkbox' id='showBattMv' checked onchange='toggleFilter(\"showBattMv\",this.checked)'>Akku mV</label>"
        "<label><input type='checkbox' id='showTxPower' checked onchange='toggleFilter(\"showTxPower\",this.checked)'>TX Teho</label>"
        "<label><input type='checkbox' id='showServiceUUID' checked onchange='toggleFilter(\"showServiceUUID\",this.checked)'>Service UUID</label>"
        "<label><input type='checkbox' id='showCompanyId' checked onchange='toggleFilter(\"showCompanyId\",this.checked)'>Company ID</label>"
        "<label><input type='checkbox' id='showMfgData' checked onchange='toggleFilter(\"showMfgData\",this.checked)'>Valmistajan Data</label>"
        "<label><input type='checkbox' id='showRawData' checked onchange='toggleFilter(\"showRawData\",this.checked)'>Raaka Data</label>"
        "</div>"
        "<h2>Suodata Laitteet</h2>"
        "<div class='filter-group'>"
        "<label><input type='radio' name='deviceFilter' checked onchange='toggleFilter(\"deviceFilter\",\"all\")'>Kaikki laitteet</label>"
        "<label><input type='radio' name='deviceFilter' onchange='toggleFilter(\"deviceFilter\",\"sensors\")'>Vain lampomittarit</label>"
        "<label><input type='radio' name='deviceFilter' onchange='toggleFilter(\"deviceFilter\",\"tracked\")'>Vain seuratut</label>"
        "<label><input type='radio' name='deviceFilter' onchange='toggleFilter(\"deviceFilter\",\"xiaomi\")'>Vain Xiaomi</label>"
        "<label><input type='radio' name='deviceFilter' onchange='toggleFilter(\"deviceFilter\",\"google\")'>Vain Google</label>"
        "<label><input type='radio' name='deviceFilter' onchange='toggleFilter(\"deviceFilter\",\"apple\")'>Vain Apple</label>"
        "</div>"
        "</div>"
        "<div class='info-box'>"
        "<h3>Firmware Paivitys ja Laitteiden Nimeaminen</h3>"
        "<p><strong>Anna laitteille nimet ja paivita firmware:</strong></p>"
        "<p><a href='https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html' target='_blank'>Avaa TelinkMiFlasher (Web Bluetooth)</a></p>"
        "<ul>"
        "<li>Klikkaa <strong>Connect</strong> ja valitse laite (esim. ATC_xxxxx tai LYWSD03MMC)</li>"
        "<li>Aseta <strong>Device Name</strong> (esim. 'Olohuone', 'Makuuhuone', 'Ulkona')</li>"
        "<li>Klikkaa <strong>Send Config</strong> - nimi nakyviin valittomasti talla sivulla!</li>"
        "<li><strong>Suositus:</strong> Paivita firmware <strong>pvvx Custom</strong> -versioon parempaan tarkkuuteen (0.01 C vs 0.1 C)</li>"
        "<li>Firmware paivityksen jalkeen laite kaynnistyy uudelleen ja nakyvissa pvvx-tyyppina</li>"
        "</ul>"
        "</div>"
        "<button class='refresh' onclick='refresh()'>Paivita Nyt</button>"
        "<div id='devices'>Ladataan...</div>"
        "</div>"
        "</body></html>";
    
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_devices_handler(httpd_req_t *req) {
    // CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    char *json = malloc(16384);  // Suurempi bufferi datalle
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int len = snprintf(json, 16384, "[");
    
    for (int i = 0; i < device_count && i < MAX_DEVICES; i++) {
        if (len < 15000) {  // Leave room for closing bracket
            len += snprintf(json + len, 16384 - len,
                "%s{\"addr\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"rssi\":%d,\"tracked\":%s",
                i > 0 ? "," : "",
                devices[i].addr[5], devices[i].addr[4], devices[i].addr[3],
                devices[i].addr[2], devices[i].addr[1], devices[i].addr[0],
                devices[i].rssi,
                devices[i].tracked ? "true" : "false");
            
            // Lisää nimi jos saatavilla
            if (devices[i].name[0] != '\0') {
                len += snprintf(json + len, 16384 - len, ",\"name\":\"%s\"", devices[i].name);
            }
            
            // Lisää TX Power jos saatavilla
            if (devices[i].has_tx_power) {
                len += snprintf(json + len, 16384 - len, ",\"txPower\":%d", devices[i].tx_power);
            }
            
            // Lisää ATC_MiThermometer data jos saatavilla
            if (devices[i].has_atc_data) {
                len += snprintf(json + len, 16384 - len, 
                    ",\"temperature\":%.1f,\"humidity\":%u,\"batteryPct\":%u,\"batteryMv\":%u",
                    devices[i].temperature, devices[i].humidity, 
                    devices[i].battery_pct, devices[i].battery_mv);
            }
            
            // Lisää laitteen tyyppi
            len += snprintf(json + len, 16384 - len, ",\"type\":\"%s\"", devices[i].device_type);
            
            // Lisää service UUID jos saatavilla
            if (devices[i].has_service_uuid) {
                len += snprintf(json + len, 16384 - len, ",\"serviceUUID\":\"0x%04X\"", devices[i].service_uuid);
            }
            
            // Lisää manufacturer data jos saatavilla
            if (devices[i].mfg_data_len > 0) {
                len += snprintf(json + len, 16384 - len, ",\"companyId\":%u,\"mfgData\":\"", 
                    devices[i].company_id);
                for (int j = 0; j < devices[i].mfg_data_len; j++) {
                    len += snprintf(json + len, 16384 - len, "%02X", devices[i].mfg_data[j]);
                }
                len += snprintf(json + len, 16384 - len, "\"");
            }
            
            // Lisää raaka mainosdata
            if (devices[i].raw_data_len > 0) {
                len += snprintf(json + len, 16384 - len, ",\"rawData\":\"");
                for (int j = 0; j < devices[i].raw_data_len && j < 62; j++) {
                    len += snprintf(json + len, 16384 - len, "%02X", devices[i].raw_data[j]);
                }
                len += snprintf(json + len, 16384 - len, "\",\"rawLen\":%u", devices[i].raw_data_len);
            }
            
            len += snprintf(json + len, 16384 - len, "}");
        }
    }
    
    len += snprintf(json + len, 16384 - len, "]");
    
    httpd_resp_send(req, json, len);
    free(json);
    return ESP_OK;
}

static esp_err_t api_track_handler(httpd_req_t *req) {
    // CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char buf[128];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    
    if (ret == ESP_OK) {
        char addr_str[20];
        if (httpd_query_key_value(buf, "addr", addr_str, sizeof(addr_str)) == ESP_OK) {
            uint8_t addr[6];
            sscanf(addr_str, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
                   &addr[5], &addr[4], &addr[3], &addr[2], &addr[1], &addr[0]);
            
            for (int i = 0; i < device_count; i++) {
                if (memcmp(devices[i].addr, addr, 6) == 0) {
                    devices[i].tracked = !devices[i].tracked;
                    ESP_LOGI(TAG, "Laite %s %s",
                        addr_str, devices[i].tracked ? "seurannassa" : "ei seurannassa");
                    break;
                }
            }
        }
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(WIFI_TAG, "Yhdistetään uudelleen...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "IP-osoite: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(WIFI_TAG, "WiFi alustettu");
}

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
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
        
        httpd_uri_t favicon = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = favicon_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &favicon);
        
        httpd_uri_t api_devices = {
            .uri = "/api/devices",
            .method = HTTP_GET,
            .handler = api_devices_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_devices);
        
        httpd_uri_t api_track = {
            .uri = "/api/track",
            .method = HTTP_POST,
            .handler = api_track_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_track);
        
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
    
    wifi_init();
    start_webserver();

    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);
}