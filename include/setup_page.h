#ifndef SETUP_PAGE_H
#define SETUP_PAGE_H

static const char SETUP_HTML_PAGE[] = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>WiFi</title><style>body{font-family:Arial,sans-serif;background:#667eea;padding:20px;margin:0}"
".box{background:#fff;max-width:400px;margin:50px auto;padding:30px;border-radius:10px;box-shadow:0 4px 20px rgba(0,0,0,0.2)}"
"h1{margin:0 0 20px;font-size:24px}h2{margin:20px 0 10px;font-size:18px;border-top:1px solid #ddd;padding-top:20px}"
"input{width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}"
"button{width:100%;padding:12px;background:#667eea;color:#fff;border:none;border-radius:5px;font-size:16px;cursor:pointer}"
"button:hover{background:#5568d3}.msg{margin-top:15px;padding:10px;border-radius:5px;display:none}.ok{background:#4caf50;color:#fff;display:block}"
".err{background:#f44336;color:#fff;display:block}a{color:#667eea;text-decoration:none}small{color:#666;display:block;margin-top:5px}</style></head><body><div class='box'><h1>🔧 WiFi Setup</h1>"
"<form id='f'><input id='s' placeholder='WiFi SSID' required><input id='p' type='password' placeholder='Password' required>"
"<button type='submit'>Save & Connect</button></form><div class='msg' id='m'></div>"
"<h2>🔒 AP Password</h2><form id='ap'><input id='app' type='password' placeholder='New AP Password (8-63 chars)' minlength='8' maxlength='63' required>"
"<small>Current: BLE-Monitor (default: temperature)</small><button type='submit'>Change AP Password</button></form><div class='msg' id='apm'></div></div><script>"
"let checkInterval;document.getElementById('f').onsubmit=async(e)=>{e.preventDefault();const s=document.getElementById('s').value,p=document.getElementById('p').value,m=document.getElementById('m');"
"try{const r=await fetch('/api/setup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,password:p})});"
"const d=await r.json();if(d.ok){m.className='msg ok';m.textContent='✓ Connecting to '+s+'...';checkInterval=setInterval(checkStatus,2000)}"
"else{m.className='msg err';m.textContent='✗ Error: '+(d.error||'Unknown')}}catch(e){m.className='msg err';m.textContent='✗ Connection error'}};"
"async function checkStatus(){try{const r=await fetch('/api/status');const d=await r.json();if(d.connected&&d.ip){clearInterval(checkInterval);"
"const m=document.getElementById('m');m.className='msg ok';m.innerHTML='✓ Connected!<br>Device IP: <a href=\"http://'+d.ip+'\">'+d.ip+'</a>'}}catch(e){}};"
"document.getElementById('ap').onsubmit=async(e)=>{e.preventDefault();const p=document.getElementById('app').value,m=document.getElementById('apm');"
"try{const r=await fetch('/api/ap-password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:p})});"
"const d=await r.json();if(d.ok){m.className='msg ok';m.textContent='✓ '+d.message}else{m.className='msg err';m.textContent='✗ '+(d.error||'Unknown')}}"
"catch(e){m.className='msg err';m.textContent='✗ Connection error'}}"
"</script></body></html>";

#endif // SETUP_PAGE_H
