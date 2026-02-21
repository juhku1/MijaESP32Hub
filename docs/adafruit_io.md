# Adafruit IO Integration

## Overview
The hub can automatically upload sensor data to Adafruit IO for cloud visualization, historical data storage, and remote access.

## Features
- **Automatic feed creation** for temperature, humidity, and battery data
- **Safe operation**: Won't overwrite or delete existing feeds
- **Configurable data types**: Choose which sensors to upload (temp/hum/battery)
- **30-second upload interval** (free tier limit)
- **Feed naming**: Uses device MAC address (e.g., `A4C138ABCDEF-temp`)

## Setup

### 1. Create Adafruit IO Account
1. Go to https://io.adafruit.com
2. Sign up for a free account
3. Navigate to "My Key" (🔑 icon)
4. Copy your **Username** and **AIO Key**

### 2. Configure in Hub
1. Open hub web UI
2. Go to **Settings** → **Adafruit IO**
3. Enter your **Username** and **AIO Key**
4. Select data types to upload (Temperature, Humidity, Battery)
5. Click **Save Settings**

### 3. Create Feeds Automatically
1. Make sure devices are visible in the main view (not hidden)
2. In Adafruit IO settings, click **Create Feeds**
3. Hub will create feeds for all visible devices
4. **Safe operation**: Existing feeds are skipped, no data is lost

**Feed Creation Behavior:**
- ✓ **Creates** new feeds for devices that don't have them yet
- ○ **Skips** devices that already have feeds (safe to run multiple times)
- ✗ **Reports** any errors (e.g., network issues, invalid credentials)

**Example output:**
```
✅ Feed creation completed!
✓ Created: 2 new feeds
○ Skipped: 3 (already exist)
✗ Failed: 0
```

### 4. Test Data Upload
1. Click **Send Data Now** to test immediately
2. Check your Adafruit IO feeds to verify data appears
3. Enable automatic uploads with the toggle switch

## Feed Naming Convention
Feeds are named using device MAC addresses to ensure consistency:

**Format**: `AABBCCDDEEFF-temp`, `AABBCCDDEEFF-hum`, `AABBCCDDEEFF-bat`

**Example**: Device with MAC `A4:C1:38:AB:CD:EF` creates:
- `A4C138ABCDEF-temp` (Temperature feed)
- `A4C138ABCDEF-hum` (Humidity feed)
- `A4C138ABCDEF-bat` (Battery feed)

**Why MAC addresses?**
- Device names can change (user can rename devices)
- MAC address never changes
- Ensures feeds always match the correct device
- Easy to identify: just remove colons from MAC address

## Feed Management

### Deleting Feeds
If you need to remove feeds (e.g., removed a sensor):
1. Go to Settings → Adafruit IO
2. Select data types to delete (Temperature, Humidity, Battery)
3. Click **Delete Feeds**
4. Confirm the action

**Warning**: This permanently deletes feeds and their historical data!

### Renaming Feeds
You can rename feeds in Adafruit IO dashboard:
1. Go to https://io.adafruit.com/feeds
2. Click on a feed
3. Edit the feed name (display name only, key stays the same)

**Note**: Hub will continue using the original key (`A4C138ABCDEF-temp`), so renaming is safe.

## Free Tier Limits
- **30-second upload interval** (minimum)
- **30 data points per minute**
- **30 days data retention**
- **10 feeds per group** (no limit on total feeds)

The hub respects these limits automatically.

## Troubleshooting

### "Failed to create feeds"
- Check username and AIO key are correct
- Verify internet connection
- Check Adafruit IO is not down (https://status.adafruit.com)

### "Upload failed"
- Check internet connection
- Verify AIO key hasn't expired
- Ensure device has sensor data (check main view)

### Feeds not appearing in Adafruit IO
1. Check Settings → Adafruit IO → "Enabled" is ON
2. Verify feeds were created (Create Feeds button)
3. Check logs in serial monitor for errors
4. Try "Send Data Now" to test immediately

### Data not updating
- Uploads happen every 30 seconds
- Check "Last Upload" time in Settings
- Verify device is still scanning (check RSSI updates)
- Check Adafruit IO dashboard for recent data

## Tips
- **Run "Create Feeds" whenever you add new devices** - it's safe and won't affect existing feeds
- **Check "Send Data Now" to verify uploads work** before enabling automatic uploads
- **Use descriptive device names** in the hub UI - they appear in feed metadata
- **Monitor feed usage** in Adafruit IO to stay within free tier limits

## Integration with Other Services
Adafruit IO can trigger actions based on sensor data:
- IFTTT integration
- Webhooks
- Email alerts
- Custom dashboards

See Adafruit IO documentation for details: https://io.adafruit.com/api/docs/
