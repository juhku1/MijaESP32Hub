-- This schema is automatically created by the worker
-- Each device gets its own table (one table per MAC address)
-- Example table name: device_c047c1a43e42

-- Template for device table:
-- CREATE TABLE IF NOT EXISTS device_{mac} (
--     id INTEGER PRIMARY KEY AUTOINCREMENT,
--     timestamp INTEGER NOT NULL,           -- Unix timestamp in seconds
--     device_name TEXT,                     -- Friendly name from ESP32
--     temperature REAL,                     -- Temperature in Celsius
--     humidity INTEGER,                     -- Relative humidity %
--     battery_mv INTEGER,                   -- Battery voltage in millivolts
--     rssi INTEGER                          -- WiFi signal strength in dBm
-- );

-- Example queries:

-- Get last 100 readings for a device:
-- SELECT * FROM device_c047c1a43e42 
-- ORDER BY timestamp DESC 
-- LIMIT 100;

-- Get average temperature per day for last 30 days:
-- SELECT 
--     DATE(timestamp, 'unixepoch') as date,
--     AVG(temperature) as avg_temp,
--     MIN(temperature) as min_temp,
--     MAX(temperature) as max_temp,
--     COUNT(*) as readings
-- FROM device_c047c1a43e42
-- WHERE timestamp > unixepoch('now', '-30 days')
-- GROUP BY DATE(timestamp, 'unixepoch')
-- ORDER BY date DESC;

-- Get battery level history:
-- SELECT 
--     datetime(timestamp, 'unixepoch') as time,
--     battery_mv,
--     temperature,
--     humidity
-- FROM device_c047c1a43e42
-- ORDER BY timestamp DESC
-- LIMIT 1000;

-- Delete old data (older than 1 year):
-- DELETE FROM device_c047c1a43e42
-- WHERE timestamp < unixepoch('now', '-1 year');

-- List all device tables:
-- SELECT name FROM sqlite_master 
-- WHERE type='table' AND name LIKE 'device_%';

-- Get row count per table:
-- SELECT 
--     (SELECT COUNT(*) FROM device_c047c1a43e42) as device_c047c1a43e42_count;
