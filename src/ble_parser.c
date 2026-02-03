#include "ble_parser.h"
#include <string.h>

bool ble_parse_pvvx_format(const uint8_t *svc_data, uint8_t svc_len, 
                           ble_sensor_data_t *sensor_data) {
    if (svc_len < 17) {
        return false;
    }
    
    // pvvx format: offset 8-9: temperature (int16_t), 10-11: humidity (uint16_t)
    // 12-13: battery_mv (uint16_t), 14: battery_pct (uint8_t)
    int16_t temp_raw = svc_data[8] | (svc_data[9] << 8);
    uint16_t humi_raw = svc_data[10] | (svc_data[11] << 8);
    
    sensor_data->temperature = temp_raw / 100.0f;
    sensor_data->humidity = humi_raw / 100;
    sensor_data->battery_mv = svc_data[12] | (svc_data[13] << 8);
    sensor_data->battery_pct = svc_data[14];
    strcpy(sensor_data->device_type, "pvvx");
    sensor_data->has_data = true;
    
    return true;
}

bool ble_parse_atc_format(const uint8_t *svc_data, uint8_t svc_len, 
                          ble_sensor_data_t *sensor_data) {
    if (svc_len < 15) {
        return false;
    }
    
    // ATC format: offset 8-9: temperature (int16_t BE), 10: humidity (uint8_t)
    // 11: battery_pct (uint8_t), 12-13: battery_mv (uint16_t BE)
    int16_t temp_raw = (svc_data[8] << 8) | svc_data[9];
    
    sensor_data->temperature = temp_raw / 10.0f;
    sensor_data->humidity = svc_data[10];
    sensor_data->battery_pct = svc_data[11];
    sensor_data->battery_mv = (svc_data[12] << 8) | svc_data[13];
    strcpy(sensor_data->device_type, "ATC");
    sensor_data->has_data = true;
    
    return true;
}

bool ble_parse_sensor_data(const uint8_t *adv_data, uint8_t adv_len, 
                           uint16_t company_id, ble_sensor_data_t *sensor_data) {
    // Initialize sensor_data
    memset(sensor_data, 0, sizeof(ble_sensor_data_t));
    sensor_data->has_data = false;
    strcpy(sensor_data->device_type, "Unknown");
    
    // For now, we don't parse from raw adv_data here
    // This function serves as a placeholder for future expansion
    // The actual parsing happens in main.c through NimBLE's field parsing
    
    return false;
}

const char* ble_get_device_type(uint16_t company_id) {
    switch (company_id) {
        case 0x038F:
            return "Xiaomi";
        case 0x004C:
            return "Apple";
        case 0x0006:
            return "Microsoft";
        case 0x0075:
            return "Samsung";
        case 0x00E0:
            return "Google";
        default:
            return "Unknown";
    }
}
