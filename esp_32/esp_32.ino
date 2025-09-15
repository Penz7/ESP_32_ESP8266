#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include "pin_config.h"

// Power latch
#define SYS_EN_PIN 41

// Vibration Motor (left hand)
#define VIBRATION_PIN 18 // Use transistor/NPN if current >20mA

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// RGB565 Colors
#define MY_GRAY       0x8410
#define MY_DARKGREY   0x4208
#define SHADOW_GRAY   0x632C
#define DARK_BG       0x18C3
#define GRADIENT_TOP  0x3A2F
#define GRADIENT_BOT  0x18C3
#define MY_YELLOW     0xFFE0
#define MY_CYAN       0x07FF
#define MY_WHITE      0xFFFF

// Display
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// Wi‑Fi STA + UDP broadcast config
static const char *STA_SSID = "Van Bach 1";      // TODO: set
static const char *STA_PASS = "0909989547";  // TODO: set
static const uint16_t UDP_PORT = 4210;
static const IPAddress BROADCAST_IP(255, 255, 255, 255);
WiFiUDP Udp;

// State Variables
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
String currentDirection = "";
String currentDistance = "";
String roadName = "";
unsigned long lastBlinkTime = 0;
bool blinkState = true;
unsigned long lastMotorLog = 0;
bool advertisePending = false;
unsigned long advertiseAtMs = 0;
// AP fallback state
bool apOpenFallbackDone = false;
unsigned long apStartedMs = 0;
// AP settings
int apChannel = 1;
bool apHidden = false;

// Function Declarations
void displayNavigation();
String removeVietnameseDiacritics(String str);

// (Using BLE from phone -> ESP32, then UDP broadcast over existing Wi‑Fi)

// Remove Vietnamese Diacritics
String removeVietnameseDiacritics(String str) {
  struct CharMap { const char* vi; const char* en; };
  const CharMap mapTbl[] = {
    {"á","a"},{"à","a"},{"ả","a"},{"ã","a"},{"ạ","a"},
    {"ă","a"},{"ắ","a"},{"ằ","a"},{"ẳ","a"},{"ẵ","a"},{"ặ","a"},
    {"â","a"},{"ấ","a"},{"ầ","a"},{"ẩ","a"},{"ẫ","a"},{"ậ","a"},
    {"é","e"},{"è","e"},{"ẻ","e"},{"ẽ","e"},{"ẹ","e"},
    {"ê","e"},{"ế","e"},{"ề","e"},{"ể","e"},{"ễ","e"},{"ệ","e"},
    {"í","i"},{"ì","i"},{"ỉ","i"},{"ĩ","i"},{"ị","i"},
    {"ó","o"},{"ò","o"},{"ỏ","o"},{"õ","o"},{"ọ","o"},
    {"ô","o"},{"ố","o"},{"ồ","o"},{"ổ","o"},{"ỗ","o"},{"ộ","o"},
    {"ơ","o"},{"ớ","o"},{"ờ","o"},{"ở","o"},{"ỡ","o"},{"ợ","o"},
    {"ú","u"},{"ù","u"},{"ủ","u"},{"ũ","u"},{"ụ","u"},
    {"ư","u"},{"ứ","u"},{"ừ","u"},{"ử","u"},{"ữ","u"},{"ự","u"},
    {"ý","y"},{"ỳ","y"},{"ỷ","y"},{"ỹ","y"},{"ỵ","y"},
    {"đ","d"},
    {"Á","A"},{"À","A"},{"Ả","A"},{"Ã","A"},{"Ạ","A"},
    {"Ă","A"},{"Ắ","A"},{"Ằ","A"},{"Ẳ","A"},{"Ẵ","A"},{"Ặ","A"},
    {"Â","A"},{"Ấ","A"},{"Ầ","A"},{"Ẩ","A"},{"Ẫ","A"},{"Ậ","A"},
    {"É","E"},{"È","E"},{"Ẻ","E"},{"Ẽ","E"},{"Ẹ","E"},
    {"Ê","E"},{"Ế","E"},{"Ề","E"},{"Ể","E"},{"Ễ","E"},{"Ệ","E"},
    {"Í","I"},{"Ì","I"},{"Ỉ","I"},{"Ĩ","I"},{"Ị","I"},
    {"Ó","O"},{"Ò","O"},{"Ỏ","O"},{"Õ","O"},{"Ọ","O"},
    {"Ô","O"},{"Ố","O"},{"Ồ","O"},{"Ổ","O"},{"Ỗ","O"},{"Ộ","O"},
    {"Ơ","O"},{"Ớ","O"},{"Ờ","O"},{"Ở","O"},{"Ỡ","O"},{"Ợ","O"},
    {"Ú","U"},{"Ù","U"},{"Ủ","U"},{"Ũ","U"},{"Ụ","U"},
    {"Ư","U"},{"Ứ","U"},{"Ừ","U"},{"Ử","U"},{"Ữ","U"},{"Ự","U"},
    {"Ý","Y"},{"Ỳ","Y"},{"Ỷ","Y"},{"Ỹ","Y"},{"Ỵ","Y"},
    {"Đ","D"}
  };
  const int N = sizeof(mapTbl)/sizeof(mapTbl[0]);

  String out = "";
  size_t i = 0;
  while (i < str.length()) {
    bool rep = false;
    for (int k = 0; k < N; k++) {
      String vi = String(mapTbl[k].vi);
      if (i + vi.length() <= str.length() && str.substring(i, i + vi.length()) == vi) {
        out += mapTbl[k].en;
        i += vi.length();
        rep = true;
        break;
      }
    }
    if (!rep) {
      out += str[i];
      i++;
    }
  }
  return out;
}

// Draw UI
void drawGradientRect(int x, int y, int w, int h, uint16_t colorTop, uint16_t colorBot) {
  for (int i = 0; i < h; i++) {
    uint16_t c = gfx->color565(
      map(i, 0, h, (colorTop >> 11) * 8, (colorBot >> 11) * 8),
      map(i, 0, h, ((colorTop >> 5) & 0x3F) * 4, ((colorBot >> 5) & 0x3F) * 4),
      map(i, 0, h, (colorTop & 0x1F) * 8, (colorBot & 0x1F) * 8)
    );
    gfx->drawFastHLine(x, y + i, w, c);
  }
}

void drawBluetoothIcon(int x, int y) {
  gfx->drawLine(x, y, x, y + 12, MY_CYAN);
  gfx->drawLine(x, y + 6, x + 6, y, MY_CYAN);
  gfx->drawLine(x, y + 6, x + 6, y + 12, MY_CYAN);
  gfx->drawLine(x + 3, y + 3, x + 6, y, MY_CYAN);
  gfx->drawLine(x + 3, y + 9, x + 6, y + 12, MY_CYAN);
}

void drawLeftArrow() {
  gfx->fillRect(72, 82, 10, 60, SHADOW_GRAY);
  gfx->fillRect(57, 82, 20, 20, SHADOW_GRAY);
  gfx->fillTriangle(37, 92, 57, 72, 57, 112, SHADOW_GRAY);
  gfx->fillRect(70, 80, 10, 60, MY_WHITE);
  gfx->fillRect(55, 80, 20, 20, MY_WHITE);
  gfx->fillTriangle(35, 90, 55, 70, 55, 110, MY_WHITE);
  gfx->drawRect(70, 80, 10, 60, MY_GRAY);
  gfx->drawRect(55, 80, 20, 20, MY_GRAY);
}

void drawRightArrow() {
  gfx->fillRect(72, 82, 10, 60, SHADOW_GRAY);
  gfx->fillRect(82, 82, 20, 20, SHADOW_GRAY);
  gfx->fillTriangle(122, 92, 102, 72, 102, 112, SHADOW_GRAY);
  gfx->fillRect(70, 80, 10, 60, MY_WHITE);
  gfx->fillRect(80, 80, 20, 20, MY_WHITE);
  gfx->fillTriangle(120, 90, 100, 70, 100, 110, MY_WHITE);
  gfx->drawRect(70, 80, 10, 60, MY_GRAY);
  gfx->drawRect(80, 80, 20, 20, MY_GRAY);
}

void drawStraightArrow() {
  gfx->fillRect(72, 87, 10, 50, SHADOW_GRAY);
  gfx->fillTriangle(77, 72, 57, 92, 97, 92, SHADOW_GRAY);
  gfx->fillRect(70, 85, 10, 50, MY_WHITE);
  gfx->fillTriangle(75, 70, 55, 90, 95, 90, MY_WHITE);
  gfx->drawRect(70, 85, 10, 50, MY_GRAY);
}

void drawSlightLeftArrow() {
  gfx->fillRect(72, 82, 10, 60, SHADOW_GRAY);
  gfx->fillRect(57, 82, 20, 15, SHADOW_GRAY);
  gfx->fillTriangle(42, 92, 57, 72, 67, 102, SHADOW_GRAY);
  gfx->fillRect(70, 80, 10, 60, MY_WHITE);
  gfx->fillRect(55, 80, 20, 15, MY_WHITE);
  gfx->fillTriangle(40, 90, 55, 70, 65, 100, MY_WHITE);
  gfx->drawRect(70, 80, 10, 60, MY_GRAY);
  gfx->drawRect(55, 80, 20, 15, MY_GRAY);
}

void drawSlightRightArrow() {
  gfx->fillRect(72, 82, 10, 60, SHADOW_GRAY);
  gfx->fillRect(82, 82, 20, 15, SHADOW_GRAY);
  gfx->fillTriangle(117, 92, 102, 72, 92, 102, SHADOW_GRAY);
  gfx->fillRect(70, 80, 10, 60, MY_WHITE);
  gfx->fillRect(80, 80, 20, 15, MY_WHITE);
  gfx->fillTriangle(115, 90, 100, 70, 90, 100, MY_WHITE);
  gfx->drawRect(70, 80, 10, 60, MY_GRAY);
  gfx->drawRect(80, 80, 20, 15, MY_GRAY);
}

void drawRoundabout() {
  gfx->fillRect(72, 92, 10, 40, SHADOW_GRAY);
  gfx->drawCircle(75, 92, 22, SHADOW_GRAY);
  gfx->fillTriangle(75, 72, 60, 87, 90, 87, SHADOW_GRAY);
  gfx->fillRect(70, 90, 10, 40, MY_WHITE);
  gfx->drawCircle(75, 90, 20, MY_WHITE);
  gfx->fillTriangle(75, 70, 60, 85, 90, 85, MY_WHITE);
  gfx->drawRect(70, 90, 10, 40, MY_GRAY);
  gfx->drawCircle(75, 90, 20, MY_GRAY);
}

// Main UI
void displayNavigation() {
  gfx->fillScreen(DARK_BG);

  // Header
  drawGradientRect(0, 0, LCD_WIDTH, 50, GRADIENT_TOP, GRADIENT_BOT);
  gfx->fillRoundRect(7, 7, LCD_WIDTH - 14, 36, 8, SHADOW_GRAY);
  gfx->drawRoundRect(5, 5, LCD_WIDTH - 10, 40, 8, MY_GRAY);
  gfx->fillRoundRect(5, 5, LCD_WIDTH - 10, 40, 8, MY_DARKGREY);

  gfx->setTextSize(2);
  gfx->setTextColor(deviceConnected ? MY_CYAN : (blinkState ? MY_GRAY : MY_DARKGREY));
  gfx->setCursor(30, 20);
  drawBluetoothIcon(10, 15);
  gfx->print("BLE: ");
  gfx->println(deviceConnected ? "Ket noi" : "Mat ket noi");

  // Arrow frame
  gfx->fillRoundRect(32, 62, 184, 124, 10, SHADOW_GRAY);
  gfx->fillRoundRect(30, 60, 180, 120, 10, MY_DARKGREY);
  gfx->drawRoundRect(30, 60, 180, 120, 10, MY_GRAY);

  String dispDir = currentDirection;

  if (!deviceConnected && dispDir == "") {
    digitalWrite(VIBRATION_PIN, LOW);
    gfx->setTextSize(2);
    gfx->setTextColor(MY_WHITE);
    gfx->setCursor(50, 110);
    gfx->println("Mat ket noi");
  } else if (dispDir == "LEFT") {
    digitalWrite(VIBRATION_PIN, HIGH);
    drawLeftArrow();
    gfx->setTextSize(2);
    gfx->setTextColor(MY_YELLOW);
    gfx->setCursor(100, 135);
    gfx->println("Re trai");
  } else if (dispDir == "SLIGHT_LEFT") {
    digitalWrite(VIBRATION_PIN, HIGH);
    drawSlightLeftArrow();
    gfx->setTextSize(2);
    gfx->setTextColor(MY_YELLOW);
    gfx->setCursor(85, 135);
    gfx->println("Re trai truoc");
  } else if (dispDir == "RIGHT") {
    digitalWrite(VIBRATION_PIN, LOW);
    drawRightArrow();
    gfx->setTextSize(2);
    gfx->setTextColor(MY_YELLOW);
    gfx->setCursor(100, 135);
    gfx->println("Re phai");
  } else if (dispDir == "SLIGHT_RIGHT") {
    digitalWrite(VIBRATION_PIN, LOW);
    drawSlightRightArrow();
    gfx->setTextSize(2);
    gfx->setTextColor(MY_YELLOW);
    gfx->setCursor(85, 135);
    gfx->println("Re phai truoc");
  } else if (dispDir == "STRAIGHT") {
    digitalWrite(VIBRATION_PIN, LOW);
    drawStraightArrow();
    gfx->setTextSize(2);
    gfx->setTextColor(MY_YELLOW);
    gfx->setCursor(100, 135);
    gfx->println("Di thang");
  } else if (dispDir == "ROUNDABOUT") {
    digitalWrite(VIBRATION_PIN, LOW);
    drawRoundabout();
    gfx->setTextSize(2);
    gfx->setTextColor(MY_YELLOW);
    gfx->setCursor(90, 135);
    gfx->println("Vao vong xoay");
  } else {
    digitalWrite(VIBRATION_PIN, LOW);
    gfx->setTextSize(1);
    gfx->setTextColor(MY_WHITE);
    gfx->setCursor(60, 110);
    gfx->println("Cho du lieu...");
  }

  // Info frame
  gfx->fillRoundRect(7, 192, LCD_WIDTH - 10, 120, 8, SHADOW_GRAY);
  gfx->fillRoundRect(5, 190, LCD_WIDTH - 6, 116, 8, MY_DARKGREY);
  gfx->drawRoundRect(5, 190, LCD_WIDTH - 6, 116, 8, MY_GRAY);

  gfx->setTextColor(MY_WHITE);
  gfx->setTextSize(2);
  gfx->setTextWrap(false);
  gfx->setCursor(10, 200);
  gfx->print("Khoang cach: ");
  gfx->println(dispDir != "" ? currentDistance : "");

  String rn = roadName;
  if (rn.length() > 100) rn = rn.substring(0, 97) + "...";
  gfx->setTextColor(MY_WHITE);
  gfx->setTextSize(2);
  gfx->setTextWrap(true);
  gfx->setCursor(10, 225);
  gfx->println(dispDir != "" ? rn : "");
}

// Helper to send to ESP8266 via UDP
void sendUdpMessage(const String &message) {
  Serial.print("[UDP] TX: ");
  Serial.println(message);
  Udp.beginPacket(BROADCAST_IP, UDP_PORT);
  Udp.print(message);
  Udp.endPacket();
}

// BLE Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    deviceConnected = true;
    Serial.println("[BLE] Connected");
    // When connected, advertising is stopped by the stack; keep it minimal.
    displayNavigation();
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    currentDirection = "";
    currentDistance = "";
    roadName = "";
    digitalWrite(VIBRATION_PIN, LOW);
    Serial.println("[BLE] Disconnected");
    // Delay re-advertising a bit to avoid immediate RF contention
    advertisePending = true;
    advertiseAtMs = millis() + 200; // 100–300 ms
    displayNavigation();
  }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String raw = pChar->getValue();
    String value = String(raw.c_str());
    Serial.printf("[BLE] RX: %s\n", value.c_str());

    if (value.length() == 0 || value.length() > 512) {
      currentDirection = "";
      currentDistance = "";
      roadName = "";
      digitalWrite(VIBRATION_PIN, LOW);
      displayNavigation();
      return;
    }

    int c1 = value.indexOf(',');
    int c2 = value.indexOf(',', c1 + 1);
    if (c1 == -1 || c2 == -1) {
      currentDirection = "";
      currentDistance = "";
      roadName = "";
      digitalWrite(VIBRATION_PIN, LOW);
      displayNavigation();
      return;
    }

    currentDirection = value.substring(0, c1);
    currentDistance = value.substring(c1 + 1, c2);
    roadName = removeVietnameseDiacritics(value.substring(c2 + 1));

    if (currentDirection == "LEFT" || currentDirection == "SLIGHT_LEFT") {
      digitalWrite(VIBRATION_PIN, HIGH);
    } else {
      digitalWrite(VIBRATION_PIN, LOW);
    }

    // Send message to ESP8266 over UDP
    {
      String payload = currentDirection + "," + currentDistance + "," + roadName;
      payload.trim();
      sendUdpMessage(payload);
    }

    displayNavigation();
  }
};

// Setup
void setup() {

  Serial.begin(115200);
  delay(200);

  // Start SoftAP
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

  // First, set country to allow channels 1-13 (many routers use 12/13)
  wifi_country_t cJP = {"JP", 1, 13, WIFI_COUNTRY_POLICY_MANUAL};
  esp_wifi_set_country(&cJP);

  // Optional scan to verify SSID and channel
  Serial.printf("[STA] Scanning for '%s'...\n", STA_SSID);
  int n = WiFi.scanNetworks();
  int foundIdx = -1; int foundCh = 0; int foundRssi = -127;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == String(STA_SSID)) { foundIdx = i; foundCh = WiFi.channel(i); foundRssi = WiFi.RSSI(i); }
  }
  if (foundIdx >= 0) {
    Serial.printf("[STA] Found SSID '%s' CH=%d RSSI=%d dBm\n", STA_SSID, foundCh, foundRssi);
  } else {
    Serial.println("[STA] Target SSID not found in scan (will attempt anyway)");
  }

  Serial.printf("[STA] Connecting to %s\n", STA_SSID);
  WiFi.begin(STA_SSID, STA_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    // Retry once with country US (1-11) if router is restricted
    wifi_country_t cUS = {"US", 1, 11, WIFI_COUNTRY_POLICY_MANUAL};
    esp_wifi_set_country(&cUS);
    Serial.println("[STA] First attempt failed; retry with US country (1-11)");
    WiFi.disconnect();
    delay(300);
    WiFi.begin(STA_SSID, STA_PASS);
    t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[STA] Connected. IP=%s GW=%s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
  } else {
    Serial.println("[STA] Connect failed; will continue and retry later");
  }
  Udp.begin(UDP_PORT);
  Serial.printf("[UDP] Sender ready on %s:%u\n", WiFi.localIP().toString().c_str(), UDP_PORT);

  pinMode(SYS_EN_PIN, OUTPUT);
  digitalWrite(SYS_EN_PIN, HIGH);


  Serial.println("=== ESP32 Navigation (Left) ===");
  Serial.println("Note: vibration motor needs transistor/NPN if >20mA (GPIO18)");
  Serial.println("ESP32 ready (STA + UDP broadcast)");

  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);

  // Test vibration for 2s
  digitalWrite(VIBRATION_PIN, HIGH);
  delay(500);
  digitalWrite(VIBRATION_PIN, LOW);

  // LCD init first (no RF touched yet)
  pinMode(LCD_BL, OUTPUT);
  if (!gfx->begin(40000000)) {
    Serial.println("[LCD] begin() failed!");
    while (true) delay(1000);
  }
  gfx->displayOn();
  // Turn on backlight after successful begin
  digitalWrite(LCD_BL, HIGH);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gfx->fillScreen(DARK_BG);
  drawGradientRect(0, 0, LCD_WIDTH, LCD_HEIGHT, GRADIENT_TOP, GRADIENT_BOT);
  gfx->setTextColor(MY_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(20, 110);
  gfx->println("ESP32 Navigation");
  delay(800);

  // ESP-NOW and Wi-Fi removed. Using UART for inter-device communication.

  // BLE init
  BLEDevice::init("ESP32_Navigation");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pService->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  // Reduce background BLE load
  adv->setScanResponse(false);
  adv->setMinPreferred(0x00);
  BLEDevice::startAdvertising();

  displayNavigation();
}

// Loop
void loop() {
  if (!deviceConnected && (millis() - lastBlinkTime >= 500)) {
    blinkState = !blinkState;
    displayNavigation();
    lastBlinkTime = millis();
  }

  digitalWrite(LCD_BL, HIGH);

  if (millis() - lastMotorLog >= 500) {
    lastMotorLog = millis();
    bool on = digitalRead(VIBRATION_PIN);
    Serial.printf("[MOTOR] %s | Dir=%s (BLE:%d)\n", on ? "ON" : "OFF", currentDirection.c_str(), deviceConnected);
  }

  // Handle delayed BLE re-advertising after disconnect
  if (!deviceConnected && advertisePending && millis() >= advertiseAtMs) {
    BLEDevice::startAdvertising();
    advertisePending = false;
  }

  // No AP fallback in STA mode

  delay(10);
}