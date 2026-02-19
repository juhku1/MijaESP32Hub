# Serial monitor quick guide (PlatformIO)

## Start
- **Serial monitor (115200):**
  - `pio device monitor -b 115200`
- **Serial monitor for a specific port:**
  - `pio device monitor -p /dev/ttyUSB0 -b 115200`

## Upload (firmware)
- **Build + upload (esp32-s3-devkitc-1):**
  - `pio run -e esp32-s3-devkitc-1 -t upload`
  - `pio device monitor -p /dev/ttyACM0 -b 115200`

## Close the port
- **Stop the monitor:**
  - `Ctrl + C`

## List ports
- **List devices/ports:**
  - `pio device list`

## Tips
- If the port is busy, close other programs using the same serial port.
- Make sure the baud rate is correct (usually 115200).
- If the device does not show up, unplug and reconnect the USB cable.
