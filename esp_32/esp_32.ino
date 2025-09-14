#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino_GFX_Library.h>
#include <esp_now.h>
#include <WiFi.h>
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

// ESP-NOW
uint8_t esp8266Mac[] = {0xE8, 0xDB, 0x84, 0xDC, 0x4C, 0x2A}; // THAY BẰNG MAC THỰC TẾ của ESP8266

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

// Function Declarations
void displayNavigation();
void sendEspNowData();
String removeVietnameseDiacritics(String str);

// ESP-NOW Callback
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.printf("[ESP-NOW] Sent to %02X:%02X:%02X:%02X:%02X:%02X, Status: %s\n",
                mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
                status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

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

// Send ESP-NOW Data
void sendEspNowData() {
  String mqttDir = currentDirection;  // Không map SLIGHT_RIGHT để debug dễ hơn
  String payload = mqttDir + "," + currentDistance + "," + roadName;
  payload.trim();
  Serial.print("[ESP-NOW] Sending to MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.print(esp8266Mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.print("[ESP-NOW] Payload: ");
  Serial.println(payload);
  esp_err_t result = esp_now_send(esp8266Mac, (uint8_t *)payload.c_str(), payload.length());
  Serial.printf("[ESP-NOW] Send '%s' => %s\n", payload.c_str(), result == ESP_OK ? "OK" : "FAIL");
}

// BLE Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    deviceConnected = true;
    Serial.println("[BLE] Connected");
    displayNavigation();
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    currentDirection = "";
    currentDistance = "";
    roadName = "";
    digitalWrite(VIBRATION_PIN, LOW);
    Serial.println("[BLE] Disconnected");
    BLEDevice::startAdvertising();
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

    if (currentDirection == "RIGHT" || currentDirection == "SLIGHT_RIGHT") {
      sendEspNowData();
    }

    displayNavigation();
  }
};

// Setup
void setup() {
  pinMode(SYS_EN_PIN, OUTPUT);
  digitalWrite(SYS_EN_PIN, HIGH);

  Serial.begin(115200);
  delay(200);
  Serial.println("=== ESP32 Navigation (Left) ===");
  Serial.println("Note: vibration motor needs transistor/NPN if >20mA (GPIO18)");
  Serial.print("ESP32 MAC: ");
  Serial.println(WiFi.macAddress()); // Thêm để lấy MAC thực tế

  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);

  // Test vibration for 2s
  digitalWrite(VIBRATION_PIN, HIGH);
  delay(500);
  digitalWrite(VIBRATION_PIN, LOW);

  // LCD backlight
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  if (!gfx->begin(40000000)) {
    Serial.println("[LCD] begin() failed!");
    while (true) delay(1000);
  }
  gfx->displayOn();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gfx->fillScreen(DARK_BG);
  drawGradientRect(0, 0, LCD_WIDTH, LCD_HEIGHT, GRADIENT_TOP, GRADIENT_BOT);
  gfx->setTextColor(MY_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(20, 110);
  gfx->println("ESP32 Navigation");
  delay(800);

  // ESP-NOW init
  WiFi.mode(WIFI_STA);
  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Initialization failed");
    while (true) delay(1000);
  }
  esp_now_register_send_cb(onEspNowSent);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, esp8266Mac, 6);
  peerInfo.channel = 1; // Sửa: Dùng channel 1
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Failed to add peer - Check MAC address!");
    while (true) delay(1000);
  }
  Serial.println("[ESP-NOW] Peer added successfully");

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
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
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

  delay(10);
}