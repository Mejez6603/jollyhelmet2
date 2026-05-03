#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>

// ---> CRITICAL: CHANGE THIS TO MATCH THE GATEWAY'S WIFI CHANNEL <---
#define WIFI_CHANNEL 10

#define SERVO_A_PIN 33 
#define SERVO_B_PIN 17 
#define COIN_PIN    16 
#define BUZZER_PIN  32 
#define RST_PIN     22 
#define SS_PIN      5  
#define FAN_PIN           4
#define HUMIDIFIER_PIN    21
#define UV_LIGHT_PIN      25
#define US_ALCOHOL_TRIG   26
#define US_ALCOHOL_ECHO   27
#define US_HELMET_TRIG    13
#define US_HELMET_ECHO    14

Servo servoA; Servo servoB; MFRC522 mfrc522(SS_PIN, RST_PIN);

// ONLY KNOWS NODE E (THE BOSS)
uint8_t nodeE_MAC[] = {0xB0, 0xCB, 0xD8, 0x03, 0xFF, 0xE0}; 

typedef struct struct_message { char command[16]; int value; } struct_message;
struct_message sendData; struct_message receiveData;

// --- HELPER TO SEND LOGS TO NODE E ---
void sendLogToE(const char* cmd, int val = 0) {
  strcpy(sendData.command, cmd);
  sendData.value = val;
  esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

volatile bool flag_triggerServoA = false; volatile bool flag_triggerServoB = false;
volatile bool flag_resetRFID = false; volatile bool flag_triggerBuzz = false;
volatile int pulseCount = 0; unsigned long lastPulseTime = 0; const int COIN_TIMEOUT = 250;      
volatile unsigned long lastDebounceTime = 0; const int DEBOUNCE_DELAY = 30;     

void IRAM_ATTR coinInterrupt() { unsigned long currentTime = millis(); if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) { pulseCount++; lastPulseTime = currentTime; lastDebounceTime = currentTime; } }

bool servoA_Active = false; unsigned long servoA_Timer = 0;
bool servoB_Active = false; unsigned long servoB_Timer = 0;
bool buzzerActive = false;  unsigned long buzzerTimer = 0; 
bool uvActive = false;      unsigned long uvTimer = 0; 
unsigned long lastSensorRead = 0;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receiveData, incomingData, sizeof(receiveData));
  if (strcmp(receiveData.command, "BUZZ") == 0) { flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "SERVO_A") == 0) { flag_triggerServoA = true; flag_triggerBuzz = true; } 
  else if (strcmp(receiveData.command, "SERVO_B") == 0) { flag_triggerServoB = true; flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "RESET_RFID") == 0) { flag_resetRFID = true; flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "FAN_ON") == 0) { digitalWrite(FAN_PIN, LOW); flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "FAN_OFF") == 0) { digitalWrite(FAN_PIN, HIGH); flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "HUM_ON") == 0) { digitalWrite(HUMIDIFIER_PIN, LOW); flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "HUM_OFF") == 0) { digitalWrite(HUMIDIFIER_PIN, HIGH); flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "UV_ON") == 0) { digitalWrite(UV_LIGHT_PIN, LOW); uvActive = true; uvTimer = millis(); flag_triggerBuzz = true; }
  else if (strcmp(receiveData.command, "UV_OFF") == 0) { digitalWrite(UV_LIGHT_PIN, HIGH); uvActive = false; }
}

float readUltrasonic(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2); digitalWrite(trigPin, HIGH); delayMicroseconds(10); digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000); if (duration == 0) return 999.0; return (duration * 0.0343) / 2.0; 
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA); 
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE); // FORCE SYNC WITH NODE E
  
  esp_now_init();
  esp_now_register_recv_cb(OnDataRecv); esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent); 
  
  esp_now_peer_info_t peerInfoE; memset(&peerInfoE, 0, sizeof(peerInfoE)); 
  memcpy(peerInfoE.peer_addr, nodeE_MAC, 6); peerInfoE.channel = 0; peerInfoE.encrypt = false; peerInfoE.ifidx = WIFI_IF_STA;           
  esp_now_add_peer(&peerInfoE);

  SPI.begin(); mfrc522.PCD_Init(); mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  pinMode(COIN_PIN, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(COIN_PIN), coinInterrupt, FALLING);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(FAN_PIN, OUTPUT); digitalWrite(FAN_PIN, HIGH); 
  pinMode(HUMIDIFIER_PIN, OUTPUT); digitalWrite(HUMIDIFIER_PIN, HIGH); 
  pinMode(UV_LIGHT_PIN, OUTPUT); digitalWrite(UV_LIGHT_PIN, HIGH); 
  pinMode(US_ALCOHOL_TRIG, OUTPUT); pinMode(US_ALCOHOL_ECHO, INPUT);
  pinMode(US_HELMET_TRIG, OUTPUT);  pinMode(US_HELMET_ECHO, INPUT);

  servoA.setPeriodHertz(50); servoB.setPeriodHertz(50);
  servoA.attach(SERVO_A_PIN, 500, 2400); servoB.attach(SERVO_B_PIN, 500, 2400); servoA.write(180); servoB.write(0);   
}

void loop() {
  unsigned long currentMillis = millis();

  if (pulseCount > 0 && (currentMillis - lastPulseTime > COIN_TIMEOUT)) {
    int currentCoin = pulseCount; pulseCount = 0;               
    if (currentCoin == 1 || currentCoin == 5 || currentCoin == 10 || currentCoin == 20) {
      strcpy(sendData.command, "COIN"); sendData.value = currentCoin; 
      esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData)); // Send to Boss
      flag_triggerBuzz = true;      
    } 
  }

  if (currentMillis - lastSensorRead >= 1000) {
    lastSensorRead = currentMillis;
    // Inside ESP32 A loop() ...
    float helm_dist = readUltrasonic(US_HELMET_TRIG, US_HELMET_ECHO); 
    float alc_dist = readUltrasonic(US_ALCOHOL_TRIG, US_ALCOHOL_ECHO);
    
    int h_state = (helm_dist > 2.0 && helm_dist < 40.0) ? 1 : 0; 
    
    // --- UPDATED 12cm CONTAINER MATH ---
    int a_state = 0; 
    if (alc_dist <= 3.5) a_state = 3;       // HIGH
    else if (alc_dist <= 7.0) a_state = 2;  // MID
    else if (alc_dist <= 10.5) a_state = 1; // LOW
    // Anything above 10.5 stays 0 (EMPTY)
    
    strcpy(sendData.command, "SENSOR_H"); sendData.value = h_state; esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData)); delay(10); 
    strcpy(sendData.command, "SENSOR_A"); sendData.value = a_state; esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
  }

  if (uvActive && (currentMillis - uvTimer >= 5000)) { digitalWrite(UV_LIGHT_PIN, HIGH); uvActive = false; }
  
  byte rfid_v = mfrc522.PCD_ReadRegister(MFRC522::VersionReg); 
  if (rfid_v == 0x00 || rfid_v == 0xFF) { mfrc522.PCD_Init(); mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max); delay(10); }
  
  if (flag_resetRFID) { flag_resetRFID = false; pinMode(RST_PIN, OUTPUT); digitalWrite(RST_PIN, LOW); delay(50); digitalWrite(RST_PIN, HIGH); delay(50); mfrc522.PCD_Init(); mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max); }
  if (flag_triggerBuzz) { flag_triggerBuzz = false; digitalWrite(BUZZER_PIN, HIGH); buzzerActive = true; buzzerTimer = millis(); }
  if (buzzerActive && (millis() - buzzerTimer >= 500)) { 
    digitalWrite(BUZZER_PIN, LOW); buzzerActive = false; 
    sendLogToE("LOG_BUZZ", 0); 
  }
  if (flag_triggerServoA) { flag_triggerServoA = false; servoA.write(0); servoA_Active = true; servoA_Timer = millis(); }
  if (flag_triggerServoB) { flag_triggerServoB = false; servoB.write(180); servoB_Active = true; servoB_Timer = millis(); }
  if (servoA_Active && (millis() - servoA_Timer >= 3000)) { 
    servoA.write(180); servoA_Active = false; 
    sendLogToE("LOG_SA", 180); 
  }
  if (servoB_Active && (millis() - servoB_Timer >= 3000)) { 
    servoB.write(0); servoB_Active = false; 
    sendLogToE("LOG_SB", 0); 
  }
  

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uidString = ""; for (byte i = 0; i < mfrc522.uid.size; i++) { uidString += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""); uidString += String(mfrc522.uid.uidByte[i], HEX); }
    uidString.toUpperCase(); int role = 0; if (uidString == "895F6811") role = 1; else if (uidString == "797E2C12") role = 2; 
    strcpy(sendData.command, "RFID"); sendData.value = role; esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
    digitalWrite(BUZZER_PIN, HIGH); buzzerActive = true; buzzerTimer = millis(); 
    mfrc522.PICC_HaltA(); mfrc522.PCD_StopCrypto1(); 
  }
}