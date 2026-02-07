#ifndef SETUP_PAGE_H
#define SETUP_PAGE_H

static const char SETUP_HTML_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BLE Monitor - Käyttöönotto</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 500px;
            width: 100%;
            padding: 40px;
        }
        h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 28px;
        }
        .subtitle {
            color: #666;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .form-group {
            margin-bottom: 25px;
        }
        label {
            display: block;
            color: #333;
            font-weight: 600;
            margin-bottom: 8px;
            font-size: 14px;
        }
        input[type="text"],
        input[type="password"],
        select {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
        }
        .role-selector {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            margin-top: 10px;
        }
        .role-option {
            padding: 20px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s;
        }
        .role-option:hover {
            border-color: #667eea;
            background: #f5f7ff;
        }
        .role-option.selected {
            border-color: #667eea;
            background: #667eea;
            color: white;
        }
        .role-option input[type="radio"] {
            display: none;
        }
        .role-icon {
            font-size: 32px;
            margin-bottom: 10px;
        }
        .role-title {
            font-weight: 600;
            font-size: 16px;
            margin-bottom: 5px;
        }
        .role-desc {
            font-size: 12px;
            opacity: 0.8;
        }
        .gateway-ip-group {
            display: none;
            margin-top: 15px;
            padding: 15px;
            background: #f5f7ff;
            border-radius: 10px;
        }
        .gateway-ip-group.show {
            display: block;
        }
        button {
            width: 100%;
            padding: 15px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 10px;
            font-size: 18px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s;
        }
        button:hover {
            transform: translateY(-2px);
        }
        button:active {
            transform: translateY(0);
        }
        .info-box {
            background: #e3f2fd;
            border-left: 4px solid #2196f3;
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 25px;
            font-size: 14px;
            color: #1976d2;
        }
        .status {
            display: none;
            margin-top: 20px;
            padding: 15px;
            border-radius: 10px;
            text-align: center;
            font-weight: 600;
        }
        .status.success {
            background: #4caf50;
            color: white;
            display: block;
        }
        .status.error {
            background: #f44336;
            color: white;
            display: block;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 BLE Live Monitor</h1>
        <p class="subtitle">Käyttöönoton määritykset</p>
        
        <div class="info-box">
            ℹ️ Määritä WiFi-verkko ja valitse, toimiiko tämä laite keskittimenä vai sensorina.
        </div>
        
        <form id="setupForm">
            <div class="form-group">
                <label for="ssid">WiFi-verkko (SSID)</label>
                <input type="text" id="ssid" name="ssid" required placeholder="Esim. Koti-WiFi">
            </div>
            
            <div class="form-group">
                <label for="password">WiFi-salasana</label>
                <input type="password" id="password" name="password" required placeholder="Salasana">
            </div>
            
            <div class="form-group">
                <label>Laitteen rooli</label>
                <div class="role-selector">
                    <label class="role-option" id="role-gateway">
                        <input type="radio" name="role" value="2" required>
                        <div class="role-icon">🏠</div>
                        <div class="role-title">Keskitin</div>
                        <div class="role-desc">Kerää kaikki tiedot</div>
                    </label>
                    <label class="role-option" id="role-sensor">
                        <input type="radio" name="role" value="1" required>
                        <div class="role-icon">📡</div>
                        <div class="role-title">Sensori</div>
                        <div class="role-desc">Lähettää keskittimelle</div>
                    </label>
                </div>
                
                <div class="gateway-ip-group" id="gatewayIpGroup">
                    <label for="gateway_ip">Keskittimen IP-osoite</label>
                    <input type="text" id="gateway_ip" name="gateway_ip" placeholder="Esim. 192.168.1.100">
                    <small style="color: #666; display: block; margin-top: 5px;">
                        Sensori lähettää tiedot tälle osoitteelle
                    </small>
                </div>
            </div>
            
            <button type="submit">💾 Tallenna ja käynnistä</button>
        </form>
        
        <div class="status" id="status"></div>
    </div>
    
    <script>
        const roleOptions = document.querySelectorAll('.role-option');
        const gatewayIpGroup = document.getElementById('gatewayIpGroup');
        const form = document.getElementById('setupForm');
        const status = document.getElementById('status');
        
        // Roolin valinta
        roleOptions.forEach(option => {
            option.addEventListener('click', function() {
                roleOptions.forEach(opt => opt.classList.remove('selected'));
                this.classList.add('selected');
                this.querySelector('input').checked = true;
                
                // Näytä Gateway IP -kenttä vain sensoreille
                const selectedRole = this.querySelector('input').value;
                if (selectedRole === '1') { // Sensor
                    gatewayIpGroup.classList.add('show');
                } else {
                    gatewayIpGroup.classList.remove('show');
                }
            });
        });
        
        // Lomakkeen lähetys
        form.addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const formData = new FormData(form);
            const data = {
                ssid: formData.get('ssid'),
                password: formData.get('password'),
                role: parseInt(formData.get('role')),
                gateway_ip: formData.get('gateway_ip') || ''
            };
            
            try {
                const response = await fetch('/api/setup', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(data)
                });
                
                const result = await response.json();
                
                if (result.ok) {
                    status.className = 'status success';
                    status.textContent = '✓ Asetukset tallennettu! Laite käynnistyy uudelleen...';
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 3000);
                } else {
                    status.className = 'status error';
                    status.textContent = '✗ Virhe: ' + (result.error || 'Tuntematon virhe');
                }
            } catch (error) {
                status.className = 'status error';
                status.textContent = '✗ Yhteysongelma: ' + error.message;
            }
        });
    </script>
</body>
</html>
)rawliteral";

#endif // SETUP_PAGE_H
