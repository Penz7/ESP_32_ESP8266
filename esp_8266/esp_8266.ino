#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <user_interface.h>

#define MOTOR_PIN 2  // ESP-01S chỉ có GPIO0 và GPIO2 khả dụng, dùng GPIO2 cho motor
#define BOOT_PIN 0   // GPIO0 cần pull-up cho boot mode

// ESP32 MAC address (MAC thực tế của ESP32)
uint8_t esp32Mac[] = { 0xE8, 0x06, 0x90, 0x94, 0x05, 0x1C };
// Broadcast MAC để test ESP-NOW communication
uint8_t broadcastMac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

bool espNowTriggeredBlink = false;
unsigned long espNowBlinkStartTime = 0;
const long espNowBlinkDuration = 5000;
unsigned long lastLedBlinkTime = 0;
const long blinkInterval = 500;
bool ledState = false;
String lastEspNowDirection = "";
bool espNowReceived = false;  // Flag để debug nhận ESP-NOW

// ESP-NOW Callback
void onEspNowReceived(uint8_t *mac, uint8_t *data, uint8_t len) {
  espNowReceived = true;                         // Đánh dấu nhận được dữ liệu
  Serial.println("[ESP-NOW] Signal received!");  // In trước để debug

  // Điều khiển motor trực tiếp không cần tắt Serial
  digitalWrite(MOTOR_PIN, HIGH);
  delay(1000);  // Bật motor 1 giây để dễ quan sát
  digitalWrite(MOTOR_PIN, LOW);

  String message = "";
  for (uint8_t i = 0; i < len; i++) {
    message += (char)data[i];
  }
  Serial.print("[ESP-NOW] Received: ");
  Serial.println(message);
  Serial.print("From MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  // Check if received from ESP32 or broadcast
  bool fromEsp32 = true;
  bool fromBroadcast = true;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != esp32Mac[i]) fromEsp32 = false;
    if (mac[i] != broadcastMac[i]) fromBroadcast = false;
  }

  if (fromEsp32) {
    Serial.println("Source: ESP32 (specific MAC)");
  } else if (fromBroadcast) {
    Serial.println("Source: Broadcast");
  } else {
    Serial.println("Source: Unknown device");
  }

  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());

  int firstComma = message.indexOf(',');
  if (firstComma != -1) {
    lastEspNowDirection = message.substring(0, firstComma);
    Serial.print("Direction: ");
    Serial.println(lastEspNowDirection);
    if (lastEspNowDirection == "RIGHT" || lastEspNowDirection == "SLIGHT_RIGHT") {
      espNowTriggeredBlink = true;
      espNowBlinkStartTime = millis();
      Serial.println("Start blinking motor for RIGHT/SLIGHT_RIGHT");
    } else {
      espNowTriggeredBlink = false;
      digitalWrite(MOTOR_PIN, LOW);
      Serial.println("Stop motor for non-RIGHT direction");
    }
  } else {
    Serial.println("Invalid ESP-NOW data");
    lastEspNowDirection = "";
    espNowTriggeredBlink = false;
    digitalWrite(MOTOR_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP8266 ESP-01S");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId());
  Serial.print("Flash Size: ");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");
  // ESP8266 specific info - thay thế getChipModel()
  Serial.print("CPU Frequency: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
  Serial.print("SDK Version: ");
  Serial.println(ESP.getSdkVersion());
  Serial.print("Core Version: ");
  Serial.println(ESP.getCoreVersion());
  Serial.print("Boot Version: ");
  Serial.println(ESP.getBootVersion());
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  // Setup GPIO0 với pull-up cho boot mode
  pinMode(BOOT_PIN, INPUT_PULLUP);
  Serial.print("GPIO0 (Boot) Status: ");
  Serial.println(digitalRead(BOOT_PIN) ? "HIGH (Normal Boot)" : "LOW (Flash Mode)");

  // Setup GPIO2 cho motor control
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  Serial.println("GPIO2 configured for motor control");

  // Test motor trên GPIO2
  Serial.println("Testing motor on GPIO2 for 2 seconds...");
  digitalWrite(MOTOR_PIN, HIGH);
  delay(2000);
  digitalWrite(MOTOR_PIN, LOW);
  Serial.println("Motor test complete");

  // Power management cho ESP-01S
  Serial.println("Configuring power management...");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // Tắt sleep mode để tránh mất kết nối
  Serial.println("WiFi sleep mode disabled");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);           // Wait for WiFi to disconnect
  wifi_set_channel(1);  // Đặt kênh 1
  delay(100);           // Wait for channel change
  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status());

  // Validate ESP-01S specific features
  Serial.println("ESP-01S Hardware Validation:");
  Serial.print("- Available GPIOs: GPIO0, GPIO2, GPIO1(TX), GPIO3(RX)");
  Serial.println();
  Serial.print("- Motor connected to: GPIO2");
  Serial.println();
  Serial.print("- Boot pin (GPIO0): ");
  Serial.println(digitalRead(BOOT_PIN) ? "HIGH" : "LOW");

  // Initialize ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("[ESP-NOW] Initialization failed");
    while (true) delay(1000);
  }
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onEspNowReceived);

  // Register ESP32 as peer with channel 1
  if (esp_now_add_peer(esp32Mac, ESP_NOW_ROLE_CONTROLLER, 1, NULL, 0) == 0) {
    Serial.println("[ESP-NOW] ESP32 peer added successfully");
  } else {
    Serial.println("[ESP-NOW] Failed to add ESP32 peer - Check MAC address!");
  }

  // Also register broadcast MAC for testing
  if (esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_CONTROLLER, 1, NULL, 0) == 0) {
    Serial.println("[ESP-NOW] Broadcast peer added successfully");
  } else {
    Serial.println("[ESP-NOW] Failed to add broadcast peer");
  }

  // Check if peers exist
  if (esp_now_is_peer_exist(esp32Mac)) {
    Serial.println("[ESP-NOW] ESP32 peer verified");
  }
  if (esp_now_is_peer_exist(broadcastMac)) {
    Serial.println("[ESP-NOW] Broadcast peer verified");
  }

  Serial.println("ESP8266 ready for ESP-NOW");
}

void loop() {
  // Reset watchdog timer để tránh timeout
  ESP.wdtFeed();
  
  // Monitor GPIO0 boot pin status và heap memory
  static unsigned long lastBootCheck = 0;
  if (millis() - lastBootCheck >= 5000) {  // Check every 5 seconds
    bool bootStatus = digitalRead(BOOT_PIN);
    if (!bootStatus) {
      Serial.println("WARNING: GPIO0 is LOW - Device may enter flash mode on restart!");
    }
    
    // Monitor heap memory
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 10000) {  // Cảnh báo nếu heap < 10KB
      Serial.print("WARNING: Low heap memory: ");
      Serial.print(freeHeap);
      Serial.println(" bytes");
    }
    
    lastBootCheck = millis();
  }

  if (espNowTriggeredBlink) {
    if (millis() - espNowBlinkStartTime >= espNowBlinkDuration) {
      espNowTriggeredBlink = false;
      digitalWrite(MOTOR_PIN, LOW);
      Serial.println("Stop blinking motor after 5s");
    } else if (millis() - lastLedBlinkTime >= blinkInterval) {
      ledState = !ledState;
      digitalWrite(MOTOR_PIN, ledState ? HIGH : LOW);
      lastLedBlinkTime = millis();
      Serial.println(ledState ? "Motor ON (GPIO2)" : "Motor OFF (GPIO2)");
    }
  } else {
    digitalWrite(MOTOR_PIN, LOW);
    if (espNowReceived) {
      Serial.println("ESP-NOW was received but no RIGHT/SLIGHT_RIGHT direction");
    }
  }
  delay(100);
}