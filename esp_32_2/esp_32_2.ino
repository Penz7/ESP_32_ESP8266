
#include "ESP32_NOW.h"
#include "WiFi.h"

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#include <vector>

/* Definitions */

#define ESPNOW_WIFI_CHANNEL 6
#define VIBRATION_PIN 2  // GPIO2 for vibration motor (right hand)

/* Classes */

// Creating a new class that inherits from the ESP_NOW_Peer class is required.

class ESP_NOW_Peer_Class : public ESP_NOW_Peer {
public:
  // Constructor of the class
  ESP_NOW_Peer_Class(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}

  // Destructor of the class
  ~ESP_NOW_Peer_Class() {}

  // Function to register the master peer
  bool add_peer() {
    if (!add()) {
      log_e("Failed to register the broadcast peer");
      return false;
    }
    return true;
  }

  // Function to print the received messages from the master
  void onReceive(const uint8_t *data, size_t len, bool broadcast) {
    Serial.printf("Received a message from master " MACSTR " (%s)\n", MAC2STR(addr()), broadcast ? "broadcast" : "unicast");
    Serial.printf("  Message: %s\n", (char *)data);
    
    // Process message for vibration control
    // Create null-terminated string from data
    char messageBuffer[len + 1];
    memcpy(messageBuffer, data, len);
    messageBuffer[len] = '\0';
    
    String message = String(messageBuffer);
    Serial.printf("[DEBUG] Full message: '%s' (len: %zu)\n", message.c_str(), len);
    
    int pipeIndex = message.indexOf('|');
    if (pipeIndex != -1) {
      String direction = message.substring(pipeIndex + 1);
      direction.trim();
      
      Serial.printf("[DEBUG] Direction: '%s'\n", direction.c_str());
      
      // Control vibration motor based on direction (opposite of master)
      if (direction == "RIGHT" || direction == "SLIGHT_RIGHT") {
        digitalWrite(VIBRATION_PIN, HIGH);
        Serial.printf("[MOTOR] %s detected - Vibration ON\n", direction.c_str());
      } else {
        digitalWrite(VIBRATION_PIN, LOW);
        Serial.printf("[MOTOR] %s detected - Vibration OFF\n", direction.c_str());
      }
    } else {
      Serial.printf("[ERROR] No pipe found in message: '%s'\n", message.c_str());
    }
  }
};

/* Global Variables */

// List of all the masters. It will be populated when a new master is registered
// Note: Using pointers instead of objects to prevent dangling pointers when the vector reallocates
std::vector<ESP_NOW_Peer_Class *> masters;

/* Callbacks */

// Callback called when an unknown peer sends a message
void register_new_master(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {
    Serial.printf("Unknown peer " MACSTR " sent a broadcast message\n", MAC2STR(info->src_addr));
    Serial.println("Registering the peer as a master");

    ESP_NOW_Peer_Class *new_master = new ESP_NOW_Peer_Class(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);
    if (!new_master->add_peer()) {
      Serial.println("Failed to register the new master");
      delete new_master;
      return;
    }
    masters.push_back(new_master);
    Serial.printf("Successfully registered master " MACSTR " (total masters: %zu)\n", MAC2STR(new_master->addr()), masters.size());
  } else {
    // The slave will only receive broadcast messages
    log_v("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
    log_v("Igorning the message");
  }
}

/* Main */

void setup() {
  Serial.begin(115200);

  // Initialize vibration motor
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);
  Serial.println("Vibration motor initialized on GPIO2 (right hand)");

  // Initialize the Wi-Fi module
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }

  Serial.println("ESP-NOW Example - Broadcast Slave");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  // Initialize the ESP-NOW protocol
  if (!ESP_NOW.begin()) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Reeboting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());

  // Register the new peer callback
  ESP_NOW.onNewPeer(register_new_master, nullptr);

  // Test vibration for 500ms
  digitalWrite(VIBRATION_PIN, HIGH);
  delay(500);
  digitalWrite(VIBRATION_PIN, LOW);

  Serial.println("=== ESP32 Navigation (Right) ===");
  Serial.println("Note: vibration motor needs transistor/NPN if >20mA (GPIO2)");
  Serial.println("Setup complete. Waiting for a master to broadcast a message...");
}

void loop() {
  // Motor status logging every 500ms (same as master)
  static unsigned long last_motor_log = 0;
  if (millis() - last_motor_log >= 500) {
    last_motor_log = millis();
    bool on = digitalRead(VIBRATION_PIN);
    Serial.printf("[MOTOR] %s (GPIO2)\n", on ? "ON" : "OFF");
  }

  // Print debug information every 10 seconds
  static unsigned long last_debug = 0;
  if (millis() - last_debug > 10000) {
    last_debug = millis();
    Serial.printf("Registered masters: %zu\n", masters.size());
    for (size_t i = 0; i < masters.size(); i++) {
      if (masters[i]) {
        Serial.printf("  Master %zu: " MACSTR "\n", i, MAC2STR(masters[i]->addr()));
      }
    }
  }

  delay(100);
}
