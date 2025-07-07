#include <esp32cam.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"

// WiFi & API keys
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";
const char* GEMINI_API_KEY = "";
//remmber to add <name>.json at the end of URL
const char* FIREBASE_URL = "";

// NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  //GMT+7 (VN)
const int daylightOffset_sec = 0;

const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void waitForTimeSync() {
  Serial.print("[*] Waiting for NTP time sync");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n[+] Time synchronized");
}

String base64_encode(const uint8_t* data, size_t length) {
  String encoded = "";
  int i = 0;
  uint8_t a3[3], a4[4];
  while (length--) {
    a3[i++] = *(data++);
    if (i == 3) {
      a4[0] = (a3[0] & 0xfc) >> 2;
      a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
      a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
      a4[3] = a3[2] & 0x3f;
      for (i = 0; i < 4; i++) encoded += base64_table[a4[i]];
      i = 0;
    }
  }
  if (i) {
    for (int j = i; j < 3; j++) a3[j] = '\0';
    a4[0] = (a3[0] & 0xfc) >> 2;
    a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
    a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
    a4[3] = a3[2] & 0x3f;
    for (int j = 0; j < i + 1; j++) encoded += base64_table[a4[j]];
    while (i++ < 3) encoded += '=';
  }
  return encoded;
}

String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[-] Failed to get local time");
    return "Time Error";
  }
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}


void sendToFirebase(String plate, String date) {
  HTTPClient http;
  http.begin(FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");
  String json = "{\"number_plate\":\"" + plate + "\",\"date_time\":\"" + date + "\"}";
  int code = http.POST(json);
  Serial.println(code > 0 ? "[+] Firebase Response: " + http.getString() : "[-] Firebase Send Failed: " + String(code));
  http.end();
}

void detectNumberPlate() {
  Serial.println("\n[+] Capturing...");
  auto frame = esp32cam::capture();
  if (!frame) {
    Serial.println("[-] Capture Failed");
    return;
  }

  String img64 = base64_encode(frame->data(), frame->size());
  frame.reset(); // Free memory ASAP

  HTTPClient http;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + String(GEMINI_API_KEY);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String prompt = R"({
    "contents": [{
      "parts": [
        {"inline_data": {"mime_type": "image/jpeg", "data": ")" + img64 + R"("}},
        {"text": "Detect and return only the vehicle number plate in plain text. If not found, return 'No Plate'."}
      ]
    }]
  })";

  int code = http.POST(prompt);
  if (code <= 0) {
    Serial.println("[-] Gemini Request Failed: " + String(code));
    http.end();
    return;
  }

  String resp = http.getString();
  http.end();
  Serial.println("[+] Gemini Response: " + resp);

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, resp)) {
    Serial.println("[-] JSON Parse Failed");
    return;
  }

  const char* text = doc["candidates"][0]["content"]["parts"][0]["text"];
  if (!text) {
    Serial.println("[!] No plate text detected.");
    return;
  }

  String plate = String(text);
  plate.replace("\n", ""); plate.replace("\r", ""); plate.trim();

  if (plate.length() < 4 || plate.indexOf("No Plate") != -1 || plate.indexOf("no") != -1) {
    Serial.println("[!] Plate not valid.");
    return;
  }

  String date = getCurrentTime();
  Serial.println("======= Plate Detected =======");
  Serial.println("📅 Time: " + date);
  Serial.println("🔢 Plate: " + plate);
  Serial.println("================================");
  sendToFirebase(plate, date);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[+] Starting ESP32-CAM...");
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("[-] WiFi Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] WiFi Connected: " + WiFi.localIP().toString());
    
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(Resolution::find(800, 600));
    cfg.setJpeg(80);
    
    if (!Camera.begin(cfg)) {
        Serial.println("[-] Camera Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] Camera Started");
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    waitForTimeSync();  // wait until time sync is successful
    
    xTaskCreate([](void*) {
        while (1) {
            detectNumberPlate();
            delay(30000); // Check every 30 seconds
        }
    }, "PlateTask", 8192, NULL, 1, NULL);
}


void loop() {}
