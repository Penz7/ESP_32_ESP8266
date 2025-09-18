#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino_GFX_Library.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <esp_mac.h>
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

// ESP-NOW Configuration
#define ESPNOW_WIFI_CHANNEL 6

// ESP-NOW Broadcast Peer Class
class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer {
public:
  // Constructor of the class using the broadcast address
  ESP_NOW_Broadcast_Peer(uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Broadcast_Peer() {
    remove();
  }

  // Function to properly initialize the ESP-NOW and register the broadcast peer
  bool begin() {
    if (!ESP_NOW.begin() || !add()) {
      log_e("Failed to initialize ESP-NOW or register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to send a message to all devices within the network
  bool send_message(const uint8_t *data, size_t len) {
    if (!send(data, len)) {
      log_e("Failed to broadcast message");
      return false;
    }
    return true;
  }
};

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

// Acknowledgment tracking
unsigned long lastMessageId = 0;
unsigned long lastMessageTime = 0;
bool waitingForAck = false;
int ackRetryCount = 0;
const int MAX_ACK_RETRIES = 3;

// ESP-NOW Global Variables
ESP_NOW_Broadcast_Peer* broadcast_peer = nullptr;

// Function Declarations
void displayNavigation();
String removeVietnameseDiacritics(String str);
void cleanupEspNow();
void handleAcknowledgment(const uint8_t *data, size_t len);

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

// Cleanup ESP-NOW resources
void cleanupEspNow() {
  if (broadcast_peer) {
    delete broadcast_peer;
    broadcast_peer = nullptr;
    Serial.println("[ESP-NOW] Cleanup completed");
  }
}

// Handle acknowledgment from slave devices
void handleAcknowledgment(const uint8_t *data, size_t len) {
  String ackMessage = String((char*)data);
  Serial.printf("[ESP-NOW] ACK received: %s\n", ackMessage.c_str());
  
  // Parse acknowledgment message (format: "ACK|messageId")
  int pipeIndex = ackMessage.indexOf('|');
  if (pipeIndex != -1) {
    String ackType = ackMessage.substring(0, pipeIndex);
    unsigned long ackMessageId = ackMessage.substring(pipeIndex + 1).toInt();
    
    if (ackType == "ACK" && ackMessageId == lastMessageId) {
      waitingForAck = false;
      ackRetryCount = 0;
      Serial.printf("[ESP-NOW] Message %lu acknowledged successfully\n", lastMessageId);
    }
  }
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

// Helper to send navigation data via ESP-NOW with proper encoding and acknowledgment
void sendEspNowMessage(const String &message) {
  if (!broadcast_peer) {
    Serial.println("[ESP-NOW] Broadcast peer not initialized");
    return;
  }
  
  Serial.print("[ESP-NOW] TX: ");
  Serial.println(message);
  
  // Generate unique message ID
  lastMessageId = millis();
  lastMessageTime = lastMessageId;
  
  // Add message ID and timestamp for better tracking
  String enhancedMessage = String(lastMessageId) + "|" + message;
  
  // Calculate exact length without null terminator for ESP-NOW
  size_t enhancedLen = enhancedMessage.length();
  if (enhancedLen > 250) { // ESP-NOW max payload is ~250 bytes
    Serial.println("[ESP-NOW] Message too long, truncating");
    enhancedLen = 250;
  }
  
  // Create buffer with exact size (no extra null terminator)
  uint8_t enhancedData[enhancedLen];
  memcpy(enhancedData, enhancedMessage.c_str(), enhancedLen);
  
  // Debug: Print the actual data being sent
  Serial.printf("[DEBUG] Sending: '%s' (len: %zu)\n", enhancedMessage.c_str(), enhancedLen);
  Serial.printf("[DEBUG] Buffer content: ");
  for (size_t i = 0; i < enhancedLen; i++) {
    Serial.printf("%c", enhancedData[i]);
  }
  Serial.println();
  
  if (!broadcast_peer->send_message(enhancedData, enhancedLen)) {
    Serial.println("[ESP-NOW] Failed to broadcast message");
    ackRetryCount++;
  } else {
    waitingForAck = true;
    Serial.printf("[ESP-NOW] Message sent successfully (ID: %lu, Len: %zu, Waiting for ACK)\n", lastMessageId, enhancedLen);
  }
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

    // Send message via ESP-NOW (only direction)
    {
      String payload = currentDirection;
      payload.trim();
      sendEspNowMessage(payload);
    }

    displayNavigation();
  }
};

// Setup
void setup() {

  Serial.begin(115200);
  delay(200);

  // Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }

  Serial.println("ESP-NOW Navigation Device");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  // Initialize and register the broadcast peer
  broadcast_peer = new ESP_NOW_Broadcast_Peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);
  if (!broadcast_peer->begin()) {
    Serial.println("Failed to initialize ESP-NOW broadcast peer");
    delete broadcast_peer;
    broadcast_peer = nullptr;
    Serial.println("Rebooting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());
  Serial.println("ESP-NOW broadcast ready");

  pinMode(SYS_EN_PIN, OUTPUT);
  digitalWrite(SYS_EN_PIN, HIGH);


  Serial.println("=== ESP32 Navigation (Left) ===");
  Serial.println("Note: vibration motor needs transistor/NPN if >20mA (GPIO18)");
  Serial.println("ESP32 ready (BLE + ESP-NOW broadcast)");

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

  // ESP-NOW initialized for inter-device communication.

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
  // Match older behavior: enable scan response and set min preferred
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

  // Handle delayed BLE re-advertising after disconnect
  if (!deviceConnected && advertisePending && millis() >= advertiseAtMs) {
    BLEDevice::startAdvertising();
    advertisePending = false;
  }

  // Handle ESP-NOW acknowledgment timeout and retry
  if (waitingForAck && (millis() - lastMessageTime > 2000)) { // 2 second timeout
    if (ackRetryCount < MAX_ACK_RETRIES) {
      Serial.printf("[ESP-NOW] ACK timeout, retrying message %lu (attempt %d/%d)\n", 
                    lastMessageId, ackRetryCount + 1, MAX_ACK_RETRIES);
      
      // Resend the last message (only direction)
      String lastMessage = currentDirection;
      lastMessage.trim();
      sendEspNowMessage(lastMessage);
    } else {
      Serial.printf("[ESP-NOW] Max retries reached for message %lu, giving up\n", lastMessageId);
      waitingForAck = false;
      ackRetryCount = 0;
    }
  }

  delay(10);
}