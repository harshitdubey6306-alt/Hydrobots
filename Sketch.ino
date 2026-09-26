#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UrlEncode.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Wokwi Virtual Wi-Fi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// --- Telegram Bot Credentials ---
const String botToken = "8934926726:AAFoTMPGWA-XRji3rsw0w_Niap5u8YdNj-U";
const String chatId   = "2132872018";

// Pin Definitions
const int TDS_PIN = 34;        // Analog TDS Sensor
const int TURBIDITY_PIN = 35;  // Optical Turbidity Sensor
const int PUMP_CUTOFF_LED = 2; // LED representing Water Pump Auto-Cutoff Relay

bool alertTriggered = false;

// Fast push alert dispatcher for Telegram
void sendTelegramAlert(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected - skipping Telegram alert.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(3000); // 3-second timeout

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + botToken + 
               "/sendMessage?chat_id=" + chatId + 
               "&text=" + urlEncode(message);

  if (https.begin(client, url)) {
    int httpCode = https.GET();
    if (httpCode == 200) {
      Serial.println("[Telegram] Alert delivered successfully to phone!");
    } else {
      Serial.printf("[Telegram] Request status code: %d\n", httpCode);
    }
    https.end();
  } else {
    Serial.println("[Telegram] Unable to begin HTTPS connection.");
  }
}

// OLED Screen Renderer
void updateOLED(int tds_ppm, int turb_ntu, bool isHazard) {
  display.clearDisplay();

  // Header Bar
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(6, 0);
  display.print("HYDROBOTS NODE SIH");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Real-Time Sensor Telemetry
  display.setCursor(0, 16);
  display.printf("TDS: %d ppm", tds_ppm);

  display.setCursor(0, 28);
  display.printf("Turbidity: %d NTU", turb_ntu);

  // Status Indicator Box
  display.drawRect(0, 42, 128, 20, SSD1306_WHITE);
  display.setCursor(8, 48);
  if (isHazard) {
    display.print("STATUS: ! HAZARD !");
  } else {
    display.print("STATUS: PURE / SAFE");
  }
  display.display();
}

void setup() {
  Serial.begin(115200);

  // Initialize Pump Cutoff Relay LED (GPIO 2)
  pinMode(PUMP_CUTOFF_LED, OUTPUT);
  digitalWrite(PUMP_CUTOFF_LED, LOW); // Pump Active (Safe)

  // Initialize SSD1306 OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(15, 25);
  display.print("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");

  // Initial Boot Confirmation to Telegram
  sendTelegramAlert(
    "💧 HYDROBOTS Sentinel Online\n"
    "System active. Monitoring inlet lines before pump outlet."
  );
}

void loop() {
  // 1. Read Analog Sensors
  int rawTDS = analogRead(TDS_PIN);
  int rawTurbidity = analogRead(TURBIDITY_PIN);

  // 2. Map 12-bit ADC (0 - 4095) to physical metric scales
  int tds_ppm = map(rawTDS, 0, 4095, 20, 1200);
  int turb_ntu = map(rawTurbidity, 0, 4095, 0, 100);

  // 3. Contamination Evaluation: BIS Limits (TDS > 500 ppm or Turbidity > 25 NTU)
  bool isHazard = (tds_ppm > 500 || turb_ntu > 25);

  // 4. Zero-Delay Local Hardware and Screen Updates
  digitalWrite(PUMP_CUTOFF_LED, isHazard ? HIGH : LOW);
  updateOLED(tds_ppm, turb_ntu, isHazard);

  // 5. State Transition Cloud Triggering
  if (isHazard && !alertTriggered) {
    Serial.println("\n[ALERT] Hazard detected! Tripping pump cutoff relay...");
    String alertMsg = "🚨 WATER HAZARD DETECTED!\n"
                      "Contamination detected in water line:\n"
                      "• TDS: " + String(tds_ppm) + " ppm\n"
                      "• Turbidity: " + String(turb_ntu) + " NTU\n\n"
                      "⚠️ WATER PUMP POWER CUT OFF.";
    sendTelegramAlert(alertMsg);
    alertTriggered = true;
  } 
  else if (!isHazard && alertTriggered) {
    Serial.println("\n[STATUS] Water parameters normalized. Restoring pump power.");
    String recoveryMsg = "✅ Water Normalized\n"
                         "Parameters within safe limits. Pump power restored.";
    sendTelegramAlert(recoveryMsg);
    alertTriggered = false;
  }

  delay(50); // Fast 50ms polling loop for responsive potentiometer tracking
}
