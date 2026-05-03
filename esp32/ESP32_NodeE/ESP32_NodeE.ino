// ==========================================
// NODE E: BLYNK IOT GATEWAY (THE BOSS)
// ==========================================
#define BLYNK_TEMPLATE_ID   "TMPL61uYG2t0V"
#define BLYNK_TEMPLATE_NAME "Jolly Helmet"
#define BLYNK_AUTH_TOKEN    "ch5XE0zq36-eR0h25fOEIJGyP0b-6qws"

#include <WiFi.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp32.h>
#include <esp_now.h>
#include "soc/soc.h"             // ADD THIS
#include "soc/rtc_cntl_reg.h"    // ADD THIS

// --- CONNECTION TRACKERS ---
bool nodeA_Online = false;
bool nodeB_Online = false;
bool nodeD_Online = false;
unsigned long lastPingTime = 0;
bool isAutoRefilling = false; // Tracks if the pump is currently running

// --- SPOKE MAC ADDRESSES (Change if your boards change!) ---
uint8_t nodeA_MAC[] = {0xA4, 0xF0, 0x0F, 0x8E, 0x94, 0x38}; // Hardware/Sensors
uint8_t nodeB_MAC[] = {0xA4, 0xF0, 0x0F, 0x90, 0xC9, 0x4C}; // Display
uint8_t nodeD_MAC[] = {0xA4, 0xF0, 0x0F, 0x82, 0x7F, 0xD0}; // Alcohol Pumps

// ==========================================
// THE DIGITAL TWIN: MASTER STATE LEDGER
// ==========================================
struct JollyHelmetState {
  // --- ESP32 A (Hardware & Sensors) ---
  String rfidStatus     = "INVALID"; // INVALID, VIP, MASTER
  int    coinSlot       = 0;         // 1 to 5
  String helmetDetected = "FALSE";   // TRUE / FALSE
  String alcLevelA      = "Empty";   // Empty, Low, Mid, High
  int    buzzerBeep     = 0;         // 0 or 1
  int    servoA         = 180;       // 180 (Default) or 0 (Triggered)
  int    servoB         = 0;         // 0 (Default) or 180 (Triggered)
  int    humidifier     = 0;         // 0 (Close) or 1 (Open)

  // --- ESP32 B (Display & Doors) ---a
  int    joystick       = 0;         
  String panelDoor      = "FALSE";   // Open = TRUE, Closed = FALSE
  String panelLock      = "TRUE";    // Locked = TRUE, Unlocked = FALSE
  String enclosureDoor  = "FALSE";
  String enclosureLock  = "TRUE";
  String backdoor       = "FALSE";
  String backdoorLock   = "TRUE";

  // --- ESP32 D (Alcohol & Pumps) ---
  String alcLevelC      = "Empty";   // Empty, Low, Mid, High
  String alcLevelD      = "Empty";
  String pumpC          = "OFF";     // ON / OFF
  String pumpD          = "OFF";
  String ledC           = "OFF";     // OFF, RED, GREEN, BLUE
  String ledD           = "OFF";
} systemState; 
// ==========================================

typedef struct struct_message { char command[16]; int value; } struct_message;
struct_message sendData; struct_message receiveData;

// 1. Add this globally at the top of Node E
esp_now_send_status_t lastStatus;

// --- STRING TRANSLATOR HELPERS ---
String getAlcString(int val) {
  if (val == 1) return "Low"; if (val == 2) return "Mid"; if (val == 3) return "High"; return "Empty";
}
String getLedString(int val) {
  if (val == 1) return "RED"; if (val == 2) return "GREEN"; if (val == 3) return "BLUE"; return "OFF";
}

// --- THE BULLETPROOF AUTO-REFILL BRAIN ---
void checkAutoRefill() {
  // 1. Look at the physical sensors
  bool aNeedsLiquid = (systemState.alcLevelA != "High"); // True if A is Empty, Low, or Mid
  bool dHasSupply = (systemState.alcLevelD != "Empty");  // True if D is Low, Mid, or High

  // 2. The Golden Rule: Only pump if A is not full AND D is not empty
  bool shouldBePumping = (aNeedsLiquid && dHasSupply);

  if (shouldBePumping) {
    isAutoRefilling = true;
    // Keep sending the ON command to keep Node D's safety timer awake!
    strcpy(sendData.command, "PUMP_D"); sendData.value = 1; 
    esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
    
  } 
  else {
    // We must STOP. Either A reached HIGH, or D just ran EMPTY.
    if (isAutoRefilling) {
      isAutoRefilling = false;
      
      // Send the absolute FORCE STOP command immediately
      strcpy(sendData.command, "PUMP_D"); sendData.value = 0; 
      esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
      
      // Log the reason to your Web Terminal
      if (!dHasSupply) {
        Blynk.virtualWrite(V0, "SYS: Refill HALTED (Tank D is Empty!)\n");
      } else {
        Blynk.virtualWrite(V0, "SYS: Refill COMPLETE (Tank A is High)\n");
      }
    }
  }
}

// --- THE MASTER ROUTER & LOGGER ---
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receiveData, incomingData, sizeof(receiveData));
  
  // // RAW INCOMING PACKET SNIFFER (UPDATED TO IGNORE PINGS)
  // if (strcmp(receiveData.command, "PING") != 0 && strcmp(receiveData.command, "PING_D") != 0) {
  //   Serial.print("⬅️ [RECV] From MAC: ");
  //   for (int i=0; i<6; i++) { Serial.print(info->src_addr[i], HEX); if (i<5) Serial.print(":"); }
  //   Serial.print(" | Command: ["); Serial.print(receiveData.command); Serial.print("]");
  //   Serial.print(" | Value: "); Serial.println(receiveData.value);
  // }

  // ==========================================
  // 1. ESP32 A (PINS 1-10)
  // ==========================================
  if (strcmp(receiveData.command, "RFID") == 0) {
    String rfidStr = "INVALID"; if(receiveData.value == 1) rfidStr = "VIP"; else if(receiveData.value == 2) rfidStr = "MASTER";
    Blynk.virtualWrite(V1, rfidStr); Blynk.virtualWrite(V0, "SYS: RFID Scanned -> " + rfidStr + "\n");
    esp_now_send(nodeB_MAC, (uint8_t *) &receiveData, sizeof(receiveData)); // Forward to Display
  }
  else if (strcmp(receiveData.command, "COIN") == 0) {
    Blynk.virtualWrite(V2, receiveData.value); Blynk.virtualWrite(V0, "SYS: Coin -> " + String(receiveData.value) + "\n");
    esp_now_send(nodeB_MAC, (uint8_t *) &receiveData, sizeof(receiveData)); 
  }
  else if (strcmp(receiveData.command, "SENSOR_H") == 0) {
    String h_str = (receiveData.value == 1) ? "TRUE" : "FALSE";
    Blynk.virtualWrite(V3, h_str); esp_now_send(nodeB_MAC, (uint8_t *) &receiveData, sizeof(receiveData));
  }
  // Replace your current SENSOR_A block with this:
  else if (strcmp(receiveData.command, "SENSOR_A") == 0) {
    systemState.alcLevelA = getAlcString(receiveData.value); // Save state
    Blynk.virtualWrite(V4, systemState.alcLevelA); 
    esp_now_send(nodeB_MAC, (uint8_t *) &receiveData, sizeof(receiveData));
    checkAutoRefill(); // Trigger the Brain
  }
  else if (strcmp(receiveData.command, "LOG_BUZZ") == 0) {
    Blynk.virtualWrite(V5, receiveData.value); Blynk.virtualWrite(V0, "SYS: Buzzed\n");
  }
  else if (strcmp(receiveData.command, "LOG_SA") == 0) {
    Blynk.virtualWrite(V7, receiveData.value); Blynk.virtualWrite(V0, "SYS: Servo A Reset (180)\n");
  }
  else if (strcmp(receiveData.command, "LOG_SB") == 0) {
    Blynk.virtualWrite(V8, receiveData.value); Blynk.virtualWrite(V0, "SYS: Servo B Reset (0)\n");
  }

  // ==========================================
  // 2. ESP32 B (PINS 11-20)
  // ==========================================
  else if (strcmp(receiveData.command, "JOYSTICK") == 0) {
    Blynk.virtualWrite(V11, receiveData.value); // No need to spam V0 with joystick
  }
  else if (strcmp(receiveData.command, "DOOR_PANEL") == 0) {
    String state = (receiveData.value == 1) ? "TRUE" : "FALSE"; 
    Blynk.virtualWrite(V12, state); Blynk.virtualWrite(V0, "SYS: Panel Door -> " + state + "\n");
  }
  else if (strcmp(receiveData.command, "LOCK_PANEL") == 0) {
    String state = (receiveData.value == 1) ? "TRUE" : "FALSE"; // 1 = Locked, 0 = Unlocked
    Blynk.virtualWrite(V13, state); Blynk.virtualWrite(V0, "SYS: Panel Lock -> " + state + "\n");
  }
  else if (strcmp(receiveData.command, "DOOR_ENC") == 0) {
    String state = (receiveData.value == 1) ? "TRUE" : "FALSE"; 
    Blynk.virtualWrite(V14, state); Blynk.virtualWrite(V0, "SYS: Enc Door -> " + state + "\n");
  }
  else if (strcmp(receiveData.command, "LOCK_ENC") == 0) {
    String state = (receiveData.value == 1) ? "TRUE" : "FALSE";
    Blynk.virtualWrite(V15, state); Blynk.virtualWrite(V0, "SYS: Enc Lock -> " + state + "\n");
  }
  else if (strcmp(receiveData.command, "DOOR_BACK") == 0) {
    String state = (receiveData.value == 1) ? "TRUE" : "FALSE"; 
    Blynk.virtualWrite(V16, state); Blynk.virtualWrite(V0, "SYS: Back Door -> " + state + "\n");
  }
  else if (strcmp(receiveData.command, "LOCK_BAK") == 0) {
    String state = (receiveData.value == 1) ? "TRUE" : "FALSE";
    Blynk.virtualWrite(V17, state); Blynk.virtualWrite(V0, "SYS: Back Lock -> " + state + "\n");
  }

  // ==========================================
  // 3. ESP32 D (PINS 31-40) LOG CATCHERS
  // ==========================================
  else if (strcmp(receiveData.command, "ALC_C") == 0) {
    Blynk.virtualWrite(V31, getAlcString(receiveData.value)); esp_now_send(nodeB_MAC, (uint8_t *) &receiveData, sizeof(receiveData));
  }
  else if (strcmp(receiveData.command, "ALC_D") == 0) {
    systemState.alcLevelD = getAlcString(receiveData.value); // Save state
    Blynk.virtualWrite(V32, systemState.alcLevelD); 
    esp_now_send(nodeB_MAC, (uint8_t *) &receiveData, sizeof(receiveData));
    checkAutoRefill(); // Trigger the Brain
  }
  else if (strcmp(receiveData.command, "LOG_PUMPC") == 0) {
    Blynk.virtualWrite(V33, "OFF"); Blynk.virtualWrite(V0, "SYS: Pump C -> OFF\n");
  }
  else if (strcmp(receiveData.command, "LOG_PUMPD") == 0) {
    Blynk.virtualWrite(V34, "OFF"); Blynk.virtualWrite(V0, "SYS: Pump D -> OFF\n");
  }
  else if (strcmp(receiveData.command, "LOG_LEDC") == 0) {
    Blynk.virtualWrite(V35, getLedString(receiveData.value)); Blynk.virtualWrite(V0, "SYS: LED C -> " + getLedString(receiveData.value) + "\n");
  }
  else if (strcmp(receiveData.command, "LOG_LEDD") == 0) {
    Blynk.virtualWrite(V36, getLedString(receiveData.value)); Blynk.virtualWrite(V0, "SYS: LED D -> " + getLedString(receiveData.value) + "\n");
  }

  // ==========================================
  // ROUTE COMMANDS DOWN TO BOARDS
  // ==========================================
  else if (strcmp(receiveData.command, "SERVO_A") == 0 || strcmp(receiveData.command, "SERVO_B") == 0 ||
           strcmp(receiveData.command, "HUM_ON") == 0 || strcmp(receiveData.command, "HUM_OFF") == 0 ||
           strcmp(receiveData.command, "UV_ON") == 0 || strcmp(receiveData.command, "UV_OFF") == 0 ||
           strcmp(receiveData.command, "BUZZ") == 0 || strcmp(receiveData.command, "RESET_RFID") == 0) {
    // Route to Node A
    esp_now_send(nodeA_MAC, (uint8_t *) &receiveData, sizeof(receiveData));
  }
  else if (strcmp(receiveData.command, "LED_C") == 0 || strcmp(receiveData.command, "LED_D") == 0 ||
           strcmp(receiveData.command, "PUMP_C") == 0 || strcmp(receiveData.command, "PUMP_D") == 0) {
    // Route to Node D
    esp_now_send(nodeD_MAC, (uint8_t *) &receiveData, sizeof(receiveData));
  }
} // <-- End of OnDataRecv

// 2. RAW OUTGOING PACKET SNIFFER (UPDATED TO IGNORE PINGS)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  lastStatus = status; // Capture the real success/fail from the radio hardware
  
  // ONLY print to the Serial Monitor if the command is NOT "PING"
  if (strcmp(sendData.command, "PING") != 0) {
    Serial.print("➡️ [SENT] To MAC: ");
    for (int i=0; i<6; i++) { Serial.print(mac_addr[i], HEX); if (i<5) Serial.print(":"); }
    Serial.print(" | Command: ["); Serial.print(sendData.command); Serial.print("]");
    Serial.print(" | Value: "); Serial.print(sendData.value);
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? " (SUCCESS)" : " (FAILED)");
  }
}

// // --- COMMANDS FROM BLYNK DASHBOARD ---
// BLYNK_WRITE(V6) { // Humidifier Toggle
//   if (param.asInt() == 1) { 
//     strcpy(sendData.command, "HUM_ON"); esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
//     Blynk.virtualWrite(V10, "ADMIN: Humidifier ON\n");
//   } else {
//     strcpy(sendData.command, "HUM_OFF"); esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
//     Blynk.virtualWrite(V10, "ADMIN: Humidifier OFF\n");
//   }
// }

// BLYNK_WRITE(V5) { // Force Door Unlock
//   if (param.asInt() == 1) { 
//     strcpy(sendData.command, "WEB_UNLOCK"); esp_now_send(nodeB_MAC, (uint8_t *) &sendData, sizeof(sendData));
//     Blynk.virtualWrite(V10, "ADMIN: Sent Unlock Command\n");
//   }
// }

// ==========================================
// BLYNK APP / WEB DASHBOARD CONTROLS
// ==========================================

// --- NODE A CONTROLS ---
BLYNK_WRITE(V5) { // BUZZER
  systemState.buzzerBeep = param.asInt();
  strcpy(sendData.command, "BUZZ");
  esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V7) { // SERVO A
  systemState.servoA = param.asInt() ? 0 : 180; 
  strcpy(sendData.command, "SERVO_A");
  esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V8) { // SERVO B
  systemState.servoB = param.asInt() ? 180 : 0; 
  strcpy(sendData.command, "SERVO_B");
  esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V9) { // HUMIDIFIER
  systemState.humidifier = param.asInt();
  strcpy(sendData.command, systemState.humidifier == 1 ? "HUM_ON" : "HUM_OFF");
  esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

// --- NODE B CONTROLS ---
BLYNK_WRITE(V13) { // PANEL DOOR LOCK
  systemState.panelLock = param.asInt() ? "TRUE" : "FALSE";
  strcpy(sendData.command, "LOCK_PANEL");
  sendData.value = param.asInt();
  esp_now_send(nodeB_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V15) { // ENCLOSURE DOOR LOCK
  systemState.enclosureLock = param.asInt() ? "TRUE" : "FALSE";
  strcpy(sendData.command, "LOCK_ENC");
  sendData.value = param.asInt();
  esp_now_send(nodeB_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V17) { // BACKDOOR LOCK
  systemState.backdoorLock = param.asInt() ? "TRUE" : "FALSE";
  strcpy(sendData.command, "LOCK_BAK");
  sendData.value = param.asInt();
  esp_now_send(nodeB_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

// --- NODE D CONTROLS ---
BLYNK_WRITE(V33) { // WATER PUMP C
  systemState.pumpC = param.asInt() ? "ON" : "OFF";
  strcpy(sendData.command, "PUMP_C");
  sendData.value = param.asInt();
  esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V34) { // WATER PUMP D
  systemState.pumpD = param.asInt() ? "ON" : "OFF";
  strcpy(sendData.command, "PUMP_D");
  sendData.value = param.asInt();
  esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V35) { // LED C
  // Send 1 to trigger cycle
  strcpy(sendData.command, "LED_C");
  sendData.value = 1;
  esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

BLYNK_WRITE(V36) { // LED D
  // Send 1 to trigger cycle
  strcpy(sendData.command, "LED_D");
  sendData.value = 1;
  esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

// ==========================================
// SYNC MASTER LEDGER TO BLYNK DASHBOARD
// ==========================================
void syncDashboard() {
  // --- ESP32 A (Pins 1-10) ---
  Blynk.virtualWrite(V1, systemState.rfidStatus);
  Blynk.virtualWrite(V2, systemState.coinSlot);
  Blynk.virtualWrite(V3, systemState.helmetDetected);
  Blynk.virtualWrite(V4, systemState.alcLevelA);
  
  // --- ESP32 B (Pins 11-20) ---
  Blynk.virtualWrite(V11, systemState.joystick);
  Blynk.virtualWrite(V12, systemState.panelDoor);
  Blynk.virtualWrite(V14, systemState.enclosureDoor);
  Blynk.virtualWrite(V16, systemState.backdoor);

  // --- ESP32 D (Pins 31-40) ---
  Blynk.virtualWrite(V31, systemState.alcLevelC);
  Blynk.virtualWrite(V32, systemState.alcLevelD);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // DISABLE BROWNOUT DETECTOR
  
  delay(1000); 
  Serial.begin(115200);
  
  WiFi.mode(WIFI_AP_STA); 
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  
  WiFiManager wm;
  if(!wm.autoConnect("Jolly_Helmet_Gateway", "admin123")) { ESP.restart(); } 
  
  Serial.println("WiFi Connected!");
  Serial.print("CRITICAL! SET NODES A, B, AND D TO WIFI CHANNEL: ");
  Serial.println(WiFi.channel());
  
  Blynk.config(BLYNK_AUTH_TOKEN); Blynk.connect(); 
  if (esp_now_init() != ESP_OK) return;
  
  esp_now_register_recv_cb(OnDataRecv); esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  
  // Register Peers
  esp_now_peer_info_t peerInfo; memset(&peerInfo, 0, sizeof(peerInfo)); 
  peerInfo.channel = 0; peerInfo.encrypt = false; 
  memcpy(peerInfo.peer_addr, nodeA_MAC, 6); esp_now_add_peer(&peerInfo);
  memcpy(peerInfo.peer_addr, nodeB_MAC, 6); esp_now_add_peer(&peerInfo);
  memcpy(peerInfo.peer_addr, nodeD_MAC, 6); esp_now_add_peer(&peerInfo);
}

// 3. Replace your Node E loop() with this Strict Tracker
void loop() { 
  Blynk.run(); 

  if (millis() - lastPingTime > 2000) {
    lastPingTime = millis();
    strcpy(sendData.command, "PING"); 

    // STRICT CHECK FOR NODE A
    if (!nodeA_Online) {
      esp_now_send(nodeA_MAC, (uint8_t *) &sendData, sizeof(sendData));
      delay(50); // Small wait for the ACK to return
      if (lastStatus == ESP_NOW_SEND_SUCCESS) {
        nodeA_Online = true;
        Serial.println("✅ REAL CONNECTION: ESP32 A IS RESPONDING!");
      } else {
        Serial.println("Searching for ESP32 A... (No hardware ACK)");
      }
    }

    // Repeat the same logic for B and D...
    if (!nodeB_Online) {
      esp_now_send(nodeB_MAC, (uint8_t *) &sendData, sizeof(sendData));
      delay(50);
      if (lastStatus == ESP_NOW_SEND_SUCCESS) {
        nodeB_Online = true;
        Serial.println("✅ REAL CONNECTION: ESP32 B IS RESPONDING!");
      }
    }

    // Check Node D
    if (!nodeD_Online) {
      esp_now_send(nodeD_MAC, (uint8_t *) &sendData, sizeof(sendData));
      delay(50);
      if (lastStatus == ESP_NOW_SEND_SUCCESS) {
        nodeD_Online = true;
        Serial.println("✅ REAL CONNECTION: ESP32 D IS RESPONDING!");
      }
    }
  }
}