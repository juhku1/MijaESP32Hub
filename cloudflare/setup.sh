#!/bin/bash

# MijiaESP32Hub Cloudflare D1 Setup Script
# This script automates the deployment of a Cloudflare Worker with D1 database
# for long-term sensor data storage.

set -e  # Exit on any error

echo "=========================================="
echo "MijiaESP32Hub - Cloudflare D1 Setup"
echo "=========================================="
echo ""

# Check if wrangler is installed
if ! command -v wrangler &> /dev/null; then
    echo "❌ Wrangler CLI is not installed."
    echo ""
    echo "Please install it with:"
    echo "  npm install -g wrangler"
    echo ""
    echo "Or visit: https://developers.cloudflare.com/workers/wrangler/install-and-update/"
    exit 1
fi

echo "✅ Wrangler CLI found: $(wrangler --version)"
echo ""

# Check if user is logged in
echo "Checking Cloudflare authentication..."
if ! wrangler whoami &> /dev/null; then
    echo "⚠️  Not logged in to Cloudflare."
    echo ""
    echo "Opening browser for authentication..."
    wrangler login
    echo ""
else
    echo "✅ Already authenticated with Cloudflare"
    echo ""
fi

# Navigate to cloudflare directory
cd "$(dirname "$0")"

# Create D1 database
echo "=========================================="
echo "Step 1: Creating D1 Database"
echo "=========================================="
echo ""

DATABASE_NAME="mijaesp32hub"

# Check if database already exists
EXISTING_DB=$(wrangler d1 list | grep "$DATABASE_NAME" || true)

if [ -n "$EXISTING_DB" ]; then
    echo "⚠️  Database '$DATABASE_NAME' already exists."
    echo ""
    read -p "Do you want to use the existing database? (y/n): " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "❌ Setup cancelled. Please use a different database name."
        exit 1
    fi
    
    # Extract database ID from existing database
    DB_ID=$(wrangler d1 info "$DATABASE_NAME" | grep "database_id" | awk '{print $2}' | tr -d '"')
else
    echo "Creating new D1 database..."
    CREATE_OUTPUT=$(wrangler d1 create "$DATABASE_NAME")
    echo "$CREATE_OUTPUT"
    
    # Extract database ID from output
    DB_ID=$(echo "$CREATE_OUTPUT" | grep "database_id" | awk '{print $3}' | tr -d '"')
    
    echo ""
    echo "✅ Database created successfully!"
fi

echo "Database ID: $DB_ID"
echo ""

# Update wrangler.toml with database ID
echo "Updating wrangler.toml..."
sed -i.bak "s/database_id = \".*\"/database_id = \"$DB_ID\"/" wrangler.toml
rm wrangler.toml.bak
echo "✅ wrangler.toml updated"
echo ""

# Generate API token
echo "=========================================="
echo "Step 2: Setting API Token"
echo "=========================================="
echo ""

# Generate a random API token
API_TOKEN=$(openssl rand -hex 32)

echo "Generated API token: $API_TOKEN"
echo ""
echo "⚠️  IMPORTANT: Save this token! You'll need it for ESP32 configuration."
echo ""

# Set the secret in Cloudflare
echo "$API_TOKEN" | wrangler secret put API_TOKEN

echo ""
echo "✅ API token configured"
echo ""

# Deploy worker
echo "=========================================="
echo "Step 3: Deploying Worker"
echo "=========================================="
echo ""

wrangler deploy

echo ""
echo "✅ Worker deployed successfully!"
echo ""

# Get worker URL
WORKER_URL=$(wrangler deployments list | grep "https://" | head -n 1 | awk '{print $1}')

echo "=========================================="
echo "✅ Setup Complete!"
echo "=========================================="
echo ""
echo "Your Cloudflare D1 archive is ready!"
echo ""
echo "📋 Configuration for ESP32:"
echo "   Worker URL: $WORKER_URL"
echo "   API Token:  $API_TOKEN"
echo ""
echo "📝 Next steps:"
echo "   1. Open your ESP32 web interface"
echo "   2. Go to Settings → Cloudflare D1"
echo "   3. Enter the Worker URL and API Token above"
echo "   4. Enable Cloudflare D1"
echo "   5. Click 'Test Connection' to verify"
echo ""
echo "💾 Your sensor data will now be archived to Cloudflare D1"
echo "   (in addition to Adafruit IO if enabled)"
echo ""
echo "🔍 To view your data:"
echo "   wrangler d1 execute $DATABASE_NAME --command=\"SELECT name FROM sqlite_master WHERE type='table'\""
echo ""
echo "📚 For more information, see: docs/cloudflare.md"
echo ""
