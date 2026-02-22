# Cloud worker (unused)

This folder is currently not documented or required for the supported setup.
Use Adafruit IO for cloud storage and dashboards.

## Files

- **worker.js** - Cloudflare Worker code (handles /ping and /data endpoints)
- **wrangler.toml** - Worker configuration
- **schema.sql** - Database schema reference (created automatically)
- **setup.sh** - Automated deployment script
- **README.md** - This file

## Architecture

```
ESP32 → POST /data → Worker → D1 Database
                              ├─ device_aabbccddeeff (table)
                              ├─ device_112233445566 (table)
                              └─ device_... (one per device)
```

## Endpoints

### POST /data
Receives sensor data from ESP32 and stores it in D1.

**Request:**
```json
{
  "mac": "C0:47:C1:A4:3E:42",
  "name": "Living Room",
  "data": {
    "temperature": 22.5,
    "humidity": 45,
    "battery_mv": 3000,
    "rssi": -65
  }
}
```

**Response:**
```json
{
  "ok": true,
  "message": "Data stored successfully",
  "table": "device_c047c1a43e42"
}
```

### GET /ping
Health check endpoint.

**Response:**
```json
{
  "ok": true,
  "message": "Cloudflare D1 Worker is running",
  "timestamp": "2024-01-01T12:00:00.000Z"
}
```

## Authentication

All requests must include the `Authorization` header with your API token:

```
Authorization: YOUR_64_CHAR_TOKEN_HERE
```

## Querying Data

Use Wrangler CLI to query your D1 database:

```bash
# List all device tables
wrangler d1 execute mijaesp32hub --command="SELECT name FROM sqlite_master WHERE type='table'"

# Get last 100 readings from a device
wrangler d1 execute mijaesp32hub --command="SELECT * FROM device_c047c1a43e42 ORDER BY timestamp DESC LIMIT 100"

# Export entire database
wrangler d1 export mijaesp32hub --output=backup.sql
```

## Local Development

Test locally with Wrangler:

```bash
wrangler dev
```

Then test with curl:

```bash
curl http://localhost:8787/ping -H "Authorization: test-token"

curl -X POST http://localhost:8787/data \
  -H "Authorization: test-token" \
  -H "Content-Type: application/json" \
  -d '{"mac":"AA:BB:CC:DD:EE:FF","name":"Test","data":{"temperature":22.5,"humidity":45,"battery_mv":3000,"rssi":-65}}'
```

## Deployment

Deploy to Cloudflare:

```bash
wrangler deploy
```

View logs:

```bash
wrangler tail
```

## Documentation

See [docs/cloudflare.md](../docs/cloudflare.md) for complete setup and usage guide.

## Support

- Cloudflare D1: https://developers.cloudflare.com/d1/
- Wrangler: https://developers.cloudflare.com/workers/wrangler/
- MijiaESP32Hub: https://github.com/your-repo
