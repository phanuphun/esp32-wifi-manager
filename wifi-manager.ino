#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>

// NATS & DNS
#include <lwip/tcpip.h>
#include <lwip/lwip_napt.h>
#include <esp_netif.h>

const char* default_ap_ssid = "ESP32-Config";
const char* default_ap_password = "12345678";

WebServer server(80);
Preferences preferences;

// STA
bool staConnecting = false;
bool staConnected = false;
unsigned long staConnectStartTime = 0;
const unsigned long STA_CONNECT_TIMEOUT = 15000;  // 15 second

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "ไม่พบไฟล์ index.html");
    return;
  }
  server.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

void enableNAT(void* ctx) {
  ip_napt_enable(WiFi.softAPIP(), 1);
  Serial.println("NAT enabled on AP interface");

  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (apNetif != NULL) {
    
    IPAddress dns = WiFi.dnsIP();
    if (dns == IPAddress(0,0,0,0)) {
      dns = IPAddress(8,8,8,8);
      Serial.println("STA DNS is 0.0.0.0, falling back to 8.8.8.8");
    }

    esp_netif_dns_info_t dnsInfo;
    dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
    dnsInfo.ip.u_addr.ip4.addr = static_cast<uint32_t>(WiFi.dnsIP());

    esp_err_t result = esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &dnsInfo);
    if (result == ESP_OK) {
      Serial.print("DNS Forwarding set to: ");
      Serial.println(WiFi.dnsIP());
    } else {
      Serial.printf("Failed to set DNS info, error: %d\n", result);
    }
  } else {
    Serial.println("Failed to get AP netif handle");
  }

  
}

void handleVuePage() {
  File file = LittleFS.open("/dist/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "ไม่พบไฟล์ index.html");
    return;
  }

  server.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() == 0 || password.length() < 8) {
    server.send(400, "text/html; charset=utf-8",
                "<h3>พลาด: ชื่อ Hotspot ห้ามว่าง และรหัสผ่านต้องยาวอย่างน้อย 8 ตัว</h3>");
    return;
  }

  preferences.putString("ssid", ssid);
  preferences.putString("password", password);

  server.send(200, "text/html; charset=utf-8",
              "<h3>บันทึกแล้ว กำลัง restart...</h3>");

  delay(1000);
  ESP.restart();
}

void handleApWifiConfig() {
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");

  String json = "{";
  json += "\"ssid\":\"" + ssid + "\",";
  json += "\"password\":\"" + password + "\"";
  json += "}";

  server.send(200, "application/json; charset=utf-8", json);
}

void handleResetWiFi() {
  Serial.println("WiFi Resetting...");

  preferences.remove("ssid");
  preferences.remove("password");
  preferences.remove("sta_ssid");
  preferences.remove("sta_password");

  Serial.println("WiFi Reset to factory successfully!");

  // ต้องส่ง response ก่อนเสมอ ก่อนจะสั่ง restart
  server.send(200, "text/html; charset=utf-8",
              "<h3>ระบบทำการคืนค่าโรงงานแล้ว ระบบกำลัง Restart กรุณารอสักครู่</h3>");

  Serial.println("ESP restarting...");
  delay(1000);
  ESP.restart();
}

void handleEspRestart() {
  server.send(200, "text/html; charset=utf-8", "<h3>กำลัง Restart...</h3>");
  Serial.println("ESP restarting...");
  delay(1000);
  ESP.restart();
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".svg")) return "image/svg+xml";
  if (filename.endsWith(".json")) return "application/json";
  return "text/plain";
}

bool handleFileRead(String path) {
  if (!LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  server.streamFile(file, getContentType(path));
  file.close();
  return true;
}

void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    server.send(404, "text/plain", "File Not FOund: " + server.uri());
  }
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " B";
  } else if (bytes < 1024 * 1024) {
    return String(bytes / 1024.0, 2) + " KB";
  } else {
    return String(bytes / 1024.0 / 1024.0, 2) + " MB";
  }
}

void handleLittlefsUsages() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t free = total - used;
  float usedPercent = (float)used / total * 100.0;

  String json = "{";
  json += "\"total\":" + String(total) + ",";
  json += "\"used\":" + String(used) + ",";
  json += "\"free\":" + String(free) + ",";
  json += "\"usedPercent\":" + String(usedPercent, 2) + ",";
  json += "\"totalFormatted\":\"" + formatBytes(total) + "\",";
  json += "\"usedFormatted\":\"" + formatBytes(used) + "\",";
  json += "\"freeFormatted\":\"" + formatBytes(free) + "\"";
  json += "}";

  server.send(200, "application/json; charset=utf-8", json);
}

void connectToSTA() {
  String staSsid = preferences.getString("sta_ssid", "");
  String staPass = preferences.getString("sta_password", "");

  if (staSsid.length() == 0) {
    Serial.println("No STA credentials saved, skip connecting");
    return;
  }

  // Clear old attempt before start connecting prevent "cannot set config"
  WiFi.disconnect(true);
  delay(100);

  Serial.printf("Connecting to STA WiFi: %s\n", staSsid.c_str());
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  staConnecting = true;
  staConnected = false;
  staConnectStartTime = millis();
}

void handleConnectWifi() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() == 0) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"SSID is required\"}");
    return;
  }

  preferences.putString("sta_ssid", ssid);
  preferences.putString("sta_password", password);

  server.send(200, "application/json; charset=utf-8", "{\"message\":\"Saved, connecting...\"}");
  connectToSTA();
}

void handleWifiStatus() {
  String json = "{";
  json += "\"connected\":" + String(staConnected ? "true" : "false") + ",";
  json += "\"connecting\":" + String(staConnecting ? "true" : "false") + ",";
  if (staConnected) {
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
  } else {
    json += "\"ip\":\"\",";
    json += "\"ssid\":\"\",";
    json += "\"rssi\":0";
  }
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void setup() {
  Serial.begin(115200);
  preferences.begin("wifi-config", false);
  LittleFS.begin(true);

  WiFi.mode(WIFI_AP_STA);

  String apSsid = preferences.getString("ssid", default_ap_ssid);
  String apPassword = preferences.getString("password", default_ap_password);
  WiFi.softAP(apSsid.c_str(), apPassword.c_str());

  Serial.println();
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  connectToSTA();

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/ap-wifi-config", HTTP_GET, handleApWifiConfig);
  server.on("/reset", HTTP_DELETE, handleResetWiFi);
  server.on("/restart", HTTP_PUT, handleEspRestart);
  server.on("/vue", HTTP_GET, handleVuePage);
  server.on("/littlefs-usages", HTTP_GET, handleLittlefsUsages);
  server.on("/connect-wifi", HTTP_POST, handleConnectWifi);
  server.on("/wifi-status", HTTP_GET, handleWifiStatus);


  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web Server Started !!");
}

void loop() {
  server.handleClient();

  if (staConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      staConnecting = false;
      staConnected = true;
      Serial.print("STA Connected! IP: ");
      Serial.println(WiFi.localIP());
      tcpip_callback(enableNAT, NULL);
    } else if (millis() - staConnectStartTime > STA_CONNECT_TIMEOUT) {
      staConnecting = false;
      staConnected = false;
      WiFi.disconnect(true);
      Serial.println("STA connection timeout");
    }
  }
}