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

bool ble_parse_mibeacon_format(const uint8_t *svc_data, uint8_t svc_len, 
                               ble_sensor_data_t *sensor_data) {
    // MiBeacon minimum packet size check
    if (svc_len < 11) {
        return false;
    }
    
    // Check for encryption flag (bit 3 of flags byte at offset 0)
    uint8_t flags = svc_data[0];
    bool has_encryption = (flags & 0x08) != 0;
    bool has_data = (flags & 0x40) != 0;
    
    // Skip encrypted packets (would need bindkey)
    if (has_encryption) {
        return false;
    }
    
    // Must have data flag
    if (!has_data) {
        return false;
    }
    
    // Device UUID at offset 2-3 (Little Endian)
    uint16_t device_uuid = svc_data[2] | (svc_data[3] << 8);
    
    // Check if this is LYWSD03MMC (device UUID 0x055b)
    if (device_uuid != 0x055b) {
        return false;  // Not LYWSD03MMC, could support other devices later
    }
    
    // Frame counter at offset 4 (not used for parsing but good for debugging)
    // uint8_t frame_count = svc_data[4];
    
    // Payload starts after header
    // Offset depends on capability flag
    bool has_capability = (flags & 0x20) != 0;
    uint8_t payload_offset = has_capability ? 12 : 11;
    
    if (svc_len < payload_offset + 3) {
        return false;  // Not enough data for payload
    }
    
    // Parse value objects in payload
    // Format: [type_lo][type_hi][length][data...]
    uint8_t pos = payload_offset;
    bool found_temp = false;
    bool found_hum = false;
    
    while (pos + 3 <= svc_len) {
        uint16_t value_type = svc_data[pos] | (svc_data[pos + 1] << 8);
        uint8_t value_len = svc_data[pos + 2];
        
        // Check if we have enough data
        if (pos + 3 + value_len > svc_len) {
            break;
        }
        
        const uint8_t *data = &svc_data[pos + 3];
        
        switch (value_type) {
            case 0x1004:  // Temperature (16-bit signed, LE, 0.1°C)
                if (value_len == 2) {
                    int16_t temp_raw = data[0] | (data[1] << 8);
                    sensor_data->temperature = temp_raw / 10.0f;
                    found_temp = true;
                }
                break;
                
            case 0x1006:  // Humidity (16-bit signed, LE, 0.1%)
                if (value_len == 2) {
                    int16_t hum_raw = data[0] | (data[1] << 8);
                    sensor_data->humidity = hum_raw / 10;
                    found_hum = true;
                }
                break;
                
            case 0x100A:  // Battery (8-bit unsigned, 1%)
                if (value_len == 1) {
                    sensor_data->battery_pct = data[0];
                    sensor_data->battery_mv = 0;  // MiBeacon doesn't provide mV
                }
                break;
                
            case 0x100D:  // Temperature + Humidity combined (4 bytes)
                if (value_len == 4) {
                    int16_t temp_raw = data[0] | (data[1] << 8);
                    int16_t hum_raw = data[2] | (data[3] << 8);
                    sensor_data->temperature = temp_raw / 10.0f;
                    sensor_data->humidity = hum_raw / 10;
                    found_temp = true;
                    found_hum = true;
                }
                break;
        }
        
        // Move to next value object
        pos += 3 + value_len;
    }
    
    // Only consider successful if we got at least temperature or humidity
    if (found_temp || found_hum) {
        strcpy(sensor_data->device_type, "MiBeacon");
        sensor_data->has_data = true;
        return true;
    }
    
    return false;
}

bool ble_parse_bthome_v2_format(const uint8_t *svc_data, uint8_t svc_len, 
                                ble_sensor_data_t *sensor_data) {
    // BTHome v2 minimum packet: UUID(2) + DevInfo(1) + at least one measurement(3)
    if (svc_len < 6) {
        return false;
    }
    
    // Verify UUID is 0xFCD2 (already checked by caller, but double-check)
    uint16_t uuid = svc_data[0] | (svc_data[1] << 8);
    if (uuid != 0xFCD2) {
        return false;
    }
    
    // Device info byte at offset 2
    uint8_t dev_info = svc_data[2];
    
    // Check encryption flag (bit 0)
    bool is_encrypted = (dev_info & 0x01) != 0;
    if (is_encrypted) {
        return false;  // Skip encrypted packets (would need encryption key)
    }
    
    // BTHome version should be 2 (bits 5-7 = 010)
    uint8_t version = (dev_info >> 5) & 0x07;
    if (version != 2) {
        return false;  // Only support v2
    }
    
    // Parse object data starting at offset 3
    uint8_t pos = 3;
    bool found_temp = false;
    bool found_hum = false;
    
    while (pos < svc_len) {
        uint8_t object_id = svc_data[pos];
        pos++;
        
        // Not enough data for this object
        if (pos >= svc_len) {
            break;
        }
        
        switch (object_id) {
            case 0x00:  // Packet ID (uint8, 1 byte) - skip
                pos += 1;
                break;
                
            case 0x01:  // Battery % (uint8, 1 byte)
                if (pos + 1 <= svc_len) {
                    sensor_data->battery_pct = svc_data[pos];
                    sensor_data->battery_mv = 0;  // BTHome doesn't provide mV
                    pos += 1;
                }
                break;
                
            case 0x02:  // Temperature (sint16, 2 bytes, factor 0.01)
                if (pos + 2 <= svc_len) {
                    int16_t temp_raw = svc_data[pos] | (svc_data[pos + 1] << 8);
                    sensor_data->temperature = temp_raw / 100.0f;
                    found_temp = true;
                    pos += 2;
                }
                break;
                
            case 0x03:  // Humidity % (uint16, 2 bytes, factor 0.01)
                if (pos + 2 <= svc_len) {
                    uint16_t hum_raw = svc_data[pos] | (svc_data[pos + 1] << 8);
                    sensor_data->humidity = hum_raw / 100;
                    found_hum = true;
                    pos += 2;
                }
                break;
                
            case 0x2E:  // Humidity % (uint8, 1 byte, factor 1)
                if (pos + 1 <= svc_len) {
                    sensor_data->humidity = svc_data[pos];
                    found_hum = true;
                    pos += 1;
                }
                break;
                
            case 0x45:  // Temperature (sint16, 2 bytes, factor 0.1)
                if (pos + 2 <= svc_len) {
                    int16_t temp_raw = svc_data[pos] | (svc_data[pos + 1] << 8);
                    sensor_data->temperature = temp_raw / 10.0f;
                    found_temp = true;
                    pos += 2;
                }
                break;
                
            // Unknown object ID - try to skip based on common sizes
            // This is a simple heuristic; ideally we'd have a full table
            default:
                // Most common sensors are 1-4 bytes, stop parsing on unknown
                // to avoid misalignment
                pos = svc_len;  // Stop parsing
                break;
        }
    }
    
    // Success if we got at least temperature or humidity
    if (found_temp || found_hum) {
        strcpy(sensor_data->device_type, "BTHome");
        sensor_data->has_data = true;
        return true;
    }
    
    return false;
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
