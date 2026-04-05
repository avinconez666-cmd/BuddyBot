/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — ESP32 WiFi + Bluetooth Bridge  ·  V2.0
 * ════════════════════════════════════════════════════════════════════
 *
 *  CHANGES FROM V1.0
 *  ─────────────────
 *  [FIX]  loop() now calls delay(1) after server.handleClient() to
 *         yield to the ESP32 FreeRTOS idle task and prevent the
 *         hardware watchdog from triggering on SDK builds that check
 *         for starvation. This is the documented workaround for
 *         ESP32 Arduino WebServer in tight polling loops.
 *  [FIX]  /status JSON now emits battery as a properly quoted string
 *         field for consistency with all other string fields.
 *  [NEW]  Sensor toggle relay: web UI can send
 *         /cmd?c=TOGGLE_SENSOR:GAS:OFF  which is forwarded to Mega
 *         as CMD:TOGGLE_SENSOR:GAS:OFF.
 *  [NEW]  Web UI shows flame alert banner, battery tier colour, and
 *         sensor status when available.
 *  [NEW]  /health endpoint returns uptime + WiFi RSSI for diagnostics.
 *  [NEW]  Bluetooth echo now confirms sensor toggle commands.
 *
 *  HOW TO CONNECT TO THE WEB UI
 *  ──────────────────────────────
 *  1. Power on BuddyBot. The ESP32 will connect to OPTUS_8B4FC8N.
 *  2. Open the Arduino Serial Monitor at 115200 baud on the ESP32's
 *     USB port. It will print:
 *       [WIFI] IP: 192.168.x.xxx
 *  3. On any phone/tablet connected to the same WiFi network, open a
 *     browser and navigate to:
 *       http://192.168.x.xxx
 *  4. The BuddyBot control panel will load. No app installation needed.
 *  5. If you cannot see the Serial Monitor, find the IP from your
 *     router's DHCP client list (look for "BuddyBot" or the ESP32
 *     MAC address).
 *  6. Bookmark the IP for future use (it may change on DHCP renewal —
 *     set a DHCP reservation in your router for the ESP32 MAC address
 *     to make it permanent).
 *
 *  BLUETOOTH CONTROL
 *  ──────────────────
 *  1. On Android, go to Settings → Bluetooth → scan for "BuddyBot".
 *  2. Pair (no PIN required on most devices).
 *  3. Open a BT Serial terminal app (e.g. Serial Bluetooth Terminal).
 *  4. Connect to "BuddyBot".
 *  5. Send commands: F B L R S AUTO DANCE SLOW NORMAL FAST ESTOP CLEAR
 *     Also: TOGGLE_SENSOR:GAS:OFF  etc.
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>

const char* WIFI_SSID = "OPTUS_8B4FC8N";
const char* WIFI_PASS = "alter62635dx";
const char* BT_NAME   = "BuddyBot";

#define MEGA_RX_PIN 16
#define MEGA_TX_PIN 17

WebServer      server(80);
BluetoothSerial BT;

struct Telemetry {
  String status    = "OFFLINE";
  String battery   = "0.0";
  String mode      = "Manual";
  String front     = "0";
  String left      = "0";
  String right     = "0";
  String rear      = "0";
  String temp      = "0.0";
  String humidity  = "0";
  String gas       = "0";
  String flame     = "0";
  bool   estop     = false;
  bool   autoMode  = false;
  unsigned long lastUpdate = 0;
} telem;

// ─── HTML with flame alert, battery tier colour, sensor toggle UI ───
const char HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
  <title>BuddyBot</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:Arial,sans-serif;background:#1a1a2e;color:#fff;text-align:center;padding:12px;touch-action:manipulation}
    h1{color:#00d4ff;font-size:22px;margin-bottom:10px}
    .card{background:#16213e;border-radius:12px;padding:12px;margin:8px 0}
    .status-row{display:flex;justify-content:space-around;font-size:13px}
    .status-row span{color:#00d4ff;font-weight:bold}
    .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:280px;margin:12px auto}
    button{padding:22px 0;width:100%;font-size:20px;font-weight:bold;border:none;border-radius:12px;cursor:pointer;-webkit-tap-highlight-color:transparent}
    button:active{opacity:.7}
    .fwd  {background:#00d4ff;color:#000;grid-column:2;grid-row:1}
    .left {background:#ff9500;color:#000;grid-column:1;grid-row:2}
    .stp  {background:#ff3333;color:#fff;grid-column:2;grid-row:2;font-size:13px}
    .right{background:#ff9500;color:#000;grid-column:3;grid-row:2}
    .bwd  {background:#00d4ff;color:#000;grid-column:2;grid-row:3}
    .auto {background:#33ff33;color:#000;grid-column:1;grid-row:3;font-size:13px}
    .dance{background:#ff00ff;color:#fff;grid-column:3;grid-row:3;font-size:13px}
    .spd  {background:#16213e;color:#00d4ff;border:2px solid #00d4ff;padding:10px;margin:4px;font-size:13px;border-radius:8px}
    .spd.active{background:#00d4ff;color:#000}
    .sensors{text-align:left;font-size:12px}
    .row{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #0d1b2a}
    .row span:last-child{color:#00d4ff}
    .warn{color:#ff3333!important}
    .bat-warn{color:#ff9500!important}
    .bat-low{color:#ff3333!important}
    #wifiip{font-size:11px;color:#888;margin-top:4px}
    #flame-banner{display:none;background:#4a0000;border:2px solid #ff3333;border-radius:8px;
      padding:8px;margin:6px 0;color:#ff4444;font-weight:bold;font-size:14px;animation:fblink 0.5s infinite}
    @keyframes fblink{0%,100%{opacity:1}50%{opacity:0.6}}
    .sens-toggle{display:inline-block;padding:3px 8px;border-radius:6px;cursor:pointer;
      font-size:11px;font-weight:bold;margin:1px}
    .sens-on {background:#004400;color:#33ff33;border:1px solid #33ff33}
    .sens-off{background:#440000;color:#ff4444;border:1px solid #ff4444}
    details summary{cursor:pointer;color:#00d4ff;font-size:12px;margin-top:8px}
    #sens-cfg{text-align:left;padding:4px 0}
  </style>
</head>
<body>
  <h1>&#129302; BuddyBot</h1>

  <div id="flame-banner">&#128293; FLAME DETECTED — CHECK SURROUNDINGS</div>

  <div class="card">
    <div class="status-row">
      <div>Status<br><span id="sts">--</span></div>
      <div>Battery<br><span id="bat">--</span></div>
      <div>Mode<br><span id="mod">--</span></div>
    </div>
    <div id="wifiip"></div>
  </div>

  <div class="grid">
    <button class="fwd"   ontouchstart="cmd('F')" onmousedown="cmd('F')">&#9650;</button>
    <button class="left"  ontouchstart="cmd('L')" onmousedown="cmd('L')">&#9664;</button>
    <button class="stp"   ontouchstart="cmd('S')" onmousedown="cmd('S')">STOP</button>
    <button class="right" ontouchstart="cmd('R')" onmousedown="cmd('R')">&#9654;</button>
    <button class="bwd"   ontouchstart="cmd('B')" onmousedown="cmd('B')">&#9660;</button>
    <button class="auto"  ontouchstart="cmd('AUTO')"  onmousedown="cmd('AUTO')">AUTO</button>
    <button class="dance" ontouchstart="cmd('DANCE')" onmousedown="cmd('DANCE')">DANCE</button>
  </div>

  <div>
    <button class="spd" id="s-slow"   onclick="cmd('SLOW')">SLOW</button>
    <button class="spd active" id="s-normal" onclick="cmd('NORMAL')">NORMAL</button>
    <button class="spd" id="s-fast"   onclick="cmd('FAST')">FAST</button>
  </div>

  <div class="card sensors" style="margin-top:10px">
    <div class="row"><span>Front</span>  <span id="fr">--</span></div>
    <div class="row"><span>Left</span>   <span id="le">--</span></div>
    <div class="row"><span>Right</span>  <span id="ri">--</span></div>
    <div class="row"><span>Rear</span>   <span id="re">--</span></div>
    <div class="row"><span>Temp</span>   <span id="tp">--</span></div>
    <div class="row"><span>Humidity</span><span id="hu">--</span></div>
    <div class="row"><span>Gas</span>    <span id="ga">--</span></div>
    <div class="row"><span>Flame</span>  <span id="fl">--</span></div>

    <details>
      <summary>Sensor on/off controls</summary>
      <div id="sens-cfg">
        <span class="sens-toggle" id="st-dht"     onclick="toggleSens('DHT')">DHT</span>
        <span class="sens-toggle" id="st-light"   onclick="toggleSens('LIGHT')">Light</span>
        <span class="sens-toggle" id="st-sound"   onclick="toggleSens('SOUND')">Sound</span>
        <span class="sens-toggle" id="st-gas"     onclick="toggleSens('GAS')">Gas</span>
        <span class="sens-toggle" id="st-flame"   onclick="toggleSens('FLAME')">Flame</span>
        <span class="sens-toggle" id="st-pir"     onclick="toggleSens('PIR')">PIR</span>
        <span class="sens-toggle" id="st-tilt"    onclick="toggleSens('TILT')">Tilt</span>
        <span class="sens-toggle" id="st-ir"      onclick="toggleSens('IR')">IR</span>
        <span class="sens-toggle" id="st-us"      onclick="toggleSens('US')">US</span>
        <span class="sens-toggle" id="st-current" onclick="toggleSens('CURRENT')">Current</span>
        <span class="sens-toggle" id="st-gps"     onclick="toggleSens('GPS')">GPS</span>
      </div>
    </details>
  </div>

  <script>
    // Map sensor ID → local toggle state (true = on)
    const sensState = {
      DHT:true,LIGHT:true,SOUND:true,GAS:true,FLAME:true,
      PIR:true,TILT:true,IR:true,US:true,CURRENT:true,GPS:true
    };
    const sensEl = {
      DHT:'st-dht',LIGHT:'st-light',SOUND:'st-sound',GAS:'st-gas',
      FLAME:'st-flame',PIR:'st-pir',TILT:'st-tilt',IR:'st-ir',
      US:'st-us',CURRENT:'st-current',GPS:'st-gps'
    };

    function updateSensUI(id, on) {
      const el = document.getElementById(sensEl[id]);
      if (!el) return;
      el.className = 'sens-toggle ' + (on ? 'sens-on' : 'sens-off');
    }

    function toggleSens(id) {
      sensState[id] = !sensState[id];
      updateSensUI(id, sensState[id]);
      fetch('/cmd?c=TOGGLE_SENSOR:'+id+':'+(sensState[id]?'ON':'OFF')).catch(()=>{});
    }

    // Update all sensor toggles from a status string
    function parseSensStatus(s) {
      const pairs = s.split('|');
      pairs.forEach(p => {
        const kv = p.split(':');
        if (kv.length === 2 && sensEl[kv[0]]) {
          const on = kv[1] === '1';
          sensState[kv[0]] = on;
          updateSensUI(kv[0], on);
        }
      });
    }

    function cmd(c){
      fetch('/cmd?c='+c).catch(()=>{});
      if(['SLOW','NORMAL','FAST'].includes(c)){
        ['slow','normal','fast'].forEach(s=>{
          document.getElementById('s-'+s).classList.toggle('active', s===c.toLowerCase());
        });
      }
    }

    let flameBannerTimer = null;
    function showFlameBanner() {
      document.getElementById('flame-banner').style.display='block';
      clearTimeout(flameBannerTimer);
      flameBannerTimer = setTimeout(()=>{
        document.getElementById('flame-banner').style.display='none';
      }, 5000);
    }

    let prevFlame = '0';
    function poll(){
      fetch('/status').then(r=>r.json()).then(d=>{
        document.getElementById('sts').textContent=d.status;

        // Battery with tier colour
        const batEl = document.getElementById('bat');
        batEl.textContent = d.battery+'V ('+d.pct+'%)';
        batEl.className = d.bat_tier==='CRIT'||d.bat_tier==='LOW' ? 'bat-low' :
                          d.bat_tier==='WARN' ? 'bat-warn' : '';

        document.getElementById('mod').textContent=d.mode;
        document.getElementById('fr').textContent=d.front+'cm';
        document.getElementById('le').textContent=d.left+'cm';
        document.getElementById('ri').textContent=d.right+'cm';
        document.getElementById('re').textContent=d.rear+'cm';
        document.getElementById('tp').textContent=d.temp+'°C';
        document.getElementById('hu').textContent=d.humidity+'%';
        document.getElementById('ga').textContent=d.gas;

        const fl=document.getElementById('fl');
        fl.textContent=d.flame==='1'?'DETECTED':'Clear';
        fl.className=d.flame==='1'?'warn':'';
        if (d.flame==='1' && prevFlame!=='1') showFlameBanner();
        prevFlame = d.flame;

        // Sync sensor toggle UI from server
        if (d.sens_status) parseSensStatus(d.sens_status);
      }).catch(()=>{});
    }
    setInterval(poll,1500);
    poll();

    // Init UI
    Object.keys(sensState).forEach(id => updateSensUI(id, sensState[id]));

    document.addEventListener('touchstart',function(e){
      if(e.target.tagName==='BUTTON'||e.target.classList.contains('sens-toggle')){
        e.preventDefault();
      }
    },{passive:false});
  </script>
</body>
</html>
)rawliteral";

// ════════════════════════════════════════════════════════════════════
//  SENSOR STATUS STRING (mirrors Mega's sensorStatusString output)
//  Stored as last received SENS_ST| payload stripped of prefix/suffix
// ════════════════════════════════════════════════════════════════════
String lastSensStatus = "";

void sendToMega(const String& cmd) {
  Serial2.println("CMD:" + cmd);
  Serial.print("[ESP32] → Mega: CMD:");
  Serial.println(cmd);
}

// ════════════════════════════════════════════════════════════════════
//  TELEMETRY PARSER
// ════════════════════════════════════════════════════════════════════
void parseTelemetry(const String& line) {
  String s = line.substring(6);
  String fields[11];
  int fieldIdx = 0, start = 0;
  for (int i = 0; i <= (int)s.length() && fieldIdx < 11; i++) {
    if (i == (int)s.length() || s[i] == ':') {
      fields[fieldIdx++] = s.substring(start, i);
      start = i + 1;
    }
  }
  if (fieldIdx >= 11) {
    telem.status   = fields[0];
    telem.battery  = fields[1];
    telem.mode     = fields[2];
    telem.front    = fields[3];
    telem.left     = fields[4];
    telem.right    = fields[5];
    telem.rear     = fields[6];
    telem.temp     = fields[7];
    telem.humidity = fields[8];
    telem.gas      = fields[9];
    telem.flame    = fields[10];
    telem.lastUpdate = millis();
  }
}

// ════════════════════════════════════════════════════════════════════
//  MEGA SERIAL HANDLER
// ════════════════════════════════════════════════════════════════════
void handleMegaSerial() {
  static String buf = "";
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      buf.trim();
      if (buf.length() > 0) {
        if (buf.startsWith("TELEM:")) {
          parseTelemetry(buf);
        } else if (buf == "MEGA:BOOT") {
          Serial.println("[ESP32] Mega boot detected — sending READY");
          Serial2.println("READY");
        } else if (buf.startsWith("STATUS|")) {
          telem.estop    = (buf.indexOf("ESTOP:YES") >= 0);
          telem.autoMode = (buf.indexOf("AUTO:ON")   >= 0);
          telem.status   = telem.estop ? "ESTOP" : (telem.autoMode ? "AUTO" : "IDLE");
          int vi = buf.indexOf("BAT:");
          if (vi >= 0) telem.battery = String(buf.substring(vi+4).toFloat(), 1);
        } else if (buf.startsWith("SENS_ST|")) {
          // Strip prefix and suffix, store for /status response
          int end = buf.indexOf("|END");
          lastSensStatus = (end > 0) ? buf.substring(8, end) : buf.substring(8);
          Serial.print("[ESP32] Sensor status: "); Serial.println(lastSensStatus);
        } else {
          Serial.print("[ESP32] Mega: "); Serial.println(buf);
        }
      }
      buf = "";
    } else if (c != '\r') {
      buf += c;
      if (buf.length() > 100) buf = "";
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  BLUETOOTH HANDLER
// ════════════════════════════════════════════════════════════════════
void handleBluetooth() {
  static String btBuf = "";
  while (BT.available()) {
    char c = BT.read();
    if (c == '\n' || c == '\r') {
      btBuf.trim();
      if (btBuf.length() > 0) {
        // Don't uppercase TOGGLE_SENSOR commands (ID values are already uppercase,
        // but preserve case for potential future extensions)
        String upper = btBuf;
        upper.toUpperCase();
        Serial.print("[BT] CMD: "); Serial.println(upper);
        sendToMega(upper);
        BT.println("OK:" + upper);
      }
      btBuf = "";
    } else {
      btBuf += c;
      if (btBuf.length() > 48) btBuf = "";
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  WEB SERVER HANDLERS
// ════════════════════════════════════════════════════════════════════
void handleRoot()  { server.send(200, "text/html", HTML); }

void handleCmd() {
  if (server.hasArg("c")) {
    String c = server.arg("c");
    // TOGGLE_SENSOR commands are already in the right format; other commands uppercased
    if (!c.startsWith("TOGGLE_SENSOR:")) c.toUpperCase();
    sendToMega(c);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing c param");
  }
}

// Battery tier string for web UI
const char* batTierStr() {
  float v = telem.battery.toFloat();
  if (v <= 6.0f)  return "CRIT";
  if (v <= 6.8f)  return "LOW";
  if (v <= 7.2f)  return "WARN";
  return "OK";
}

void handleStatus() {
  if (millis() - telem.lastUpdate > 5000 && telem.lastUpdate > 0)
    telem.status = "STALE";

  // [FIX] battery is now a quoted string field
  String json = "{";
  json += "\"status\":\""  + telem.status    + "\",";
  json += "\"battery\":\"" + telem.battery   + "\",";  // ← quoted
  json += "\"pct\":"       + String(int((telem.battery.toFloat() - 6.0f) / (8.4f - 6.0f) * 100.0f)) + ",";
  json += "\"bat_tier\":\"" + String(batTierStr()) + "\",";
  json += "\"mode\":\""    + telem.mode      + "\",";
  json += "\"front\":"     + telem.front     + ",";
  json += "\"left\":"      + telem.left      + ",";
  json += "\"right\":"     + telem.right     + ",";
  json += "\"rear\":"      + telem.rear      + ",";
  json += "\"temp\":"      + telem.temp      + ",";
  json += "\"humidity\":"  + telem.humidity  + ",";
  json += "\"gas\":"       + telem.gas       + ",";
  json += "\"flame\":\""   + telem.flame     + "\"";
  if (lastSensStatus.length() > 0) {
    json += ",\"sens_status\":\"" + lastSensStatus + "\"";
  }
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleHealth() {
  String json = "{";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"rssi\":"      + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\""      + WiFi.localIP().toString() + "\",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== BUDDYBOT ESP32 BRIDGE V2.0 ===");

  Serial2.begin(115200, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
  Serial.println("[ESP32] Serial2 to Mega ready (GPIO16/17)");

  if (!BT.begin(BT_NAME)) {
    Serial.println("[BT] Failed to start — continuing without BT");
  } else {
    Serial.print("[BT] Discoverable as: "); Serial.println(BT_NAME);
  }

  Serial.print("[WIFI] Connecting to "); Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500); Serial.print("."); attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected!");
    Serial.print("[WIFI] IP: "); Serial.println(WiFi.localIP());
    Serial.println("[WIFI] → Open that IP in your phone browser to control BuddyBot");
  } else {
    Serial.println("\n[WIFI] Failed to connect — check SSID/password");
  }

  server.on("/",       handleRoot);
  server.on("/cmd",    handleCmd);
  server.on("/status", handleStatus);
  server.on("/health", handleHealth);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[WIFI] Web server started on port 80");

  delay(200);
  Serial2.println("READY");
  Serial.println("[ESP32] Sent READY to Mega");
}

// ════════════════════════════════════════════════════════════════════
//  LOOP  — delay(1) yields to FreeRTOS idle task (watchdog fix)
// ════════════════════════════════════════════════════════════════════
void loop() {
  server.handleClient();
  handleMegaSerial();
  handleBluetooth();
  delay(1);   // ← [FIX] yield to prevent hardware watchdog trigger

  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Reconnecting...");
      WiFi.reconnect();
    }
  }
}
