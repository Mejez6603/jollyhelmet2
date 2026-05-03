#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_wifi.h>

// ONLY KNOWS NODE E (THE BOSS)
uint8_t nodeE_MAC[] = {0xB0, 0xCB, 0xD8, 0x03, 0xFF, 0xE0};
#define WIFI_CHANNEL 10

// --- PINOUTS FROM YOUR IMAGE ---
#define PUMP_C_PIN 16
#define PUMP_D_PIN 17   // Fixed from 13 to 17 based on your image!
#define LED_C_R 25
#define LED_C_G 26
#define LED_C_B 27
#define LED_D_R 18
#define LED_D_G 19
#define LED_D_B 23

// ULTRASONIC SENSORS
#define US_ALC_C_TRIG 32 // Added Alc C
#define US_ALC_C_ECHO 34
#define US_ALC_D_TRIG 33
#define US_ALC_D_ECHO 35

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

typedef struct struct_message { char command[16]; int value; } struct_message;
struct_message sendData; struct_message receiveData;

void sendLogToE(const char* cmd, int val = 0) {
  strcpy(sendData.command, cmd); sendData.value = val;
  esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

volatile bool flag_pumpC = false; volatile bool flag_pumpD = false;
volatile bool flag_ledC = false; volatile bool flag_ledD = false;

bool pumpC_Active = false; unsigned long pumpC_Timer = 0;
bool pumpD_Active = false; unsigned long pumpD_Timer = 0;
int ledC_State = 0; int ledD_State = 0;
unsigned long lastSensorRead = 0; 
int lastAlcStateC = -1; int lastAlcStateD = -1; // Track both

void setLED_C() { digitalWrite(LED_C_R, LOW); digitalWrite(LED_C_G, LOW); digitalWrite(LED_C_B, LOW); if (ledC_State == 1) digitalWrite(LED_C_R, HIGH); if (ledC_State == 2) digitalWrite(LED_C_G, HIGH); if (ledC_State == 3) digitalWrite(LED_C_B, HIGH); }
void setLED_D() { digitalWrite(LED_D_R, LOW); digitalWrite(LED_D_G, LOW); digitalWrite(LED_D_B, LOW); if (ledD_State == 1) digitalWrite(LED_D_R, HIGH); if (ledD_State == 2) digitalWrite(LED_D_G, HIGH); if (ledD_State == 3) digitalWrite(LED_D_B, HIGH); }

float readUltrasonic(int trigPin, int echoPin) { digitalWrite(trigPin, LOW); delayMicroseconds(2); digitalWrite(trigPin, HIGH); delayMicroseconds(10); digitalWrite(trigPin, LOW); long duration = pulseIn(echoPin, HIGH, 30000); if (duration == 0) return 999.0; return (duration * 0.0343) / 2.0; }

volatile bool flag_stopPumpD = false; // Add this line right above OnDataRecv

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receiveData, incomingData, sizeof(receiveData));
  if (strcmp(receiveData.command, "PUMP_C") == 0) flag_pumpC = true;
  else if (strcmp(receiveData.command, "PUMP_D") == 0) {
    if (receiveData.value == 1) flag_pumpD = true;     // Turn ON
    else flag_stopPumpD = true;                        // Force OFF
  }
  else if (strcmp(receiveData.command, "LED_C") == 0) flag_ledC = true;
  else if (strcmp(receiveData.command, "LED_D") == 0) flag_ledD = true;
}

// --- UPDATED TO SHOW BOTH C AND D ---
void updateOLED(int stateC, int stateD) {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("ALCOHOL SENSORS");
  
  display.setTextSize(2); 
  // Draw C
  display.setCursor(0, 15); display.print("C:");
  if (stateC == 0) display.println("EMTY"); else if (stateC == 1) display.println("LOW"); else if (stateC == 2) display.println("MID"); else if (stateC == 3) display.println("HIGH");
  
  // Draw D
  display.setCursor(0, 40); display.print("D:");
  if (stateD == 0) display.println("EMTY"); else if (stateD == 1) display.println("LOW"); else if (stateD == 2) display.println("MID"); else if (stateD == 3) display.println("HIGH");

  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(PUMP_C_PIN, OUTPUT); digitalWrite(PUMP_C_PIN, HIGH);
  pinMode(PUMP_D_PIN, OUTPUT); digitalWrite(PUMP_D_PIN, HIGH);
  pinMode(LED_C_R, OUTPUT); pinMode(LED_C_G, OUTPUT); pinMode(LED_C_B, OUTPUT);
  pinMode(LED_D_R, OUTPUT); pinMode(LED_D_G, OUTPUT); pinMode(LED_D_B, OUTPUT);
  setLED_C(); setLED_D(); 
  
  // Init both ultrasonic sensors
  pinMode(US_ALC_C_TRIG, OUTPUT); pinMode(US_ALC_C_ECHO, INPUT);
  pinMode(US_ALC_D_TRIG, OUTPUT); pinMode(US_ALC_D_ECHO, INPUT);

  Wire.begin(21, 22);
  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { updateOLED(0, 0); }

  WiFi.mode(WIFI_STA); esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE); esp_now_init(); esp_now_register_recv_cb(OnDataRecv);
  esp_now_peer_info_t peerInfo; memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, nodeE_MAC, 6); peerInfo.channel = 0; peerInfo.encrypt = false; peerInfo.ifidx = WIFI_IF_STA;           
  esp_now_add_peer(&peerInfo);
}

void loop() {
  // ... inside loop() ...
  unsigned long currentMillis = millis();

  if (flag_pumpC) { flag_pumpC = false; digitalWrite(PUMP_C_PIN, LOW); pumpC_Active = true; pumpC_Timer = currentMillis; }
  if (flag_pumpD) { flag_pumpD = false; digitalWrite(PUMP_D_PIN, LOW); pumpD_Active = true; pumpD_Timer = currentMillis; }
  
  // --- ADD FORCE STOP LOGIC ---
  if (flag_stopPumpD) { 
    flag_stopPumpD = false; pumpD_Active = false; 
    digitalWrite(PUMP_D_PIN, HIGH); sendLogToE("LOG_PUMPD", 0); 
  }
  
  // RENAME THESE TO "LOG_LEDC" and "LOG_LEDD"
  if (flag_ledC) { flag_ledC = false; ledC_State++; if(ledC_State > 3) ledC_State = 0; setLED_C(); sendLogToE("LOG_LEDC", ledC_State); }
  if (flag_ledD) { flag_ledD = false; ledD_State++; if(ledD_State > 3) ledD_State = 0; setLED_D(); sendLogToE("LOG_LEDD", ledD_State); }

  // RENAME THESE TO "LOG_PUMPC" and "LOG_PUMPD"
  if (pumpC_Active && (currentMillis - pumpC_Timer >= 5000)) { digitalWrite(PUMP_C_PIN, HIGH); pumpC_Active = false; sendLogToE("LOG_PUMPC", 0); }
  if (pumpD_Active && (currentMillis - pumpD_Timer >= 5000)) { digitalWrite(PUMP_D_PIN, HIGH); pumpD_Active = false; sendLogToE("LOG_PUMPD", 0); }

  if (currentMillis - lastSensorRead >= 1000) {
    lastSensorRead = currentMillis;
    strcpy(sendData.command, "PING_D"); sendData.value = 0; esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData)); delay(10);
    
    // --- UPDATED 14cm CONTAINER MATH ---
    float alcC_dist = readUltrasonic(US_ALC_C_TRIG, US_ALC_C_ECHO);
    int c_state = 0; if (alcC_dist <= 4.5) c_state = 3; else if (alcC_dist <= 9.0) c_state = 2; else if (alcC_dist <= 12.5) c_state = 1; 
    strcpy(sendData.command, "ALC_C"); sendData.value = c_state; esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
    delay(10);

    float alcD_dist = readUltrasonic(US_ALC_D_TRIG, US_ALC_D_ECHO);
    int d_state = 0; if (alcD_dist <= 4.5) d_state = 3; else if (alcD_dist <= 9.0) d_state = 2; else if (alcD_dist <= 12.5) d_state = 1; 
    strcpy(sendData.command, "ALC_D"); sendData.value = d_state; esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
    
    // Update OLED if either changes
    if (c_state != lastAlcStateC || d_state != lastAlcStateD) { 
      updateOLED(c_state, d_state); 
      lastAlcStateC = c_state; 
      lastAlcStateD = d_state; 
    } 
  }
}