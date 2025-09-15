#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define MOTOR_PIN 2  // ESP-01S chỉ có GPIO0 và GPIO2 khả dụng, dùng GPIO2 cho motor
#define BOOT_PIN 0   // GPIO0 cần pull-up cho boot mode

bool serialTriggeredBlink = false;
unsigned long serialBlinkStartTime = 0;  // kept for possible future timeout use
unsigned long lastLedBlinkTime = 0;
const long blinkInterval = 500;
bool ledState = false;
String lastDirection = "";
bool serialReceived = false;  // Flag để debug nhận UART
String uartBuffer = "";

// Wi‑Fi STA + UDP listener config (existing Wi‑Fi)
static const char *STA_SSID = "Van Bach 1";      // TODO: set
static const char *STA_PASS = "0909989547";  // TODO: set
static const uint16_t UDP_PORT = 4210;
WiFiUDP Udp;

// Static IP configuration to always listen on 192.168.1.248:4210
static IPAddress STATIC_IP(192, 168, 1, 248);
static IPAddress STATIC_GW(192, 168, 1, 1);
static IPAddress STATIC_MASK(255, 255, 255, 0);
static IPAddress STATIC_DNS(8, 8, 8, 8);

static void blinkMotorTimes(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(MOTOR_PIN, HIGH);
    delay(onMs);
    digitalWrite(MOTOR_PIN, LOW);
    delay(offMs);
  }
}

static void scanAndReportNetworks() {
  Serial.println("Scanning WiFi networks...");
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (n <= 0) {
    Serial.println("No networks found");
    return;
  }
  Serial.printf("Found %d networks:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("%2d) SSID='%s' RSSI=%d dBm CH=%d ENC=%s BSSID=%s\n",
                  i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  WiFi.channel(i),
                  (WiFi.encryptionType(i) == ENC_TYPE_NONE ? "OPEN" : "SECURE"),
                  WiFi.BSSIDstr(i).c_str());
  }
  // Look specifically for our AP
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == STA_SSID) {
      found = true;
      Serial.printf("Target SSID '%s' visible: RSSI=%d dBm, CH=%d\n", STA_SSID, WiFi.RSSI(i), WiFi.channel(i));
      break;
    }
  }
  if (!found) {
    Serial.printf("Target SSID '%s' NOT found in scan.\n", STA_SSID);
  }
}

// Parse one CSV line: "DIRECTION,DISTANCE,ROAD..."
void handleUartLine(const String &line) {
  serialReceived = true;
  Serial.print("[UART] RX: ");
  Serial.println(line);

  int c1 = line.indexOf(',');
  // Accept commands with or without commas. If no comma, whole line is the command
  if (c1 == -1) {
    lastDirection = line;
  } else {
    lastDirection = line.substring(0, c1);
  }
  lastDirection.trim();
  lastDirection.toUpperCase();
  Serial.print("Direction: ");
  Serial.println(lastDirection);

  if (lastDirection == "RIGHT" || lastDirection == "SLIGHT_RIGHT") {
    serialTriggeredBlink = true;
    serialBlinkStartTime = millis();
    Serial.println("Start blinking/vibrating for RIGHT (continuous until stop command)");
  } else if (lastDirection == "STRAIGHT") {
    serialTriggeredBlink = false;
    digitalWrite(MOTOR_PIN, LOW);
    Serial.println("Stop blinking/vibrating on STRAIGHT");
  } else if (lastDirection == "RESET" || lastDirection == "ARRIVED") {
    ESP.restart();
  } else {
    // For other directions, keep previous state as-is
    Serial.println("Direction not RIGHT/SLIGHT_RIGHT/STRAIGHT -> keep current state");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP8266 ESP-01S");
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId());
  Serial.print("Flash Size: ");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");
  // ESP8266 specific info - thay thế getChipModel()
  Serial.print("CPU Frequency: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
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

  // Validate ESP-01S specific features
  Serial.println("ESP-01S Hardware Validation:");
  Serial.print("- Available GPIOs: GPIO0, GPIO2, GPIO1(TX), GPIO3(RX)");
  Serial.println();
  Serial.print("- Motor connected to: GPIO2");
  Serial.println();
  Serial.print("- Boot pin (GPIO0): ");
  Serial.println(digitalRead(BOOT_PIN) ? "HIGH" : "LOW");
  
  // Wi‑Fi connect to existing AP (static IP)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.setOutputPower(20.5f); // max TX power for better link budget
  wifi_set_phy_mode(PHY_MODE_11G);
  // Configure static IP 192.168.1.248 so UDP listener always uses this address
  bool cfgOk = WiFi.config(STATIC_IP, STATIC_GW, STATIC_MASK, STATIC_DNS);
  Serial.printf("Static IP config %s: IP=%s GW=%s MASK=%s DNS=%s\n",
                cfgOk ? "OK" : "FAILED",
                STATIC_IP.toString().c_str(),
                STATIC_GW.toString().c_str(),
                STATIC_MASK.toString().c_str(),
                STATIC_DNS.toString().c_str());
  Serial.printf("Connecting to AP %s ", STA_SSID);
  WiFi.begin(STA_SSID, STA_PASS);
  const uint32_t connectWindowMs = 20000; // wait up to 20s per boot
  uint32_t tStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - tStart < connectWindowMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected. IP=%s GW=%s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
    // Blink 3 times to indicate successful Wi‑Fi connection
    blinkMotorTimes(3, 150, 150);
  } else {
    Serial.println("WiFi connect failed. Rebooting to retry...");
    digitalWrite(MOTOR_PIN, LOW);
    delay(500);
    ESP.restart();
  }

  // Start UDP listener only after Wi‑Fi connected
  Udp.begin(UDP_PORT);
  Serial.printf("[UDP] Listener on %s:%u\n", WiFi.localIP().toString().c_str(), UDP_PORT);
}

void loop() {
  // Reset watchdog timer để tránh timeout
  ESP.wdtFeed();
  
  // Monitor WiFi connection status
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck >= 10000) {  // Check every 10 seconds
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected! Attempting to reconnect...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(STA_SSID, STA_PASS);
      
      // Wait for reconnection
      unsigned long reconnectStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - reconnectStart < 15000) {
        delay(500);
        Serial.print(".");
        ESP.wdtFeed(); // Feed watchdog during reconnect
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi reconnected successfully!");
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      } else {
        Serial.println("\nWiFi reconnection failed! Rebooting...");
        delay(1000);
        ESP.restart();
      }
    }
    lastWifiCheck = millis();
  }
  
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
    
    // Monitor WiFi signal strength
    if (WiFi.status() == WL_CONNECTED) {
      int32_t rssi = WiFi.RSSI();
      if (rssi < -80) {  // Signal too weak
        Serial.printf("WARNING: Weak WiFi signal: %d dBm\n", rssi);
      }
    }
    
    lastBootCheck = millis();
  }

  // UDP packet reading (broadcast)
  int packetSize = Udp.parsePacket();
  if (packetSize > 0) {
    Serial.printf("[UDP] RX %d bytes from %s:%u\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    char buf[600];
    int toRead = packetSize;
    if (toRead > (int)sizeof(buf) - 1) toRead = sizeof(buf) - 1;
    int len = Udp.read(buf, toRead);
    if (len > 0) {
      buf[len] = 0;
      Serial.print("[UDP] Payload (string): '");
      Serial.print(buf);
      Serial.println("'");
      Serial.print("[UDP] Payload (hex): ");
      for (int i = 0; i < len; i++) {
        Serial.printf("%02X ", (uint8_t)buf[i]);
      }
      Serial.println();
      String line = String(buf);
      line.trim();
      if (line.length() > 0) handleUartLine(line);
    } else {
      Serial.println("[UDP] Read returned 0");
    }
  }

  if (serialTriggeredBlink) {
    if (millis() - lastLedBlinkTime >= blinkInterval) {
      ledState = !ledState;
      digitalWrite(MOTOR_PIN, ledState ? HIGH : LOW);
      lastLedBlinkTime = millis();
      Serial.println(ledState ? "Motor ON (GPIO2)" : "Motor OFF (GPIO2)");
    }
  } else {
    digitalWrite(MOTOR_PIN, LOW);
    if (serialReceived) {
      Serial.println("UART data received but not in RIGHT/SLIGHT_RIGHT state");
      // Clear the flag so this message is printed only once per packet
      serialReceived = false;
    }
  }
  delay(50);
}