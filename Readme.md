This is the project to storage information about liecens plate and entry time of vehicles in parking area by using AI Thinker ESP32-CAM, geminiAI, and firebase.

# Workflow summary:
- The ESP32-CAM boots and connects to WIFI
- The camera is initialized, and time is synchronized via NTP.
- A FreeRTOS task repeatedly:
  + Capture an image every 3 seconds and encode it it to Base64
  + Send it to Gemini API to detect the number plate
  + Validates the detected plate
  + Stores valid plates and timestamps in Firebase
- Debug messages are printed to the Serial Monitor for monitoring
  
# Set up:
### Set up driver:
- Extract CP210x_Windows_Drivers_with_Serial_Enumeration.zip
- Run x64 or x86 installer depend on your device.
### Set up Gemini API:
- Access and sign in the Gemini AI Developers (https://ai.google.dev)
- In Solution, choose Gemini API
- Choose "Get a Gemini API Key"
- Choose "Create API Key" and create an API Key (can skip if already have one)
### Set up Firebase:
- Access Firebase and sign in Firebase (firebase.google.com)
- Choose "Get started in console"
- Choose "Create a firebase project" or choose your project if already have
- Choose "Build" in the task bar at the right
- Select "Realtime database" and create a database (choose "Start in test mode")
### Set up Arduino IDE:
- Additional board manager URLs (in case you don't have): https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
- install library:
  +  esp32cam-main.zip in this repository
  +  ArduinoJson

# Running:
- open main.ino in Arduino IDE
- select AI Thinker ESP32-CAM as your board and choose your port
- Fill your WIFI SSID and Password
- Fill GEMINI_API_KEY with your API Key on https://ai.google.dev
- Fill FIREBASE_URL with your URL on Firebase realtime database, and then add data.json at the end of this URL (you can replace data in data.json with another name) (Eg: firbease-link/data.json)
- Modify and Upload code to your ESP32-CAM

# Notes:
- Connection errors may occur if the network is weak
- The speed of recognizing and sending information of a license plate can be a bit slow
- Errors may occur when the image is blurred, or letters and numbers are obscured by obstacles such as mud. A fix is ​​being worked on
