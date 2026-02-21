# Serial monitor quick guide (PlatformIO)

## Start
- **Serial monitor (115200):**
  - `pio device monitor -b 115200`
- **Serial monitor for a specific port:**
  - `pio device monitor -p /dev/ttyUSB0 -b 115200`

## Upload (firmware)
- **Build + upload (esp32-c3-devkitm-1):**
  - `pio run -e esp32-c3-devkitm-1 -t upload`
  - `pio device monitor -p /dev/ttyACM0 -b 115200`

## Close the port
- **Stop the monitor:**
  - `Ctrl + C`

## List ports
- **List devices/ports:**
  - `pio device list`

## Crash Diagnostics

### Check device diagnostics in Web UI
The main page now shows a **Diagnostics** section with:
- **Boot Count**: How many times the device has restarted
- **Last Reset**: Reason for last boot (POWERON, PANIC, TASK_WDT, etc.)
- **Uptime**: Time since last boot
- **Memory**: Free heap and largest allocatable block
- **BLE Rate**: Bluetooth packets per 10 seconds

### Reset reasons explained:
- **POWERON**: Normal power-on or first boot ✅
- **SW**: Software reset (normal reboot) ✅
- **PANIC**: Firmware crash (check serial logs!) ⚠️
- **TASK_WDT**: Task watchdog timeout (task stuck) ⚠️
- **INT_WDT**: Interrupt watchdog timeout (interrupt stuck) ⚠️
- **BROWNOUT**: Voltage too low (power supply issue) ⚠️

### If device crashes repeatedly:
1. **Check serial monitor** during crash:
   ```bash
   pio device monitor -b 115200
   ```
   Look for stack traces, error messages, or "Guru Meditation Error"

2. **Check free memory** in diagnostics:
   - If **minFreeHeap** < 30,000 bytes → memory leak likely
   - If **largestBlock** is very small → heap fragmentation

3. **Check boot count vs uptime**:
   - High boot count but low uptime → device crashing frequently
   - Example: 50 boots in 1 hour = crash every 72 seconds

4. **Common crash causes**:
   - Memory leak (heap running out)
   - Stack overflow (recursive functions, large local variables)
   - NULL pointer dereference
   - HTTP client timeout during Cloudflare API calls
   - Too many devices in scan list (>50)

## Console Commands

Once connected to the serial monitor, you can type these commands:

### Factory Reset
```
factory_reset
```
**⚠️ WARNING:** This erases ALL settings and reboots the device:
- WiFi credentials (SSID, password)
- Adafruit IO configuration (username, AIO key, feed settings)
- Cloudflare D1 configuration (Worker URL, token)
- Device list (discovered BLE sensors)
- Custom device names

After reset, the device boots into **Setup Mode** (creates WiFi hotspot `MijiaESP32Hub-SETUP`).

### Help
```
help
```
Shows available commands in the serial console.

## Tips
- If the port is busy, close other programs using the same serial port.
- Make sure the baud rate is correct (usually 115200).
- If the device does not show up, unplug and reconnect the USB cable.
- Commands are case-sensitive (use lowercase).
- Press Enter after typing a command.

## Use Cases for Factory Reset

### 1. Moving to a new WiFi network
If you can't access the web UI to change WiFi settings (e.g., you moved the device to a new location):
1. Connect via USB and open serial monitor
2. Type `factory_reset` and press Enter
3. Device reboots into Setup Mode
4. Connect to `MijiaESP32Hub-SETUP` WiFi and configure new network

### 2. Selling/giving away the device
Remove all personal data:
- Cloud credentials (Adafruit IO, Cloudflare D1)
- WiFi password
- Device names you assigned

### 3. Troubleshooting corrupted settings
If the device behaves unexpectedly or won't connect:
- Factory reset clears potentially corrupted NVS data
- Fresh start with clean configuration

### 4. Testing/development
Quickly return to initial state for testing setup process.
