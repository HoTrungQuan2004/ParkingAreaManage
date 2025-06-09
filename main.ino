#include <esp32cam.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "time.h"

// WiFi Credentials
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";

// Gemini AI API Key
const char* GEMINI_API_KEY = "your_gemini_api_key";

// Firebase URL (Replace with your Firebase Realtime Database URL)
const char* FIREBASE_URL = "https://your-project-id-default-rtdb.firebaseio.com";

// Parking Configuration
const int TOTAL_PARKING_SLOTS = 100;
int occupiedSlots = 0;
int availableSlots = TOTAL_PARKING_SLOTS;

// NTP Server for Date and Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;  // Adjust for your timezone
const int daylightOffset_sec = 0;

// Detection settings
unsigned long lastDetectionTime = 0;
const unsigned long DETECTION_INTERVAL = 3000; // 3 seconds
const unsigned long COOLDOWN_PERIOD = 10000;   // 10 seconds cooldown between same plate detections
String lastDetectedPlate = "";

// Base64 Encoding Function
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const uint8_t* data, size_t length) {
    String encoded = "";
    int i = 0;
    uint8_t array_3[3], array_4[4];
    
    while (length--) {
        array_3[i++] = *(data++);
        if (i == 3) {
            array_4[0] = (array_3[0] & 0xfc) >> 2;
            array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
            array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
            array_4[3] = array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                encoded += base64_table[array_4[i]];
            i = 0;
        }
    }
    
    // Handle padding
    if (i) {
        for (int j = i; j < 3; j++)
            array_3[j] = '\0';
            
        array_4[0] = (array_3[0] & 0xfc) >> 2;
        array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
        array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
        array_4[3] = array_3[2] & 0x3f;
        
        for (int j = 0; j < i + 1; j++)
            encoded += base64_table[array_4[j]];
            
        while (i++ < 3)
            encoded += '=';
    }
    
    return encoded;
}

// Get Current Date and Time
String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[-] Failed to obtain time");
        return "Time Error";
    }
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

// Get Current Date (for daily counting)
String getCurrentDate() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Date Error";
    }
    char buffer[15];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    return String(buffer);
}

// Get Current Month (for monthly counting)
String getCurrentMonth() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Month Error";
    }
    char buffer[10];
    strftime(buffer, sizeof(buffer), "%Y-%m", &timeinfo);
    return String(buffer);
}

// Check if vehicle exists in Firebase (for exit detection)
bool checkVehicleExists(String numberPlate) {
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/vehicles.json";
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        http.end();
        
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("[-] JSON Parse Error: " + String(error.c_str()));
            return false;
        }
        
        // Check if the plate exists in any vehicle entry
        for (JsonPair kv : doc.as<JsonObject>()) {
            if (kv.value()["number_plate"] == numberPlate) {
                return true;
            }
        }
    }
    http.end();
    return false;
}

// Remove vehicle from Firebase (for exit)
void removeVehicleFromFirebase(String numberPlate) {
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/vehicles.json";
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("[-] JSON Parse Error: " + String(error.c_str()));
            http.end();
            return;
        }
        
        // Find and delete the vehicle entry
        for (JsonPair kv : doc.as<JsonObject>()) {
            if (kv.value()["number_plate"] == numberPlate) {
                String deleteUrl = String(FIREBASE_URL) + "/vehicles/" + String(kv.key().c_str()) + ".json";
                HTTPClient deleteHttp;
                deleteHttp.begin(deleteUrl);
                int deleteCode = deleteHttp.sendRequest("DELETE");
                
                if (deleteCode > 0) {
                    Serial.println("[+] Vehicle removed from Firebase: " + numberPlate);
                    occupiedSlots--;
                    availableSlots++;
                    updateParkingStats();
                } else {
                    Serial.println("[-] Failed to remove vehicle from Firebase");
                }
                deleteHttp.end();
                break;
            }
        }
    }
    http.end();
}

// Update parking statistics in Firebase
void updateParkingStats() {
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/parking_stats.json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{";
    payload += "\"total_slots\": " + String(TOTAL_PARKING_SLOTS) + ",";
    payload += "\"occupied_slots\": " + String(occupiedSlots) + ",";
    payload += "\"available_slots\": " + String(availableSlots) + ",";
    payload += "\"last_updated\": \"" + getCurrentTime() + "\"";
    payload += "}";
    
    int httpCode = http.PUT(payload);
    if (httpCode > 0) {
        Serial.println("[+] Parking stats updated");
    } else {
        Serial.println("[-] Failed to update parking stats");
    }
    http.end();
}

// Update daily count
void updateDailyCount() {
    String currentDate = getCurrentDate();
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/daily_counts/" + currentDate + ".json";
    http.begin(url);
    
    // Get current count
    int currentCount = 0;
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc.containsKey("count")) {
            currentCount = doc["count"];
        }
    }
    
    // Update count
    http.end();
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{";
    payload += "\"date\": \"" + currentDate + "\",";
    payload += "\"count\": " + String(currentCount + 1);
    payload += "}";
    
    httpCode = http.PUT(payload);
    if (httpCode > 0) {
        Serial.println("[+] Daily count updated: " + String(currentCount + 1));
    }
    http.end();
}

// Update monthly count
void updateMonthlyCount() {
    String currentMonth = getCurrentMonth();
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/monthly_counts/" + currentMonth + ".json";
    http.begin(url);
    
    // Get current count
    int currentCount = 0;
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc.containsKey("count")) {
            currentCount = doc["count"];
        }
    }
    
    // Update count
    http.end();
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{";
    payload += "\"month\": \"" + currentMonth + "\",";
    payload += "\"count\": " + String(currentCount + 1);
    payload += "}";
    
    httpCode = http.PUT(payload);
    if (httpCode > 0) {
        Serial.println("[+] Monthly count updated: " + String(currentCount + 1));
    }
    http.end();
}

// Function to Send Vehicle Entry Data to Firebase
void sendVehicleEntryToFirebase(String numberPlate, String dateTime, String imageBase64) {
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/vehicles.json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{";
    payload += "\"number_plate\": \"" + numberPlate + "\",";
    payload += "\"entry_time\": \"" + dateTime + "\",";
    payload += "\"image\": \"" + imageBase64 + "\",";
    payload += "\"status\": \"entered\"";
    payload += "}";
    
    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        Serial.println("[+] Vehicle entry recorded in Firebase");
        occupiedSlots++;
        availableSlots--;
        updateParkingStats();
        updateDailyCount();
        updateMonthlyCount();
    } else {
        Serial.println("[-] Failed to record vehicle entry: " + String(httpCode));
    }
    http.end();
}

// Function to clean and validate number plate
String cleanNumberPlate(String plate) {
    plate.trim();
    plate.toUpperCase();
    
    // Remove common AI response prefixes/suffixes
    if (plate.startsWith("THE NUMBER PLATE IS")) {
        plate = plate.substring(19);
    }
    if (plate.startsWith("NUMBER PLATE:")) {
        plate = plate.substring(13);
    }
    if (plate.startsWith("PLATE:")) {
        plate = plate.substring(6);
    }
    
    plate.trim();
    
    // Basic validation - should be alphanumeric and reasonable length
    if (plate.length() < 3 || plate.length() > 15) {
        return "";
    }
    
    // Check if it contains mostly alphanumeric characters
    int alphanumCount = 0;
    for (int i = 0; i < plate.length(); i++) {
        if (isAlphaNumeric(plate[i]) || plate[i] == '-' || plate[i] == ' ') {
            alphanumCount++;
        }
    }
    
    if (alphanumCount < plate.length() * 0.7) { // At least 70% should be alphanumeric
        return "";
    }
    
    return plate;
}

// Function to Detect Vehicle Number Plate
void detectNumberPlate() {
    Serial.println("\n[+] Capturing Image...");
    auto frame = esp32cam::capture();
    if (frame == nullptr) {
        Serial.println("[-] Capture failed");
        return;
    }
    
    Serial.println("[+] Image captured successfully, size: " + String(frame->size()) + " bytes");
    
    // Convert Image to Base64
    String base64Image = base64_encode(frame->data(), frame->size());
    Serial.println("[+] Image encoded to Base64, length: " + String(base64Image.length()));
    
    // Send Image to Gemini AI
    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:generateContent?key=" + String(GEMINI_API_KEY);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(30000); // 30 seconds timeout
    
    String payload = "{\"contents\":[{";
    payload += "\"parts\":[";
    payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + base64Image + "\"}}";
    payload += ",{\"text\":\"Analyze this image and detect any vehicle license plate or number plate. If you find a license plate, return only the plate number/text in plain text format without any additional explanation. If no license plate is visible or readable, return exactly 'No Plate'.\"}";
    payload += "]}]}";
    
    Serial.println("[+] Sending request to Gemini AI...");
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
        String response = http.getString();
        Serial.println("[+] Gemini AI Response Code: " + String(httpCode));
        
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("[-] JSON Parse Error: " + String(error.c_str()));
            http.end();
            return;
        }
        
        if (!doc.containsKey("candidates") || doc["candidates"].size() == 0) {
            Serial.println("[-] No candidates in AI response");
            http.end();
            return;
        }
        
        const char* aiText = doc["candidates"][0]["content"]["parts"][0]["text"];
        if (!aiText) {
            Serial.println("[-] No text in AI response");
            http.end();
            return;
        }
        
        String plateNumber = cleanNumberPlate(String(aiText));
        Serial.println("[+] Raw AI Response: " + String(aiText));
        Serial.println("[+] Cleaned Plate Number: " + plateNumber);
        
        // Check if valid number plate is detected
        if (plateNumber == "" || plateNumber == "NO PLATE" || 
            plateNumber.indexOf("NO PLATE") != -1 || 
            plateNumber.indexOf("NOT VISIBLE") != -1 ||
            plateNumber.indexOf("UNABLE") != -1) {
            Serial.println("[!] No valid number plate detected");
            http.end();
            return;
        }
        
        // Check cooldown period for same plate
        if (plateNumber == lastDetectedPlate && 
            (millis() - lastDetectionTime) < COOLDOWN_PERIOD) {
            Serial.println("[!] Same plate detected within cooldown period, ignoring");
            http.end();
            return;
        }
        
        // Update last detection
        lastDetectedPlate = plateNumber;
        lastDetectionTime = millis();
        
        String dateTime = getCurrentTime();
        
        Serial.println("\n======= Vehicle Detection =======");
        Serial.println("📅 Date & Time: " + dateTime);
        Serial.println("🔢 Number Plate: " + plateNumber);
        
        // Check if vehicle is exiting (already exists in database)
        if (checkVehicleExists(plateNumber)) {
            Serial.println("🚗 Status: VEHICLE EXITING");
            Serial.println("📊 Occupied Slots: " + String(occupiedSlots - 1));
            Serial.println("📊 Available Slots: " + String(availableSlots + 1));
            Serial.println("=================================\n");
            
            removeVehicleFromFirebase(plateNumber);
        } else {
            // Vehicle is entering
            Serial.println("🚗 Status: VEHICLE ENTERING");
            Serial.println("📊 Occupied Slots: " + String(occupiedSlots + 1));
            Serial.println("📊 Available Slots: " + String(availableSlots - 1));
            Serial.println("=================================\n");
            
            sendVehicleEntryToFirebase(plateNumber, dateTime, base64Image);
        }
        
    } else {
        Serial.println("[-] HTTP Request Failed: " + String(httpCode));
        if (httpCode == -1) {
            Serial.println("[-] Connection timeout or network error");
        }
    }
    http.end();
}

// Initialize parking stats from Firebase
void initializeParkingStats() {
    HTTPClient http;
    String url = String(FIREBASE_URL) + "/vehicles.json";
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, response);
        if (!error) {
            occupiedSlots = 0;
            // Count existing vehicles
            for (JsonPair kv : doc.as<JsonObject>()) {
                occupiedSlots++;
            }
            availableSlots = TOTAL_PARKING_SLOTS - occupiedSlots;
            Serial.println("[+] Initialized parking stats - Occupied: " + String(occupiedSlots) + ", Available: " + String(availableSlots));
            updateParkingStats();
        }
    }
    http.end();
}

// Setup Function
void setup() {
    Serial.begin(115200);
    Serial.println("\n🚗 Starting ESP32-CAM Parking Management System...");
    
    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[+] Connecting to WiFi");
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[-] WiFi Failed!");
        delay(5000);
        ESP.restart();
    }
    
    Serial.println("\n[+] WiFi Connected!");
    Serial.println("[+] IP Address: " + WiFi.localIP().toString());
    Serial.println("[+] Signal Strength: " + String(WiFi.RSSI()) + " dBm");
    
    // Initialize Camera
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(Resolution::find(800, 600));
    cfg.setJpeg(80);
    cfg.setBufferCount(2);
    
    if (!Camera.begin(cfg)) {
        Serial.println("[-] Camera Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] Camera Started Successfully");
    Serial.println("[+] Resolution: 800x600");
    Serial.println("[+] JPEG Quality: 80%");
    
    // Initialize NTP Time
    Serial.println("[+] Synchronizing time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    int timeAttempts = 0;
    while (getCurrentTime() == "Time Error" && timeAttempts < 10) {
        delay(1000);
        timeAttempts++;
        Serial.print(".");
    }
    
    if (getCurrentTime() != "Time Error") {
        Serial.println("\n[+] Time synchronized: " + getCurrentTime());
    } else {
        Serial.println("\n[-] Time synchronization failed, continuing anyway...");
    }
    
    // Initialize parking statistics
    Serial.println("[+] Initializing parking statistics...");
    initializeParkingStats();
    
    Serial.println("\n🎯 System Ready!");
    Serial.println("📊 Total Parking Slots: " + String(TOTAL_PARKING_SLOTS));
    Serial.println("📊 Currently Occupied: " + String(occupiedSlots));
    Serial.println("📊 Currently Available: " + String(availableSlots));
    Serial.println("🔄 Detection Interval: " + String(DETECTION_INTERVAL/1000) + " seconds");
    Serial.println("⏰ Cooldown Period: " + String(COOLDOWN_PERIOD/1000) + " seconds");
    Serial.println("\n🔍 Starting vehicle detection...\n");
    
    // Start Plate Detection Task
    xTaskCreate([](void*) {
        while (1) {
            if (WiFi.status() == WL_CONNECTED) {
                detectNumberPlate();
            } else {
                Serial.println("[-] WiFi disconnected, attempting reconnection...");
                WiFi.reconnect();
                delay(5000);
            }
            delay(DETECTION_INTERVAL);
        }
    }, "PlateDetectionTask", 16384, NULL, 1, NULL);
    
    Serial.println("[+] Detection task started successfully");
}

// Main Loop
void loop() {
    // Keep the main loop light
    delay(1000);
    
    // Optional: Print system status every 30 seconds
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 30000) {
        Serial.println("\n📊 System Status:");
        Serial.println("🔗 WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
        Serial.println("📊 Occupied Slots: " + String(occupiedSlots) + "/" + String(TOTAL_PARKING_SLOTS));
        Serial.println("🕐 Current Time: " + getCurrentTime());
        Serial.println("💾 Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n");
        lastStatusPrint = millis();
    }
}
