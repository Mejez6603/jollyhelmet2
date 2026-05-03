#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

TFT_eSPI tft = TFT_eSPI();


// ---> CRITICAL: CHANGE THIS TO MATCH THE GATEWAY'S WIFI CHANNEL <---
#define WIFI_CHANNEL 10
#define SENSOR_PANEL     19
#define SENSOR_ENCLOSURE 22
#define SENSOR_BACKDOOR  23
#define JOYSTICK_SW      25 
#define JOYSTICK_VRX     34 
#define JOYSTICK_VRY     35 
#define PANEL_LOCK_RELAY     16 
#define ENCLOSURE_LOCK_RELAY 21 
#define BACKDOOR_LOCK_RELAY  17


// ONLY KNOWS NODE E (THE BOSS)
uint8_t nodeE_MAC[] = {0xB0, 0xCB, 0xD8, 0x03, 0xFF, 0xE0};
typedef struct struct_message { char command[16]; int value; } struct_message;
struct_message sendData; struct_message receiveData;

void sendLogToE(const char* cmd, int val = 0) {
  strcpy(sendData.command, cmd); sendData.value = val;
  esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData));
}

void triggerSystemLock(const char* commandToSend, unsigned long customLockTime = 0);

bool isNodeAOnline = false; bool isNodeDOnline = false; 
unsigned long lastPingTimeA = 0; unsigned long lastPingTimeD = 0; 
bool isSystemBusy = false; unsigned long busyTimer = 0; unsigned long busyDuration = 1000;         
unsigned long globalInteractionDelay = 1000; 

int helmetPresent = 0; int alcLevelA = 0; int alcLevelC = 0; int alcLevelD = 0;

// --- UX VARIABLES ---
int currentCoinTotal = 0; unsigned long cleaningTimeTotal = 0; unsigned long cleaningStartTime = 0;
unsigned long uvLastToggleTime = 0; bool uvIsOn = false; unsigned long blinkTimer = 0; bool blinkState = false; bool humIsOn = false; 

// --- TIMERS ---
bool isPanelUnlocking = false; unsigned long panelUnlockTimer = 0; 
bool isEnclosureUnlocking = false; unsigned long enclUnlockTimer = 0; 
bool isBackdoorUnlocking = false; unsigned long backUnlockTimer = 0; 
unsigned long helmetCheckTimer = 0; 
unsigned long alarmTimer = 0;       
unsigned long refundTimer = 0; 
bool waitingForDoorToOpen = false; 

enum ScreenState { 
  STATE_MAIN_MENU, STATE_DOOR_ALARM, STATE_PAYMENT, STATE_REFUNDING, STATE_VIP, STATE_ADMIN, STATE_PREP, STATE_CLEANING, 
  STATE_PAUSED, STATE_INTERRUPTED, STATE_COMPLETION, STATE_LASTCHECK, STATE_THANKYOU,
  STATE_PAGE1, STATE_PAGE2, STATE_PAGE3, STATE_PAGE4, STATE_PAGE5, STATE_PAGE6, STATE_DISPLAY_TEST, STATE_JOYSTICK_TEST, STATE_RFID_TEST, STATE_COIN_TEST 
};
ScreenState currentState = STATE_MAIN_MENU; bool needsRedraw = true;

int lastPanelState = -1, lastEnclosureState = -1, lastBackdoorState = -1;
enum RFIDRole { GUEST, VIP, MASTER }; RFIDRole currentRFID = GUEST; 
int selectedIndex = 0; unsigned long lastJoyMoveTime = 0; 
int cursorX = 160, cursorY = 120, lastCursorX = -1, lastCursorY = -1;

int btnCoords[6][4] = { {20, 40, 130, 50}, {20, 100, 130, 50}, {20, 160, 130, 50}, {170, 40, 130, 50}, {170, 100, 130, 50}, {170, 160, 130, 50} };

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&receiveData, incomingData, sizeof(receiveData));
  if (strcmp(receiveData.command, "RFID") == 0) {
    if (receiveData.value == 1) { currentRFID = VIP; if(currentState == STATE_MAIN_MENU) { currentState = STATE_VIP; needsRedraw = true; } } 
    else if (receiveData.value == 2) { currentRFID = MASTER; if(currentState == STATE_MAIN_MENU) { currentState = STATE_ADMIN; needsRedraw = true; } } 
    else { currentRFID = GUEST; }
    if (currentState == STATE_RFID_TEST) needsRedraw = true; 
  }
  
  // ---> THE FIX: DO NOT REDRAW PREP OR COMPLETION ON SENSOR UPDATE! <---
  else if (strcmp(receiveData.command, "SENSOR_H") == 0) { 
    helmetPresent = receiveData.value; // Silently update the memory variable
    if (currentState == STATE_PAGE4) needsRedraw = true; // Only redraw if looking at the Debugger screen
  }
  else if (strcmp(receiveData.command, "SENSOR_A") == 0) { alcLevelA = receiveData.value; if (currentState == STATE_PAGE4) needsRedraw = true; }
  else if (strcmp(receiveData.command, "ALC_C") == 0) { alcLevelC = receiveData.value; if (currentState == STATE_PAGE4) needsRedraw = true; }
  else if (strcmp(receiveData.command, "ALC_D") == 0) { alcLevelD = receiveData.value; if (currentState == STATE_PAGE4) needsRedraw = true; }
  else if (strcmp(receiveData.command, "PING_D") == 0) { lastPingTimeD = millis(); isNodeDOnline = true; }
  
  else if (strcmp(receiveData.command, "COIN") == 0) {
    if (currentState == STATE_MAIN_MENU) { currentState = STATE_PAYMENT; needsRedraw = true; }
    if (currentState == STATE_PAYMENT) {
      int incomingCoins = receiveData.value;
      while(incomingCoins > 0) {
        if (currentCoinTotal < 5) { currentCoinTotal++; needsRedraw = true; } 
        else { 
          strcpy(sendData.command, "SERVO_A"); esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData)); 
          currentState = STATE_REFUNDING; needsRedraw = true; 
        } 
        incomingCoins--;
      }
    }
  }
  
  // --- CATCH WEB COMMANDS FROM NODE E ---
  else if (strcmp(receiveData.command, "LOCK_PANEL") == 0) {
    if (receiveData.value == 0) { // 0 = Unlock Triggered
      digitalWrite(PANEL_LOCK_RELAY, LOW); 
      isPanelUnlocking = true; 
      panelUnlockTimer = millis(); 
      triggerSystemLock("BUZZ"); // Make it beep!
    } else {
      digitalWrite(PANEL_LOCK_RELAY, HIGH); 
      isPanelUnlocking = false;
    }
  }
  
  else if (strcmp(receiveData.command, "LOCK_ENC") == 0) {
    if (receiveData.value == 0) { // 0 = Unlock Triggered
      digitalWrite(ENCLOSURE_LOCK_RELAY, LOW); 
      isEnclosureUnlocking = true; 
      enclUnlockTimer = millis(); 
      triggerSystemLock("BUZZ"); // Make it beep!
    } else {
      digitalWrite(ENCLOSURE_LOCK_RELAY, HIGH); 
      isEnclosureUnlocking = false;
    }
  }
  
  else if (strcmp(receiveData.command, "LOCK_BAK") == 0) {
    if (receiveData.value == 0) { // 0 = Unlock Triggered
      digitalWrite(BACKDOOR_LOCK_RELAY, LOW); 
      isBackdoorUnlocking = true; 
      backUnlockTimer = millis(); 
      triggerSystemLock("BUZZ"); // Make it beep!
    } else {
      digitalWrite(BACKDOOR_LOCK_RELAY, HIGH); 
      isBackdoorUnlocking = false;
    }
  }
} // <-- End of OnDataRecv function

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) { isNodeAOnline = (status == ESP_NOW_SEND_SUCCESS); }
void recoverTFT() { delay(50); tft.init(); tft.setRotation(1); uint16_t calData[5] = {256, 3542, 420, 3600, 1}; tft.setTouch(calData); needsRedraw = true; }
void triggerSystemLock(const char* commandToSend, unsigned long customLockTime) { isSystemBusy = true; busyTimer = millis(); busyDuration = (customLockTime > 0) ? customLockTime : globalInteractionDelay; if (strlen(commandToSend) > 0) { strcpy(sendData.command, commandToSend); esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData)); } }
void setup() {
  Serial.begin(115200); tft.init(); tft.setRotation(1); uint16_t calData[5] = {256, 3542, 420, 3600, 1}; tft.setTouch(calData); tft.fillScreen(TFT_BLACK); delay(100); 
  pinMode(SENSOR_PANEL, INPUT_PULLUP); pinMode(SENSOR_ENCLOSURE, INPUT_PULLUP); pinMode(SENSOR_BACKDOOR, INPUT_PULLUP); pinMode(JOYSTICK_SW, INPUT_PULLUP); pinMode(JOYSTICK_VRX, INPUT); pinMode(JOYSTICK_VRY, INPUT);
  pinMode(PANEL_LOCK_RELAY, OUTPUT); digitalWrite(PANEL_LOCK_RELAY, HIGH); pinMode(ENCLOSURE_LOCK_RELAY, OUTPUT); digitalWrite(ENCLOSURE_LOCK_RELAY, HIGH); pinMode(BACKDOOR_LOCK_RELAY, OUTPUT); digitalWrite(BACKDOOR_LOCK_RELAY, HIGH);  
  WiFi.mode(WIFI_STA); esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE); esp_now_init(); esp_now_register_recv_cb(OnDataRecv); esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent); 
  esp_now_peer_info_t peerInfo; memset(&peerInfo, 0, sizeof(peerInfo)); memcpy(peerInfo.peer_addr, nodeE_MAC, 6); peerInfo.channel = 0; peerInfo.encrypt = false; peerInfo.ifidx = WIFI_IF_STA; esp_now_add_peer(&peerInfo);
}

// --- DEBUGGER DRAW FUNCTIONS ---
void drawPage1Layout() { 
  tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); tft.drawString("DEBUGGER - P1", 120, 15, 2); 
  tft.fillRect(250, 5, 60, 25, TFT_RED); tft.drawRect(250, 5, 60, 25, TFT_WHITE); tft.drawString("HOME", 280, 17, 2);
  tft.fillRect(btnCoords[3][0], btnCoords[3][1], 130, 50, TFT_ORANGE); tft.drawString("Servo A", btnCoords[3][0]+65, btnCoords[3][1]+25, 2); 
  tft.fillRect(btnCoords[4][0], btnCoords[4][1], 130, 50, TFT_ORANGE); tft.drawString("Servo B", btnCoords[4][0]+65, btnCoords[4][1]+25, 2); 
  tft.fillRect(btnCoords[5][0], btnCoords[5][1], 130, 50, TFT_CYAN); tft.setTextColor(TFT_BLACK); tft.drawString("More...", btnCoords[5][0]+65, btnCoords[5][1]+25, 2); 
}
void drawPage2Layout() { tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE); tft.drawString("DEBUGGER - P2 (TESTS)", 160, 15, 2); tft.fillRect(btnCoords[0][0], btnCoords[0][1], 130, 50, TFT_BLUE); tft.drawString("BACK", btnCoords[0][0]+65, btnCoords[0][1]+25, 2); tft.fillRect(btnCoords[1][0], btnCoords[1][1], 130, 50, TFT_BLUE); tft.drawString("RFID TEST", btnCoords[1][0]+65, btnCoords[1][1]+25, 2); tft.fillRect(btnCoords[2][0], btnCoords[2][1], 130, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("JOYSTICK", btnCoords[2][0]+65, btnCoords[2][1]+25, 2); tft.fillRect(btnCoords[3][0], btnCoords[3][1], 130, 50, TFT_CYAN); tft.drawString("PAGE 3", btnCoords[3][0]+65, btnCoords[3][1]+25, 2); tft.fillRect(btnCoords[4][0], btnCoords[4][1], 130, 50, TFT_ORANGE); tft.drawString("DISPLAY", btnCoords[4][0]+65, btnCoords[4][1]+25, 2); tft.fillRect(btnCoords[5][0], btnCoords[5][1], 130, 50, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("RESET RFID", btnCoords[5][0]+65, btnCoords[5][1]+25, 2); }
void drawPage3Layout() { tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE); tft.drawString("DEBUGGER - P3 (NODE A)", 160, 15, 2); tft.fillRect(btnCoords[0][0], btnCoords[0][1], 130, 50, TFT_BLUE); tft.drawString("BACK", btnCoords[0][0]+65, btnCoords[0][1]+25, 2); tft.fillRect(btnCoords[1][0], btnCoords[1][1], 130, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("COIN SLOT", btnCoords[1][0]+65, btnCoords[1][1]+25, 2); tft.fillRect(btnCoords[2][0], btnCoords[2][1], 130, 50, TFT_DARKGREY); tft.setTextColor(TFT_WHITE); tft.drawString("EMPTY", btnCoords[2][0]+65, btnCoords[2][1]+25, 2); tft.fillRect(btnCoords[3][0], btnCoords[3][1], 130, 50, TFT_DARKGREY); tft.setTextColor(TFT_WHITE); tft.drawString("EMPTY", btnCoords[3][0]+65, btnCoords[3][1]+25, 2); tft.fillRect(btnCoords[4][0], btnCoords[4][1], 130, 50, humIsOn ? TFT_GREEN : TFT_RED); tft.setTextColor(humIsOn ? TFT_BLACK : TFT_WHITE); tft.drawString(humIsOn ? "HUMID <ON>" : "HUMID <OFF>", btnCoords[4][0]+65, btnCoords[4][1]+25, 2); tft.fillRect(btnCoords[5][0], btnCoords[5][1], 130, 50, TFT_CYAN); tft.setTextColor(TFT_BLACK); tft.drawString("NODE D >", btnCoords[5][0]+65, btnCoords[5][1]+25, 2); }
void drawPage6Layout() { tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE); tft.drawString("DEBUGGER - P6 (NODE D)", 160, 15, 2); tft.fillRect(btnCoords[0][0], btnCoords[0][1], 130, 50, TFT_BLUE); tft.drawString("BACK", btnCoords[0][0]+65, btnCoords[0][1]+25, 2); tft.fillRect(btnCoords[1][0], btnCoords[1][1], 130, 50, TFT_MAGENTA); tft.setTextColor(TFT_WHITE); tft.drawString("PUMP C", btnCoords[1][0]+65, btnCoords[1][1]+25, 2); tft.fillRect(btnCoords[2][0], btnCoords[2][1], 130, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("TOGGLE LED C", btnCoords[2][0]+65, btnCoords[2][1]+25, 2); tft.fillRect(btnCoords[3][0], btnCoords[3][1], 130, 50, TFT_MAGENTA); tft.setTextColor(TFT_WHITE); tft.drawString("PUMP D", btnCoords[3][0]+65, btnCoords[3][1]+25, 2); tft.fillRect(btnCoords[4][0], btnCoords[4][1], 130, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("TOGGLE LED D", btnCoords[4][0]+65, btnCoords[4][1]+25, 2); tft.fillRect(btnCoords[5][0], btnCoords[5][1], 130, 50, TFT_CYAN); tft.setTextColor(TFT_BLACK); tft.drawString("SENSORS >", btnCoords[5][0]+65, btnCoords[5][1]+25, 2); }
void drawPage4Layout() { tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE); tft.drawString("DEBUGGER - SENSORS", 160, 15, 2); tft.fillRect(btnCoords[0][0], btnCoords[0][1], 130, 50, TFT_BLUE); tft.drawString("BACK", btnCoords[0][0]+65, btnCoords[0][1]+25, 2); tft.fillRect(btnCoords[1][0], btnCoords[1][1], 130, 50, helmetPresent ? TFT_GREEN : TFT_DARKGREY); tft.setTextColor(helmetPresent ? TFT_BLACK : TFT_WHITE); tft.drawString(helmetPresent ? "HELMET: YES" : "HELMET: NO", btnCoords[1][0]+65, btnCoords[1][1]+25, 2); uint16_t alcAColor = TFT_DARKGREY; String alcATxt = "ALC A: EMTY"; if (alcLevelA == 1) { alcAColor = TFT_GREEN; alcATxt = "ALC A: LOW"; } else if (alcLevelA == 2) { alcAColor = TFT_YELLOW; alcATxt = "ALC A: MID"; } else if (alcLevelA == 3) { alcAColor = TFT_RED; alcATxt = "ALC A: HIGH"; } tft.fillRect(btnCoords[2][0], btnCoords[2][1], 130, 50, alcAColor); tft.setTextColor(alcLevelA == 3 ? TFT_WHITE : TFT_BLACK); tft.drawString(alcATxt, btnCoords[2][0]+65, btnCoords[2][1]+25, 2); uint16_t alcDColor = TFT_DARKGREY; String alcDTxt = "ALC D: EMTY"; if (alcLevelD == 1) { alcDColor = TFT_GREEN; alcDTxt = "ALC D: LOW"; } else if (alcLevelD == 2) { alcDColor = TFT_YELLOW; alcDTxt = "ALC D: MID"; } else if (alcLevelD == 3) { alcDColor = TFT_RED; alcDTxt = "ALC D: HIGH"; } tft.fillRect(btnCoords[4][0], btnCoords[4][1], 130, 50, alcDColor); tft.setTextColor(alcLevelD == 3 ? TFT_WHITE : TFT_BLACK); tft.drawString(alcDTxt, btnCoords[4][0]+65, btnCoords[4][1]+25, 2); tft.fillRect(btnCoords[3][0], btnCoords[3][1], 130, 50, TFT_CYAN); tft.setTextColor(TFT_BLACK); tft.drawString("SETTINGS >", btnCoords[3][0]+65, btnCoords[3][1]+25, 2);
uint16_t alcCColor = TFT_DARKGREY; String alcCTxt = "ALC C: EMTY"; 
if (alcLevelC == 1) { alcCColor = TFT_GREEN; alcCTxt = "ALC C: LOW"; } 
else if (alcLevelC == 2) { alcCColor = TFT_YELLOW; alcCTxt = "ALC C: MID"; } 
else if (alcLevelC == 3) { alcCColor = TFT_RED; alcCTxt = "ALC C: HIGH"; } 
tft.fillRect(btnCoords[5][0], btnCoords[5][1], 130, 50, alcCColor); 
tft.setTextColor(alcLevelC == 3 ? TFT_WHITE : TFT_BLACK); 
tft.drawString(alcCTxt, btnCoords[5][0]+65, btnCoords[5][1]+25, 2); }
void drawPage5Layout() { tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE); tft.drawString("DEBUGGER - P5 (SETTINGS)", 160, 15, 2); tft.fillRect(btnCoords[0][0], btnCoords[0][1], 130, 50, TFT_BLUE); tft.drawString("BACK", btnCoords[0][0]+65, btnCoords[0][1]+25, 2); tft.fillRect(btnCoords[1][0], btnCoords[1][1], 130, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("DELAY - 1s", btnCoords[1][0]+65, btnCoords[1][1]+25, 2); tft.fillRect(btnCoords[2][0], btnCoords[2][1], 130, 50, TFT_ORANGE); tft.drawString("DELAY + 1s", btnCoords[2][0]+65, btnCoords[2][1]+25, 2); tft.fillRect(btnCoords[3][0], btnCoords[3][1], 130, 50, TFT_DARKGREY); tft.setTextColor(TFT_WHITE); String delayText = "LIMIT: " + String(globalInteractionDelay / 1000) + " SEC"; tft.drawString(delayText, btnCoords[3][0]+65, btnCoords[3][1]+25, 2); tft.fillRect(btnCoords[4][0], btnCoords[4][1], 130, 50, TFT_DARKGREY); tft.drawString("EMPTY", btnCoords[4][0]+65, btnCoords[4][1]+25, 2); tft.fillRect(btnCoords[5][0], btnCoords[5][1], 130, 50, TFT_DARKGREY); tft.drawString("EMPTY", btnCoords[5][0]+65, btnCoords[5][1]+25, 2); }
void updatePage1Sensors() { int currentPanel = digitalRead(SENSOR_PANEL), currentEnclosure = digitalRead(SENSOR_ENCLOSURE), currentBackdoor = digitalRead(SENSOR_BACKDOOR); tft.setTextDatum(MC_DATUM); if (currentPanel != lastPanelState) { tft.fillRect(btnCoords[0][0], btnCoords[0][1], 130, 50, (currentPanel == HIGH) ? TFT_GREEN : TFT_RED); tft.setTextColor((currentPanel == HIGH) ? TFT_BLACK : TFT_WHITE); tft.drawString((currentPanel == HIGH) ? "Panel <Open>" : "Panel <Closed>", btnCoords[0][0]+65, btnCoords[0][1]+25, 2); lastPanelState = currentPanel; sendLogToE("DOOR_PANEL", currentPanel); lastPanelState = currentPanel;} if (currentEnclosure != lastEnclosureState) { tft.fillRect(btnCoords[1][0], btnCoords[1][1], 130, 50, (currentEnclosure == HIGH) ? TFT_GREEN : TFT_RED); tft.setTextColor((currentEnclosure == HIGH) ? TFT_BLACK : TFT_WHITE); tft.drawString((currentEnclosure == HIGH) ? "Encl <Open>" : "Encl <Closed>", btnCoords[1][0]+65, btnCoords[1][1]+25, 2); lastEnclosureState = currentEnclosure; sendLogToE("DOOR_ENC", currentEnclosure); lastEnclosureState = currentEnclosure;} if (currentBackdoor != lastBackdoorState) { tft.fillRect(btnCoords[2][0], btnCoords[2][1], 130, 50, (currentBackdoor == HIGH) ? TFT_GREEN : TFT_RED); tft.setTextColor((currentBackdoor == HIGH) ? TFT_BLACK : TFT_WHITE); tft.drawString((currentBackdoor == HIGH) ? "Backdoor <Open>" : "Backdoor <Closed>", btnCoords[2][0]+65, btnCoords[2][1]+25, 2); lastBackdoorState = currentBackdoor; sendLogToE("DOOR_BACK", currentBackdoor); lastBackdoorState = currentBackdoor;} }
void handleJoystickNavigation() { if (millis() - lastJoyMoveTime < 200) return; int xVal = analogRead(JOYSTICK_VRX), yVal = analogRead(JOYSTICK_VRY), prevIndex = selectedIndex; if (xVal < 1500) { if (selectedIndex < 3) selectedIndex += 3; } else if (xVal > 2500) { if (selectedIndex >= 3) selectedIndex -= 3; } else if (yVal > 2500) { if (selectedIndex % 3 != 0) selectedIndex -= 1; } else if (yVal < 1500) { if (selectedIndex % 3 != 2) selectedIndex += 1; } if (selectedIndex != prevIndex) { tft.drawRect(btnCoords[prevIndex][0], btnCoords[prevIndex][1], 130, 50, TFT_BLACK); tft.drawRect(btnCoords[prevIndex][0]-1, btnCoords[prevIndex][1]-1, 132, 52, TFT_BLACK); tft.drawRect(btnCoords[selectedIndex][0], btnCoords[selectedIndex][1], 130, 50, TFT_WHITE); tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); lastJoyMoveTime = millis(); sendLogToE("JOYSTICK", selectedIndex); lastJoyMoveTime = millis();} }

void loop() {
  if (millis() - lastPingTimeA > 2000) { strcpy(sendData.command, "PING"); esp_now_send(nodeE_MAC, (uint8_t *) &sendData, sizeof(sendData)); lastPingTimeA = millis(); }
  if (isSystemBusy) { if (millis() - busyTimer >= busyDuration) { isSystemBusy = false; needsRedraw = true; } else { return; } }

  uint16_t t_x = 0, t_y = 0; bool touched = tft.getTouch(&t_x, &t_y); 
  bool joyPressed = (digitalRead(JOYSTICK_SW) == LOW);

  // --- SOLENOID 2-SECOND SAFETY SHUTOFF ---
  unsigned long currentMillis = millis();
  if (isPanelUnlocking && currentMillis - panelUnlockTimer >= 2000) { 
    digitalWrite(PANEL_LOCK_RELAY, HIGH); isPanelUnlocking = false; sendLogToE("LOCK_PANEL", 1); 
  }
  if (isEnclosureUnlocking && currentMillis - enclUnlockTimer >= 2000) { 
    digitalWrite(ENCLOSURE_LOCK_RELAY, HIGH); isEnclosureUnlocking = false; sendLogToE("LOCK_ENC", 1); 
  }
  if (isBackdoorUnlocking && currentMillis - backUnlockTimer >= 2000) { 
    digitalWrite(BACKDOOR_LOCK_RELAY, HIGH); isBackdoorUnlocking = false; sendLogToE("LOCK_BAK", 1); 
  }

  switch (currentState) {
    
    // ==========================================
    // THE USER EXPERIENCE (UX) STATE MACHINE
    // ==========================================
    
    case STATE_MAIN_MENU:
      if (digitalRead(SENSOR_ENCLOSURE) == HIGH) { 
        currentState = STATE_DOOR_ALARM; needsRedraw = true; 
        break; 
      }

      if (needsRedraw) {
        if (humIsOn) { triggerSystemLock("HUM_OFF"); humIsOn = false; }
        if (uvIsOn) { triggerSystemLock("UV_OFF"); uvIsOn = false; }
        tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("JOLLY HELMET", 160, 60, 4); tft.setTextColor(TFT_GREEN); tft.drawString("Make your HELMET happy again!", 160, 90, 2);
        needsRedraw = false; 
      }
      if (millis() - blinkTimer > 500) {
        blinkTimer = millis(); blinkState = !blinkState;
        if (blinkState) { tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString(">>> INSERT COIN <<<", 160, 160, 4); }
        else { tft.setTextColor(TFT_BLACK, TFT_BLACK); tft.drawString(">>> INSERT COIN <<<", 160, 160, 4); }
      }
      break;

    case STATE_DOOR_ALARM:
      if (needsRedraw) {
        tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM);
        tft.drawString("Kindly Close the Door", 160, 120, 4);
        needsRedraw = false; alarmTimer = millis(); triggerSystemLock("BUZZ"); 
      }
      if (millis() - alarmTimer >= 2000) { triggerSystemLock("BUZZ"); alarmTimer = millis(); }
      if (digitalRead(SENSOR_ENCLOSURE) == LOW) { currentState = STATE_MAIN_MENU; needsRedraw = true; }
      break;

    case STATE_VIP:
      if (needsRedraw) {
        tft.fillScreen(TFT_YELLOW); tft.setTextColor(TFT_BLACK); tft.setTextDatum(MC_DATUM); 
        tft.drawString("Welcome VIP", 160, 80, 4);
        tft.fillRect(20, 150, 120, 50, TFT_GREEN); tft.drawString("CLEAN", 80, 175, 2);
        tft.fillRect(180, 150, 120, 50, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("CANCEL", 240, 175, 2);
        needsRedraw = false; delay(200);
      }
      if (touched && t_y >= 150 && t_y <= 200) {
        if (t_x >= 20 && t_x <= 140) { cleaningTimeTotal = 60000; currentState = STATE_PREP; triggerSystemLock("BUZZ"); } 
        if (t_x >= 180 && t_x <= 300) { currentCoinTotal = 0; currentState = STATE_MAIN_MENU; triggerSystemLock("BUZZ"); }
      }
      break;

    case STATE_ADMIN:
      if (needsRedraw) {
        tft.fillScreen(TFT_BLUE); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("Welcome ADMIN", 160, 80, 4);
        tft.fillRect(20, 150, 120, 50, TFT_GREEN); tft.setTextColor(TFT_BLACK); tft.drawString("CLEAN", 80, 175, 2);
        tft.fillRect(180, 150, 120, 50, TFT_ORANGE); tft.drawString("DEBUG MODE", 240, 175, 2);
        needsRedraw = false; delay(200);
      }
      if (touched && t_y >= 150 && t_y <= 200) {
        if (t_x >= 20 && t_x <= 140) { cleaningTimeTotal = 60000; currentState = STATE_PREP; triggerSystemLock("BUZZ"); } 
        if (t_x >= 180 && t_x <= 300) { currentState = STATE_PAGE1; triggerSystemLock("BUZZ"); } 
      }
      break;

    case STATE_PAYMENT:
      if (needsRedraw) {
        tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("COINS INSERTED", 160, 40, 4);
        tft.setTextColor(TFT_GREEN); tft.drawString("P " + String(currentCoinTotal) + ".00", 160, 90, 6);
        tft.setTextColor(TFT_YELLOW); tft.drawString("Time: " + String(currentCoinTotal * 12) + " Seconds", 160, 130, 2);

        tft.fillRect(20, 170, 120, 50, TFT_GREEN); tft.setTextColor(TFT_BLACK); tft.drawString("CONFIRM", 80, 195, 2);
        tft.fillRect(180, 170, 120, 50, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("REFUND", 240, 195, 2);
        needsRedraw = false; delay(200);
      }
      if (touched && t_y >= 170 && t_y <= 220) {
        if (t_x >= 20 && t_x <= 140) { cleaningTimeTotal = currentCoinTotal * 12000; triggerSystemLock("SERVO_B"); currentState = STATE_PREP; }
        if (t_x >= 180 && t_x <= 300) { 
          triggerSystemLock("SERVO_A"); 
          currentState = STATE_REFUNDING; needsRedraw = true;
        }
      }
      break;

    case STATE_REFUNDING:
      if (needsRedraw) {
        tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM);
        tft.drawString("REFUNDING COINS...", 160, 120, 4);
        needsRedraw = false; refundTimer = millis(); 
      }
      if (millis() - refundTimer >= 2000) { currentCoinTotal = 0; currentState = STATE_MAIN_MENU; needsRedraw = true; }
      break;

    case STATE_PREP:
      if (needsRedraw) {
        tft.fillScreen(TFT_BLUE); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("Place Your Helmet", 160, 60, 4); tft.drawString("and close the door.", 160, 100, 2);
        tft.fillRect(40, 150, 240, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("TAP TO UNLOCK DOOR", 160, 175, 2);

        // POP DOOR ONCE
        digitalWrite(ENCLOSURE_LOCK_RELAY, LOW); isEnclosureUnlocking = true; enclUnlockTimer = millis(); 
        
        needsRedraw = false; helmetCheckTimer = 0; waitingForDoorToOpen = true; 
      }
      
      // Allow user to manually pop the lock if they missed the 2-second window
      if (waitingForDoorToOpen && touched && t_y > 130) {
        digitalWrite(ENCLOSURE_LOCK_RELAY, LOW); isEnclosureUnlocking = true; enclUnlockTimer = millis(); triggerSystemLock("BUZZ");
      }

      // If the 2-second lock timer finishes, and the user NEVER opened the door, we are still stuck waiting.
      // But if the user physically pulls the door open:
      if (digitalRead(SENSOR_ENCLOSURE) == HIGH) { 
        waitingForDoorToOpen = false; helmetCheckTimer = 0; 
      } 
      // User has opened the door, placed the helmet, and closed it again (Now we process!)
      else if (!waitingForDoorToOpen && !isEnclosureUnlocking && digitalRead(SENSOR_ENCLOSURE) == LOW) { 
        if (helmetCheckTimer == 0) helmetCheckTimer = millis(); 

        if (helmetPresent == 1) {
          digitalWrite(ENCLOSURE_LOCK_RELAY, HIGH); 
          cleaningStartTime = millis(); uvLastToggleTime = millis(); uvIsOn = false; currentState = STATE_CLEANING; 
          triggerSystemLock("HUM_ON"); humIsOn = true;
        } 
        else if (millis() - helmetCheckTimer >= 3000) { // Gives sensor 3 seconds to find helmet
          tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("NO HELMET DETECTED!", 160, 120, 4);
          triggerSystemLock("BUZZ"); delay(2000); 
          needsRedraw = true; // Loops back to top of PREP, popping the door again
        }
      }
      break;

    case STATE_CLEANING: { 
      unsigned long elapsed = millis() - cleaningStartTime;
      long timeLeftSec = (cleaningTimeTotal - elapsed) / 1000;

      if (digitalRead(SENSOR_ENCLOSURE) == HIGH) { 
        triggerSystemLock("HUM_OFF"); triggerSystemLock("UV_OFF"); uvIsOn = false; humIsOn = false;
        cleaningTimeTotal = cleaningTimeTotal - elapsed; 
        currentState = STATE_PAUSED; needsRedraw = true;
        break; 
      }

      if (timeLeftSec <= 0) {
        triggerSystemLock("HUM_OFF"); triggerSystemLock("UV_OFF"); uvIsOn = false; humIsOn = false;
        tft.fillScreen(TFT_GREEN); tft.setTextColor(TFT_BLACK); tft.drawString("SANITATION COMPLETE", 160, 120, 4);
        delay(2000); currentCoinTotal = 0; currentState = STATE_COMPLETION; needsRedraw = true;
      } else {
        if (uvIsOn && (millis() - uvLastToggleTime > 10000)) { triggerSystemLock("UV_OFF", 50); uvIsOn = false; uvLastToggleTime = millis(); }
        else if (!uvIsOn && (millis() - uvLastToggleTime > 20000)) { triggerSystemLock("UV_ON", 50); uvIsOn = true; uvLastToggleTime = millis(); }

        if (millis() - blinkTimer > 1000) { 
          blinkTimer = millis();
          tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
          tft.drawString("CLEANING IN PROGRESS", 160, 40, 4);
          tft.setTextColor(TFT_GREEN); tft.drawString(String(timeLeftSec) + " Sec", 160, 100, 6);
          tft.fillRect(100, 170, 120, 50, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("CANCEL", 160, 195, 2);
        }
        if (touched && t_x >= 100 && t_x <= 220 && t_y >= 170 && t_y <= 220) {
          triggerSystemLock("HUM_OFF"); triggerSystemLock("UV_OFF"); uvIsOn = false; humIsOn = false;
          currentState = STATE_INTERRUPTED; needsRedraw = true;
        }
      }
      break;
    } 

    case STATE_PAUSED:
      if (digitalRead(SENSOR_ENCLOSURE) == LOW) {
        if (helmetPresent == 1) { 
          cleaningStartTime = millis(); uvLastToggleTime = millis(); uvIsOn = false; 
          currentState = STATE_CLEANING; triggerSystemLock("HUM_ON"); humIsOn = true;
        } else {
          tft.fillRect(0, 130, 320, 30, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("No helmet inside!", 160, 145, 2);
          delay(1500); needsRedraw = true;
        }
        break; 
      }

      if (needsRedraw) {
        tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("DOOR OPENED!", 160, 60, 4); tft.drawString("Cleaning Paused.", 160, 100, 2);
        tft.fillRect(20, 170, 120, 50, TFT_GREEN); tft.setTextColor(TFT_BLACK); tft.drawString("CONTINUE", 80, 195, 2);
        tft.fillRect(180, 170, 120, 50, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("CANCEL", 240, 195, 2);
        needsRedraw = false; delay(200);
      }
      if (touched && t_y >= 170 && t_y <= 220) {
        if (t_x >= 20 && t_x <= 140) { 
           tft.fillRect(0, 130, 320, 30, TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("Please close door first!", 160, 145, 2);
           delay(1500); needsRedraw = true;
        }
        if (t_x >= 180 && t_x <= 300) { currentState = STATE_INTERRUPTED; needsRedraw = true; } 
      }
      break;

    case STATE_INTERRUPTED:
      if (needsRedraw) {
        tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("SANITATION CANCELLED", 160, 120, 4);
        triggerSystemLock("BUZZ"); delay(2000);
        currentCoinTotal = 0; currentState = STATE_COMPLETION; needsRedraw = true;
      }
      break;

    case STATE_COMPLETION:
      if (needsRedraw) {
        tft.fillScreen(TFT_BLUE); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); 
        tft.drawString("Get your HELMET", 160, 60, 4);
        tft.fillRect(40, 150, 240, 50, TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.drawString("TAP TO UNLOCK DOOR", 160, 175, 2);

        digitalWrite(ENCLOSURE_LOCK_RELAY, LOW); isEnclosureUnlocking = true; enclUnlockTimer = millis(); 
        needsRedraw = false; waitingForDoorToOpen = true;
      }

      if (waitingForDoorToOpen && touched && t_y > 130) {
        digitalWrite(ENCLOSURE_LOCK_RELAY, LOW); isEnclosureUnlocking = true; enclUnlockTimer = millis(); triggerSystemLock("BUZZ");
      }

      if (digitalRead(SENSOR_ENCLOSURE) == HIGH) { waitingForDoorToOpen = false; }

      if (!waitingForDoorToOpen && !isEnclosureUnlocking) {
        if (helmetPresent == 0) { currentState = STATE_LASTCHECK; needsRedraw = true; }
        else if (helmetPresent == 1 && digitalRead(SENSOR_ENCLOSURE) == LOW) {
           tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString("Helmet is still inside!", 160, 120, 4);
           triggerSystemLock("BUZZ"); delay(2000); needsRedraw = true; 
        }
      }
      break;

    case STATE_LASTCHECK:
      if (needsRedraw) {
        tft.fillScreen(TFT_ORANGE); tft.setTextColor(TFT_BLACK); tft.setTextDatum(MC_DATUM); 
        tft.drawString("Kindly Close the Door", 160, 120, 4);
        needsRedraw = false; delay(500);
      }
      if (helmetPresent == 1) { currentState = STATE_COMPLETION; needsRedraw = true; } 
      else if (digitalRead(SENSOR_ENCLOSURE) == LOW) { 
        digitalWrite(ENCLOSURE_LOCK_RELAY, HIGH); currentState = STATE_THANKYOU; needsRedraw = true; 
      }
      break;

    case STATE_THANKYOU:
      if (needsRedraw) {
        tft.fillScreen(TFT_GREEN); tft.setTextColor(TFT_BLACK); tft.setTextDatum(MC_DATUM); 
        tft.drawString("Thank You!", 160, 80, 4); tft.drawString("Jolly Helmet", 160, 120, 2);
        if (alcLevelA == 1) { triggerSystemLock("PUMP_D"); } 
        delay(2000); currentState = STATE_MAIN_MENU; needsRedraw = true;
      }
      break;

    // ==========================================
    // THE DEBUGGER STATE MACHINE
    // ==========================================
    
    case STATE_PAGE1:
      if (needsRedraw) { drawPage1Layout(); lastPanelState = -1; lastEnclosureState = -1; lastBackdoorState = -1; tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); needsRedraw = false; delay(200); }
      updatePage1Sensors(); handleJoystickNavigation();
      if (joyPressed || touched) {
        if (touched && t_x >= 250 && t_x <= 310 && t_y >= 5 && t_y <= 30) { currentState = STATE_MAIN_MENU; triggerSystemLock("BUZZ"); break; }
        
        int actIdx = -1; if (joyPressed) actIdx = selectedIndex; else { for(int i=0; i<6; i++) { if (t_x >= btnCoords[i][0] && t_x <= btnCoords[i][0]+130 && t_y >= btnCoords[i][1] && t_y <= btnCoords[i][1]+50) { actIdx = i; break; } } }
        if (actIdx == 0) { tft.drawRect(btnCoords[0][0]-1, btnCoords[0][1]-1, 132, 52, TFT_YELLOW); digitalWrite(PANEL_LOCK_RELAY, LOW); isPanelUnlocking = true; panelUnlockTimer = millis(); triggerSystemLock("BUZZ"); }
        else if (actIdx == 1) { tft.drawRect(btnCoords[1][0]-1, btnCoords[1][1]-1, 132, 52, TFT_YELLOW); digitalWrite(ENCLOSURE_LOCK_RELAY, LOW); isEnclosureUnlocking = true; enclUnlockTimer = millis(); triggerSystemLock("BUZZ");}
        else if (actIdx == 2) { tft.drawRect(btnCoords[2][0]-1, btnCoords[2][1]-1, 132, 52, TFT_YELLOW); digitalWrite(BACKDOOR_LOCK_RELAY, LOW); isBackdoorUnlocking = true; backUnlockTimer = millis(); triggerSystemLock("BUZZ");}
        else if (actIdx == 3) { tft.drawRect(btnCoords[3][0]-1, btnCoords[3][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("SERVO_A"); }
        else if (actIdx == 4) { tft.drawRect(btnCoords[4][0]-1, btnCoords[4][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("SERVO_B"); }
        else if (actIdx == 5) { tft.drawRect(btnCoords[5][0]-1, btnCoords[5][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE2; triggerSystemLock("BUZZ"); }
      }
      break;

    case STATE_PAGE2:
      if (needsRedraw) { drawPage2Layout(); tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); needsRedraw = false; delay(200); }
      handleJoystickNavigation();
      if (joyPressed || touched) {
        int actIdx = -1; if (joyPressed) actIdx = selectedIndex; else { for(int i=0; i<6; i++) { if (t_x >= btnCoords[i][0] && t_x <= btnCoords[i][0]+130 && t_y >= btnCoords[i][1] && t_y <= btnCoords[i][1]+50) { actIdx = i; break; } } }
        if (actIdx == 0) { tft.drawRect(btnCoords[0][0]-1, btnCoords[0][1]-1, 132, 52, TFT_YELLOW); currentState = STATE_MAIN_MENU; triggerSystemLock("BUZZ"); } 
        else if (actIdx == 1) { tft.drawRect(btnCoords[1][0]-1, btnCoords[1][1]-1, 132, 52, TFT_YELLOW); currentRFID = GUEST; currentState = STATE_RFID_TEST; triggerSystemLock("BUZZ"); }
        else if (actIdx == 2) { tft.drawRect(btnCoords[2][0]-1, btnCoords[2][1]-1, 132, 52, TFT_YELLOW); cursorX = 160; cursorY = 120; currentState = STATE_JOYSTICK_TEST; triggerSystemLock("BUZZ"); }
        else if (actIdx == 3) { tft.drawRect(btnCoords[3][0]-1, btnCoords[3][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE3; triggerSystemLock("BUZZ"); } 
        else if (actIdx == 4) { tft.drawRect(btnCoords[4][0]-1, btnCoords[4][1]-1, 132, 52, TFT_YELLOW); currentState = STATE_DISPLAY_TEST; triggerSystemLock("BUZZ"); }
        else if (actIdx == 5) { tft.drawRect(btnCoords[5][0]-1, btnCoords[5][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("RESET_RFID"); }
      }
      break;

    case STATE_PAGE3: 
      if (needsRedraw) { drawPage3Layout(); tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); needsRedraw = false; delay(200); }
      handleJoystickNavigation();
      if (joyPressed || touched) {
        int actIdx = -1; if (joyPressed) actIdx = selectedIndex; else { for(int i=0; i<6; i++) { if (t_x >= btnCoords[i][0] && t_x <= btnCoords[i][0]+130 && t_y >= btnCoords[i][1] && t_y <= btnCoords[i][1]+50) { actIdx = i; break; } } }
        if (actIdx == 0) { tft.drawRect(btnCoords[0][0]-1, btnCoords[0][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE2; triggerSystemLock("BUZZ"); }
        else if (actIdx == 1) { tft.drawRect(btnCoords[1][0]-1, btnCoords[1][1]-1, 132, 52, TFT_YELLOW); currentState = STATE_COIN_TEST; triggerSystemLock("BUZZ"); } 
        else if (actIdx == 2) { tft.drawRect(btnCoords[2][0]-1, btnCoords[2][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("BUZZ"); }
        else if (actIdx == 3) { tft.drawRect(btnCoords[3][0]-1, btnCoords[3][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("BUZZ"); }
        else if (actIdx == 4) { tft.drawRect(btnCoords[4][0]-1, btnCoords[4][1]-1, 132, 52, TFT_YELLOW); humIsOn = !humIsOn; if (humIsOn) triggerSystemLock("HUM_ON"); else triggerSystemLock("HUM_OFF"); }
        else if (actIdx == 5) { tft.drawRect(btnCoords[5][0]-1, btnCoords[5][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE6; triggerSystemLock("BUZZ"); }
      }
      break;

    case STATE_PAGE6: 
      if (needsRedraw) { drawPage6Layout(); tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); needsRedraw = false; delay(200); }
      handleJoystickNavigation();
      if (joyPressed || touched) {
        int actIdx = -1; if (joyPressed) actIdx = selectedIndex; else { for(int i=0; i<6; i++) { if (t_x >= btnCoords[i][0] && t_x <= btnCoords[i][0]+130 && t_y >= btnCoords[i][1] && t_y <= btnCoords[i][1]+50) { actIdx = i; break; } } }
        if (actIdx == 0) { tft.drawRect(btnCoords[0][0]-1, btnCoords[0][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE3; triggerSystemLock("BUZZ"); }
        else if (actIdx == 1) { tft.drawRect(btnCoords[1][0]-1, btnCoords[1][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("PUMP_C", 5000); }
        else if (actIdx == 2) { tft.drawRect(btnCoords[2][0]-1, btnCoords[2][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("LED_C"); } 
        else if (actIdx == 3) { tft.drawRect(btnCoords[3][0]-1, btnCoords[3][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("PUMP_D", 5000); } 
        else if (actIdx == 4) { tft.drawRect(btnCoords[4][0]-1, btnCoords[4][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("LED_D"); }
        else if (actIdx == 5) { tft.drawRect(btnCoords[5][0]-1, btnCoords[5][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE4; triggerSystemLock("BUZZ"); }
      }
      break;

    case STATE_PAGE4: 
      if (needsRedraw) { drawPage4Layout(); tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); needsRedraw = false; delay(50); }
      handleJoystickNavigation();
      if (joyPressed || touched) {
        int actIdx = -1; if (joyPressed) actIdx = selectedIndex; else { for(int i=0; i<6; i++) { if (t_x >= btnCoords[i][0] && t_x <= btnCoords[i][0]+130 && t_y >= btnCoords[i][1] && t_y <= btnCoords[i][1]+50) { actIdx = i; break; } } }
        if (actIdx == 0) { tft.drawRect(btnCoords[0][0]-1, btnCoords[0][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE6; triggerSystemLock("BUZZ"); }
        else if (actIdx == 3) { tft.drawRect(btnCoords[3][0]-1, btnCoords[3][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE5; triggerSystemLock("BUZZ"); } 
        else if (actIdx != -1) { tft.drawRect(btnCoords[actIdx][0]-1, btnCoords[actIdx][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("BUZZ"); } 
      }
      break;

    case STATE_PAGE5: 
      if (needsRedraw) { drawPage5Layout(); tft.drawRect(btnCoords[selectedIndex][0]-1, btnCoords[selectedIndex][1]-1, 132, 52, TFT_WHITE); needsRedraw = false; delay(200); }
      handleJoystickNavigation();
      if (joyPressed || touched) {
        int actIdx = -1; if (joyPressed) actIdx = selectedIndex; else { for(int i=0; i<6; i++) { if (t_x >= btnCoords[i][0] && t_x <= btnCoords[i][0]+130 && t_y >= btnCoords[i][1] && t_y <= btnCoords[i][1]+50) { actIdx = i; break; } } }
        if (actIdx == 0) { tft.drawRect(btnCoords[0][0]-1, btnCoords[0][1]-1, 132, 52, TFT_YELLOW); selectedIndex = 0; currentState = STATE_PAGE4; triggerSystemLock("BUZZ"); }
        else if (actIdx == 1) { tft.drawRect(btnCoords[1][0]-1, btnCoords[1][1]-1, 132, 52, TFT_YELLOW); if (globalInteractionDelay > 1000) { globalInteractionDelay -= 1000; needsRedraw = true; } triggerSystemLock("BUZZ"); }
        else if (actIdx == 2) { tft.drawRect(btnCoords[2][0]-1, btnCoords[2][1]-1, 132, 52, TFT_YELLOW); if (globalInteractionDelay < 5000) { globalInteractionDelay += 1000; needsRedraw = true; } triggerSystemLock("BUZZ"); }
        else if (actIdx != -1) { tft.drawRect(btnCoords[actIdx][0]-1, btnCoords[actIdx][1]-1, 132, 52, TFT_YELLOW); triggerSystemLock("BUZZ"); } 
      }
      break;

    case STATE_RFID_TEST:
      if (needsRedraw) {
        if (currentRFID == VIP) { tft.fillScreen(TFT_YELLOW); tft.setTextColor(TFT_BLACK, TFT_YELLOW); tft.drawString("VIP", 160, 160, 4); } else if (currentRFID == MASTER) { tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString("MASTER", 160, 160, 4); } else { tft.fillScreen(TFT_BLUE); tft.setTextColor(TFT_WHITE, TFT_BLUE); }
        tft.drawString("Insert a Card to check", 160, 100, 4); tft.fillRect(10, 10, 80, 40, TFT_RED); tft.drawRect(10, 10, 80, 40, TFT_WHITE); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); tft.drawString("BACK", 50, 30, 2); needsRedraw = false; delay(200);
      }
      if (joyPressed || (touched && t_x >= 10 && t_x <= 90 && t_y >= 10 && t_y <= 50)) { currentState = STATE_PAGE2; triggerSystemLock("BUZZ"); }
      break;

    case STATE_COIN_TEST:
      if (needsRedraw) {
        tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_ORANGE); tft.setTextDatum(MC_DATUM); tft.drawString("DROP A COIN", 160, 60, 4); tft.setTextColor(TFT_GREEN); tft.drawString("P " + String(currentCoinTotal) + ".00", 160, 130, 6); 
        tft.fillRect(10, 10, 80, 40, TFT_RED); tft.drawRect(10, 10, 80, 40, TFT_WHITE); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); tft.drawString("BACK", 50, 30, 2);
        tft.fillRect(230, 10, 80, 40, TFT_BLUE); tft.drawRect(230, 10, 80, 40, TFT_WHITE); tft.setTextColor(TFT_WHITE); tft.drawString("RESET", 270, 30, 2); needsRedraw = false; delay(200);
      }
      if (joyPressed || (touched && t_x >= 10 && t_x <= 90 && t_y >= 10 && t_y <= 50)) { currentState = STATE_PAGE3; triggerSystemLock("BUZZ"); }
      else if (touched && t_x >= 230 && t_x <= 310 && t_y >= 10 && t_y <= 50) { currentCoinTotal = 0; needsRedraw = true; triggerSystemLock("BUZZ"); }
      break;

    case STATE_DISPLAY_TEST:
      if (needsRedraw) { int third = tft.width() / 3; tft.fillRect(0, 0, third, tft.height(), TFT_RED); tft.fillRect(third, 0, third, tft.height(), TFT_GREEN); tft.fillRect(third * 2, 0, third, tft.height(), TFT_BLUE); tft.fillRect(10, 10, 80, 40, TFT_BLACK); tft.drawRect(10, 10, 80, 40, TFT_WHITE); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); tft.drawString("BACK", 50, 30, 2); needsRedraw = false; delay(200); }
      if (joyPressed || (touched && t_x >= 10 && t_x <= 90 && t_y >= 10 && t_y <= 50)) { currentState = STATE_PAGE2; triggerSystemLock("BUZZ"); }
      break;

    case STATE_JOYSTICK_TEST:
      if (needsRedraw) { tft.fillScreen(TFT_BLACK); tft.fillRect(10, 10, 80, 40, TFT_RED); tft.setTextColor(TFT_WHITE); tft.setTextDatum(MC_DATUM); tft.drawString("BACK", 50, 30, 2); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("Move Stick to test. Press SW on Red to Exit", 160, 220, 2); needsRedraw = false; }
      int vx = analogRead(JOYSTICK_VRX), vy = analogRead(JOYSTICK_VRY);
      if (vx < 1500) cursorX += 4; if (vx > 2500) cursorX -= 4; if (vy < 1500) cursorY += 4; if (vy > 2500) cursorY -= 4; cursorX = constrain(cursorX, 4, 316); cursorY = constrain(cursorY, 4, 236);
      if (cursorX != lastCursorX || cursorY != lastCursorY) { if (lastCursorX != -1) { tft.fillCircle(lastCursorX, lastCursorY, 5, TFT_BLACK); tft.fillRect(100, 10, 150, 30, TFT_BLACK); } tft.setTextColor(TFT_GREEN, TFT_BLACK); String coordStr = "X: " + String(cursorX) + "  Y: " + String(cursorY); tft.drawString(coordStr, 160, 25, 2); tft.fillCircle(cursorX, cursorY, 5, TFT_MAGENTA); lastCursorX = cursorX; lastCursorY = cursorY; delay(15); }
      if (joyPressed && cursorX >= 10 && cursorX <= 90 && cursorY >= 10 && cursorY <= 50) { currentState = STATE_PAGE2; triggerSystemLock("BUZZ"); } else if (touched && t_x >= 10 && t_x <= 90 && t_y >= 10 && t_y <= 50) { currentState = STATE_PAGE2; triggerSystemLock("BUZZ"); }
      break;
  }
}