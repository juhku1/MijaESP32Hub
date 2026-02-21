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
- WiFi credentials (SSID, password, AP password)
- Adafruit IO configuration (username, AIO key, feed settings)
- Cloudflare D1 configuration (Worker URL, token)
- Device list (discovered BLE sensors)
- Custom device names

After reset, the device boots with:
- **WiFi AP**: `BLE-Monitor` (always on)
- **AP password**: `temperature` (default)
- **Setup page**: http://192.168.4.1

### WiFi Configuration Status
```
wifi_status
```
Shows current WiFi configuration:
- Station SSID and connection status
- Station IP address (if connected)
- AP SSID and IP (always 192.168.4.1)
- AP password status (default or custom)

### Change AP Password via Serial
```
ap_password:<new_password>
```
Change the WiFi AP password (8-63 characters):
```
ap_password:MySecurePass123
```
**Note**: You can also change this via web UI at http://192.168.4.1

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
You have three options:

**Option A: Web UI (recommended)**
1. Connect to `BLE-Monitor` WiFi (password: `temperature` or your custom password)
2. Navigate to http://192.168.4.1
3. Enter new WiFi credentials
4. Device reconnects without restart

**Option B: Serial Console**
1. Connect via USB and open serial monitor
2. Type `factory_reset` and press Enter
3. Device reboots with default settings
4. Connect to `BLE-Monitor` WiFi (password: `temperature`)
5. Configure via http://192.168.4.1

**Option C: Physical access not needed**
- The AP is **always available** at `BLE-Monitor`
- Even when connected to home WiFi, you can access setup at 192.168.4.1

### 2. Forgot AP password
If you changed the AP password and forgot it:
1. Connect via USB serial monitor
2. Type `ap_password:temperature` to reset to default
3. Reconnect to `BLE-Monitor` with password `temperature`

### 3. Selling/giving away the device
Remove all personal data:
- Cloud credentials (Adafruit IO, Cloudflare D1)
- WiFi password- AP password (resets to `temperature`)- Device names you assigned

### 3. Troubleshooting corrupted settings
If the device behaves unexpectedly or won't connect:
- Factory reset clears potentially corrupted NVS data
- Fresh start with clean configuration

### 4. Testing/development
Quickly return to initial state for testing setup process.
