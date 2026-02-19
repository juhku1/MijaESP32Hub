/**
 * Cloudflare Worker for MijiaESP32Hub D1 Data Archive
 * 
 * This worker receives sensor data from ESP32 devices and stores it in
 * a Cloudflare D1 SQLite database for long-term archival.
 * 
 * Endpoints:
 * - GET /ping - Health check
 * - POST /data - Store sensor data
 */

export default {
	async fetch(request, env, ctx) {
		const url = new URL(request.url);
		
		// CORS headers
		const corsHeaders = {
			'Access-Control-Allow-Origin': '*',
			'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
			'Access-Control-Allow-Headers': 'Content-Type, Authorization',
		};
		
		// Handle OPTIONS preflight
		if (request.method === 'OPTIONS') {
			return new Response(null, { headers: corsHeaders });
		}
		
		// Verify authorization token
		const authHeader = request.headers.get('Authorization');
		if (!authHeader || authHeader !== env.API_TOKEN) {
			return new Response(JSON.stringify({ error: 'Unauthorized' }), {
				status: 401,
				headers: { ...corsHeaders, 'Content-Type': 'application/json' },
			});
		}
		
		try {
			// Health check endpoint
			if (url.pathname === '/ping' && request.method === 'GET') {
				return new Response(JSON.stringify({ 
					ok: true, 
					message: 'Cloudflare D1 Worker is running',
					timestamp: new Date().toISOString()
				}), {
					headers: { ...corsHeaders, 'Content-Type': 'application/json' },
				});
			}
			
			// Data ingestion endpoint
			if (url.pathname === '/data' && request.method === 'POST') {
				const data = await request.json();
				
				// Validate required fields
				if (!data.mac || !data.data) {
					return new Response(JSON.stringify({ error: 'Missing required fields' }), {
						status: 400,
						headers: { ...corsHeaders, 'Content-Type': 'application/json' },
					});
				}
				
				// Create table for this device if it doesn't exist
				const tableName = createTableName(data.mac);
				await ensureTable(env.DB, tableName);
				
				// Insert data
				const timestamp = Math.floor(Date.now() / 1000);
				const deviceData = data.data;
				
				const insertQuery = `
					INSERT INTO ${tableName} (
						timestamp, 
						device_name,
						temperature, 
						humidity, 
						battery_mv, 
						rssi
					) VALUES (?, ?, ?, ?, ?, ?)
				`;
				
				await env.DB.prepare(insertQuery)
					.bind(
						timestamp,
						data.name || 'Unknown',
						deviceData.temperature || null,
						deviceData.humidity || null,
						deviceData.battery_mv || null,
						deviceData.rssi || null
					)
					.run();
				
				return new Response(JSON.stringify({ 
					ok: true, 
					message: 'Data stored successfully',
					table: tableName
				}), {
					headers: { ...corsHeaders, 'Content-Type': 'application/json' },
				});
			}
			
			// 404 for unknown endpoints
			return new Response(JSON.stringify({ error: 'Not found' }), {
				status: 404,
				headers: { ...corsHeaders, 'Content-Type': 'application/json' },
			});
			
		} catch (error) {
			console.error('Worker error:', error);
			return new Response(JSON.stringify({ 
				error: 'Internal server error',
				message: error.message 
			}), {
				status: 500,
				headers: { ...corsHeaders, 'Content-Type': 'application/json' },
			});
		}
	},
};

/**
 * Create a safe table name from MAC address
 * Example: C0:47:C1:A4:3E:42 -> device_c047c1a43e42
 */
function createTableName(mac) {
	const cleanMac = mac.replace(/:/g, '').toLowerCase();
	return `device_${cleanMac}`;
}

/**
 * Ensure table exists for this device
 */
async function ensureTable(db, tableName) {
	const createTableSQL = `
		CREATE TABLE IF NOT EXISTS ${tableName} (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			timestamp INTEGER NOT NULL,
			device_name TEXT,
			temperature REAL,
			humidity INTEGER,
			battery_mv INTEGER,
			rssi INTEGER
		)
	`;
	
	await db.prepare(createTableSQL).run();
	
	// Create index on timestamp for faster queries
	const createIndexSQL = `
		CREATE INDEX IF NOT EXISTS idx_${tableName}_timestamp 
		ON ${tableName}(timestamp DESC)
	`;
	
	await db.prepare(createIndexSQL).run();
}
