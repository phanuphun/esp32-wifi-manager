#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>

const char* default_ap_ssid = "ESP32-Config";
const char* default_ap_password = "12345678";

WebServer server(80);
Preferences preferences;

void handleRoot() {
  File file = LittleFS.open("/index.html","r");
  if (!file) {
    server.send(500, "text/plain", "ไม่พบไฟล์ index.html");
    return;
  }
  server.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

void handleVuePage() {
  File file = LittleFS.open("/dist/index.html","r");
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

void handleWifiConfig() {
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

void setup() {
  Serial.begin(115200);
  preferences.begin("wifi-config", false);
  LittleFS.begin(true);
  
  WiFi.mode(WIFI_MODE_AP);

  String apSsid = preferences.getString("ssid", default_ap_ssid);
  String apPassword = preferences.getString("password", default_ap_password);

  WiFi.softAP(apSsid.c_str(), apPassword.c_str());

  Serial.println();
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/wifi-config", HTTP_GET, handleWifiConfig);
  server.on("/reset", HTTP_DELETE, handleResetWiFi);
  server.on("/restart", HTTP_PUT, handleEspRestart);
  server.on("/vue",HTTP_GET, handleVuePage);

  server.begin();
  Serial.println("Web Server Started !!");
}

void loop() {
  server.handleClient();
}