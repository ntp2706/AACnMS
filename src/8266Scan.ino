#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

#define SSID "NTP"
#define PASSWORD "qwert123"

#define SS_PIN 0 // D3
#define RST_PIN 2 // D4
#define BUZZER 4 // D2

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;


IPAddress localIP;
char serverIP[16];
char esp32camIP[16];

// lấy IP tự động thay thế octet 4
void createModifiedIP(IPAddress ip, uint8_t newLastOctet, char* result) {
  snprintf(result, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], newLastOctet);
}

WebSocketsServer webSocket = WebSocketsServer(81);
uint8_t clientID;

bool addMode = false;
bool readMode = true;
bool deleteMode = false;
bool deleteFirstScan = false;

unsigned long buzzerStartTime = 0;
unsigned long buzzerTime = 0;
bool buzzerActive = false;

unsigned long restartStartTime = 0;
unsigned long restartTime = 0;
bool restartActive = false;

int blockNum; 
byte blockData[16];
byte bufferLen = 18;
byte readBlockData[18];

String timestampReceive;

String nameSend;
String dobSend;
String roomSend;
String timestampSend;

void writeStringToBlocks(int startBlock, String data);
String readStringFromBlocks(int startBlock, int blockCount);
void writeStringToBlock(int block, String data);
String readStringFromBlock(int block);
void writeDataToBlock(int blockNum, byte blockData[]);
void readDataFromBlock(int blockNum, byte readBlockData[]);
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  String message = String((char *)data).substring(0, len);
  if (message.startsWith("Database:")) {
    timestampReceive = message.substring(9);

    addMode = true;
    readMode = false;
    deleteMode = false;

    String message = "ReadyToScan";
    webSocket.broadcastTXT(message);
  } 

  else if (message.startsWith("InvalidInformation")) {
    buzzerStartTime = millis();
    buzzerTime = 3000;
    buzzerActive = true;
    digitalWrite(BUZZER, HIGH);
  }

  else if (message.startsWith("DeleteUser")) {
    addMode = false;
    readMode = false;
    deleteMode = true;
    deleteFirstScan = true;

    String message = "ReadyToDelete";
    webSocket.broadcastTXT(message);
  } 

  else if (message.startsWith("RestartSystem")) {
    String message = "RestartSystem";
    webSocket.broadcastTXT(message);

    restartStartTime = millis();
    restartTime = 1000;
    restartActive = true;
  } 
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER, OUTPUT);

  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  localIP = WiFi.localIP();

  createModifiedIP(localIP, 50, serverIP);

  createModifiedIP(localIP, 43, esp32camIP);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  SPI.begin();            
  mfrc522.PCD_Init();     
  delay(4);               
  mfrc522.PCD_DumpVersionToSerial();  

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

void loop() {
  webSocket.loop();

  if (buzzerActive && (millis() - buzzerStartTime >= buzzerTime)) {
    digitalWrite(BUZZER, LOW);
    buzzerActive = false;
  }

  if (restartActive && (millis() - restartStartTime >= restartTime)) {
    ESP.restart();
    restartActive = false;
  }

  if ((!mfrc522.PICC_IsNewCardPresent())||(!mfrc522.PICC_ReadCardSerial())) {
    return;
  }

  if (readMode) {
    timestampSend = readStringFromBlocks(16, 3);

    buzzerStartTime = millis();
    buzzerActive = true;
    digitalWrite(BUZZER, HIGH);
    buzzerTime = 200;
    
    String message = "Recall:$$$" + timestampSend;
    webSocket.broadcastTXT(message);
  }
  
  if (addMode) {
    buzzerStartTime = millis();
    buzzerTime = 200;
    buzzerActive = true;
    digitalWrite(BUZZER, HIGH);

    writeStringToBlocks(16, timestampReceive);
      
    String message = "ScanDone";
    webSocket.broadcastTXT(message);

    addMode = false;
    readMode = true;
    deleteMode = false;
  }

  if (deleteMode) {
    buzzerStartTime = millis();
    buzzerTime = 200;
    buzzerActive = true;
    digitalWrite(BUZZER, HIGH);

    if (!deleteFirstScan) {
      writeStringToBlocks(16, "");

      String message = "DeleteDone";
      webSocket.broadcastTXT(message);

      addMode = false;
      readMode = true;
      deleteMode = false;
    } else {
        timestampSend = readStringFromBlocks(16, 3);

        String message = "DeleteFirstScanDone:" + timestampSend;
        webSocket.broadcastTXT(message);
        deleteFirstScan = false;
      }
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void writeDataToBlock(int blockNum, byte blockData[]) {
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    return;
  }
  status = mfrc522.MIFARE_Write(blockNum, blockData, 16);
  if (status != MFRC522::STATUS_OK) {
    return;
  }
}

void writeStringToBlocks(int startBlock, String data) {
  int len = data.length();
  byte buffer[16];
  for (int i = 0; i < 3; i++) {
    int offset = i * 16;
    for (int j = 0; j < 16; j++) {
      if (offset + j < len) {
        buffer[j] = data[offset + j];
      } else {
          buffer[j] = 0;
        }
    }
    writeDataToBlock(startBlock + i, buffer);
  }
}

void readDataFromBlock(int blockNum, byte readBlockData[]) {
  byte status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    return;
  }
  status = mfrc522.MIFARE_Read(blockNum, readBlockData, &bufferLen);
  if (status != MFRC522::STATUS_OK) {
    return;
  }
}

String readStringFromBlocks(int startBlock, int blockCount) {
  String result = "";
  byte buffer[18];
  for (int i = 0; i < blockCount; i++) {
    readDataFromBlock(startBlock + i, buffer);
    for (int j = 0; j < 16; j++) {
      if (buffer[j] != 0) {
        result += (char)buffer[j];
      }
    }
  }
  return result;
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_TEXT:
      handleWebSocketMessage(NULL, payload, length);
      break;
    case WStype_DISCONNECTED:
      break;
    case WStype_CONNECTED: {
      clientID = num;
      IPAddress ip = webSocket.remoteIP(clientID);
      char ipStr[16];
      sprintf(ipStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

      if (strcmp(ipStr, esp32camIP) == 0) {
        String message = "ESP32 CAM: OK";
        webSocket.broadcastTXT(message);
      }
      break;
    }
  }
}