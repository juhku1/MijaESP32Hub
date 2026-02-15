# BLE Master Hub (ESP32-S3)

ESP32-S3 BLE master device that scans for BLE advertisements, processes device data, and exposes it via HTTP API.

## Hardware
- **Board**: ESP32-S3-DevKitC-1
- **Chip**: ESP32-S3 (Dual-core, WiFi + BLE)
- **Flash**: 4MB
- **USB**: Native USB-Serial/JTAG

## Features
- **BLE Scanner**: Continuous BLE advertising packet scanning
- **HTTP API**: RESTful server exposing device data
- **Service Discovery**: Broadcasts presence via UDP (port 19798) for satellite auto-discovery
- **Serial Console**: Commands for configuration and debugging

## Network Configuration
- **WiFi**: Connects to configured SSID
- **HTTP Server**: Port 80
- **Discovery Protocol**: UDP broadcast on port 19798
  - Message format: `SATMASTER <IP> <port>`
  - Interval: Every 5 seconds

## API Endpoints
- `GET /api/devices` - List all discovered BLE devices with latest data
- `POST /api/satellite-data` - Receive data from satellite devices (JSON array)

## Building & Flashing

### Prerequisites
```bash
pip install platformio
```

### Build
```bash
platformio run --environment esp32-s3-devkitc-1
```

### Upload
```bash
platformio run --target upload --environment esp32-s3-devkitc-1 --upload-port /dev/ttyACM0
```

### Monitor
```bash
platformio device monitor -p /dev/ttyACM0 -b 115200
```

## Configuration
WiFi credentials are configured at compile time in the source code. Look for:
```c
#define WIFI_SSID "YourSSID"
#define WIFI_PASSWORD "YourPassword"
```

## Related Projects
- [SuperMiniProjekti-Satellite](../SuperMiniProjekti-Satellite) - ESP32-C3 satellite device that forwards BLE data to this master

## License
[Your License Here]
