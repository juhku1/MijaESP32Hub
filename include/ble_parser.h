#ifndef BLE_PARSER_H
#define BLE_PARSER_H

#include <stdint.h>
#include <stdbool.h>

// Device data structure
typedef struct {
    bool has_data;
    float temperature;
    uint8_t humidity;
    uint8_t battery_pct;
    uint16_t battery_mv;
    char device_type[32];
} ble_sensor_data_t;

/**
 * Parse BLE advertisement data and extract sensor information
 * 
 * @param adv_data Pointer to advertisement data
 * @param adv_len Length of advertisement data
 * @param company_id Manufacturer company ID
 * @param sensor_data Output structure for parsed sensor data
 * @return true if sensor data was successfully parsed, false otherwise
 */
bool ble_parse_sensor_data(const uint8_t *adv_data, uint8_t adv_len, 
                           uint16_t company_id, ble_sensor_data_t *sensor_data);

/**
 * Parse ATC (Xiaomi Thermometer Custom Format) data
 * 
 * @param svc_data Service data payload
 * @param svc_len Service data length
 * @param sensor_data Output structure for parsed sensor data
 * @return true if successfully parsed
 */
bool ble_parse_atc_format(const uint8_t *svc_data, uint8_t svc_len, 
                          ble_sensor_data_t *sensor_data);

/**
 * Parse pvvx (Custom Xiaomi Thermometer Format) data
 * 
 * @param svc_data Service data payload
 * @param svc_len Service data length
 * @param sensor_data Output structure for parsed sensor data
 * @return true if successfully parsed
 */
bool ble_parse_pvvx_format(const uint8_t *svc_data, uint8_t svc_len, 
                           ble_sensor_data_t *sensor_data);

/**
 * Get device type string based on company ID
 * 
 * @param company_id BLE company ID
 * @return String describing the device type
 */
const char* ble_get_device_type(uint16_t company_id);

#endif // BLE_PARSER_H
