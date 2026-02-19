# Cloudflare D1 Integration

This guide explains how to set up Cloudflare D1 as a long-term data archive for your MijiaESP32Hub sensor data.

## Overview

Cloudflare D1 is a serverless SQLite database that provides:
- **10 GB of free storage** (enough for millions of sensor readings)
- **Unlimited data retention** (no automatic deletion like Adafruit IO's 30 days)
- **Fast SQL queries** for historical data analysis
- **Global edge distribution** for low-latency access

The D1 integration works **alongside** Adafruit IO:
- **Adafruit IO**: Real-time visualization and dashboards (30-day retention on free tier)
- **Cloudflare D1**: Long-term data archive for analysis and backup (unlimited retention)

## Architecture

```
ESP32 Device → Cloudflare Worker → D1 Database
                                  ↓
                        One table per device
                     (device_c047c1a43e42)
```

Each device gets its own table in the D1 database, named after its MAC address. This allows:
- Efficient storage and queries per device
- Easy data export for specific devices
- Independent data retention policies per device

## Prerequisites

1. **Cloudflare Account** (free tier is sufficient)
   - Sign up at: https://dash.cloudflare.com/sign-up

2. **Node.js and npm** (for Wrangler CLI)
   - Download from: https://nodejs.org/

3. **Wrangler CLI** (Cloudflare's deployment tool)
   ```bash
   npm install -g wrangler
   ```

## Quick Setup (Automated)

The easiest way to set up D1 is using the automated setup script:

```bash
cd cloudflare
./setup.sh
```

The script will:
1. ✅ Check if Wrangler is installed
2. 🔐 Authenticate with Cloudflare (opens browser)
3. 💾 Create a D1 database named `mijaesp32hub`
4. 🔑 Generate a secure API token
5. 🚀 Deploy the Worker to Cloudflare
6. 📋 Display the Worker URL and API token

**Important:** Save the API token shown at the end! You'll need it to configure the ESP32.

## Manual Setup (Advanced)

If you prefer manual control:

### 1. Create D1 Database

```bash
cd cloudflare
wrangler d1 create mijaesp32hub
```

Copy the `database_id` from the output and update `wrangler.toml`:

```toml
[[d1_databases]]
binding = "DB"
database_name = "mijaesp32hub"
database_id = "YOUR_DATABASE_ID_HERE"  # Replace with actual ID
```

### 2. Generate API Token

```bash
openssl rand -hex 32
```

Save this token securely. Then set it as a Cloudflare secret:

```bash
echo "YOUR_TOKEN_HERE" | wrangler secret put API_TOKEN
```

### 3. Deploy Worker

```bash
wrangler deploy
```

Note the Worker URL from the output (e.g., `https://mijaesp32hub-d1-worker.your-subdomain.workers.dev`).

## ESP32 Configuration

1. Open the ESP32 web interface (usually `http://192.168.4.1` or your device's IP)
2. Click **Settings** → **Cloudflare D1** tab
3. Enter:
   - **Worker URL**: The URL from setup (e.g., `https://mijaesp32hub-d1-worker.xxx.workers.dev`)
   - **API Token**: The 64-character token from setup
   - **Enable checkbox**: ✅ Check to activate
4. Click **Save Settings**
5. Click **Test Connection** to verify (should show "Connection successful!")

## Data Structure

Each device's table has the following schema:

| Column        | Type    | Description                          |
|---------------|---------|--------------------------------------|
| `id`          | INTEGER | Auto-incrementing primary key        |
| `timestamp`   | INTEGER | Unix timestamp (seconds since epoch) |
| `device_name` | TEXT    | Friendly name from ESP32             |
| `temperature` | REAL    | Temperature in Celsius               |
| `humidity`    | INTEGER | Relative humidity (%)                |
| `battery_mv`  | INTEGER | Battery voltage in millivolts        |
| `rssi`        | INTEGER | WiFi signal strength (dBm)           |

Example table name: `device_c047c1a43e42` (for MAC address `C0:47:C1:A4:3E:42`)

## Querying Your Data

Use Wrangler CLI to run SQL queries:

### List all device tables:
```bash
wrangler d1 execute mijaesp32hub --command="SELECT name FROM sqlite_master WHERE type='table'"
```

### Get last 100 readings from a device:
```bash
wrangler d1 execute mijaesp32hub --command="SELECT * FROM device_c047c1a43e42 ORDER BY timestamp DESC LIMIT 100"
```

### Get average temperature per day (last 30 days):
```bash
wrangler d1 execute mijaesp32hub --command="
SELECT 
    DATE(timestamp, 'unixepoch') as date,
    AVG(temperature) as avg_temp,
    MIN(temperature) as min_temp,
    MAX(temperature) as max_temp,
    COUNT(*) as readings
FROM device_c047c1a43e42
WHERE timestamp > unixepoch('now', '-30 days')
GROUP BY DATE(timestamp, 'unixepoch')
ORDER BY date DESC
"
```

### Export data to CSV:
```bash
wrangler d1 export mijaesp32hub --output=backup.sql
```

## Free Tier Limits

Cloudflare D1 free tier includes:
- **10 GB storage** (millions of sensor readings)
- **5 million reads/day**
- **100,000 writes/day**

For typical usage (1 device uploading every 5 minutes):
- Daily writes: 288 readings × 1 device = 288 writes
- **You can support ~300 devices** on the free tier!

If you exceed these limits, consider:
- Increasing upload interval (e.g., every 10 or 15 minutes)
- Deleting old data (see Data Retention below)
- Upgrading to Cloudflare's paid plan ($5/month for 10x limits)

## Data Retention

D1 has **no automatic deletion** - data stays forever (or until you delete it).

To manage storage, you can delete old data manually:

### Delete readings older than 1 year:
```bash
wrangler d1 execute mijaesp32hub --command="
DELETE FROM device_c047c1a43e42
WHERE timestamp < unixepoch('now', '-1 year')
"
```

### Check database size:
```bash
wrangler d1 info mijaesp32hub
```

You can also set up a scheduled Worker to auto-delete old data (advanced).

## Troubleshooting

### "Unauthorized" error when testing connection
- Verify the API token is correct (64 hex characters)
- Check that the token was set in Cloudflare: `wrangler secret list`

### "Worker not found" or 404 errors
- Verify Worker URL is correct (should end with `.workers.dev`)
- Check deployment status: `wrangler deployments list`
- Redeploy if needed: `wrangler deploy`

### Data not appearing in D1
- Check Worker logs: `wrangler tail`
- Verify ESP32 shows "D1: OK" in serial output
- Test Worker directly with curl:
  ```bash
  curl -X POST https://your-worker.workers.dev/data \
    -H "Authorization: YOUR_TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"mac":"AA:BB:CC:DD:EE:FF","name":"Test","data":{"temperature":22.5,"humidity":45,"battery_mv":3000,"rssi":-65}}'
  ```

### "Database not found" error
- Verify `database_id` in `wrangler.toml` matches the D1 database
- List databases: `wrangler d1 list`

## Security Notes

- **API Token**: Keep your API token secret! Anyone with this token can write to your database
- **CORS**: The Worker allows all origins (`Access-Control-Allow-Origin: *`). For production, consider restricting to your ESP32's IP
- **Rate Limiting**: Consider adding rate limiting to prevent abuse
- **HTTPS**: All communication is encrypted (Workers only accept HTTPS)

## Advanced Topics

### Custom Worker Logic

You can modify [worker.js](../cloudflare/worker.js) to:
- Add data validation (temperature range checks, etc.)
- Implement rate limiting per device
- Send alerts for abnormal readings
- Aggregate data before storage (e.g., hourly averages)

### Data Backup

Export entire database periodically:
```bash
wrangler d1 export mijaesp32hub --output=backup-$(date +%Y%m%d).sql
```

Restore from backup:
```bash
wrangler d1 execute mijaesp32hub --file=backup-20240101.sql
```

### Multiple Workers

You can deploy multiple Workers for different purposes:
- One for ESP32 data ingestion (POST /data)
- One for web-based data visualization (GET /api/*)
- One for scheduled maintenance (DELETE old data)

## Cost Estimation

| Devices | Readings/Day | Monthly Writes | Storage/Year | Free Tier OK? |
|---------|--------------|----------------|--------------|---------------|
| 1       | 288          | 8,640          | ~50 MB       | ✅ Yes        |
| 10      | 2,880        | 86,400         | ~500 MB      | ✅ Yes        |
| 50      | 14,400       | 432,000        | ~2.5 GB      | ⚠️ Upgrade    |
| 100     | 28,800       | 864,000        | ~5 GB        | ❌ Paid plan  |

*Assumes: 200 bytes per reading, 5-minute upload interval*

## Support

- Cloudflare D1 docs: https://developers.cloudflare.com/d1/
- Wrangler docs: https://developers.cloudflare.com/workers/wrangler/
- SQLite docs: https://www.sqlite.org/docs.html
- MijiaESP32Hub issues: https://github.com/your-repo/issues

## Alternative Backends

If Cloudflare doesn't meet your needs, you can implement similar Workers for:
- **Supabase** (PostgreSQL database)
- **PlanetScale** (MySQL database)
- **MongoDB Atlas** (NoSQL database)
- **AWS Lambda + DynamoDB**
- **Self-hosted** (Node.js server + SQLite)

The ESP32 code is backend-agnostic - just change the Worker URL!