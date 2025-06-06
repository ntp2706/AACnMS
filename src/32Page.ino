#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define SSID "NTP"
#define PASSWORD "qwert123"

#define WEBAPP_URL "https://script.google.com/macros/s/AKfycbwiUOj947ROPLbEiq3Mt4KV21B3g0lvjoncwc-Wjf4fwVfL819LAsCYcvZ3GpD8KhJI/exec"

const String FIREBASE_STORAGE_BUCKET = "test-96fdf.appspot.com";

IPAddress localIP;
char esp8266LocalIP[16];
char serverIP[16];

// lấy IP tự động thay thế octet 4
void createModifiedIP(IPAddress ip, uint8_t newLastOctet, char* result) {
  snprintf(result, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], newLastOctet);
}

String Feedback = ""; 
String Command = "";
String cmd = "";
String pointer = "";

byte receiveState = 0;
byte cmdState = 1;
byte strState = 1;
byte questionstate = 0;
byte equalstate = 0;
byte semicolonstate = 0;

int flashValue = 0;
bool webSocketConnected = false;

String nameSend;
String dobSend;
String roomSend;
String timestampSend;
String folderSend;
String emailSend;
String passwordSend;

String nameReceive = "N/A";
String dobReceive = "N/A";
String roomReceive = "N/A";
String timestampReceive = "0";

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiServer server(80);
WiFiClient client;
WebSocketsClient webSocket;

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.fb_count = 2;
  } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_count = 1;
    }

  config.jpeg_quality = 10;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    delay(1000);
    ESP.restart();
  }

  sensor_t * s = esp_camera_sensor_get();

  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
  }

  s->set_framesize(s, FRAMESIZE_CIF); // QVGA (320 x 240); CIF (352 x 288); VGA (640 x 480); SVGA (800 x 600); XGA (1024 x 768); SXGA (1280 x 1024); UXGA (1600 x 1200);
  s->set_brightness(s, 0);
  s->set_contrast(s, 2);
  s->set_saturation(s, 0);

  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  localIP = WiFi.localIP();

  createModifiedIP(localIP, 100, esp8266LocalIP);
  
  createModifiedIP(localIP, 50, serverIP);

  server.begin();

  webSocket.begin(esp8266LocalIP, 81, "/");
  webSocket.onEvent(onWebSocketEvent);
  if (!webSocketConnected) {
    webSocket.setReconnectInterval(1000);
  }
}

void processMessage(String message, int headerLength) {
  int index1 = message.indexOf('$');
  int index2 = message.indexOf('$', index1 + 1);
  int index3 = message.indexOf('$', index2 + 1);

  nameReceive = message.substring(headerLength, index1);
  dobReceive = message.substring(index1 + 1, index2);
  roomReceive = message.substring(index2 + 1, index3);
  timestampReceive = message.substring(index3 + 1);
}

void recallIdentifier(String id) {
  HTTPClient http;

  String urlNode = "http://" + String(serverIP) + ":3000/recall";
  String message = 
    "{\"identification\":\"" + id + "\"}";

  http.begin(urlNode);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int httpResponseCode = http.POST(message);

  if (httpResponseCode > 0) {
    String response = http.getString();
    if (response.indexOf("error") != -1) {
      String message = "InvalidInformation";
      webSocket.sendTXT(message);
    }
  }

  http.end();
}

void updateNotification(String main, String sub, String timeout, String color) {
  HTTPClient http;

  String urlNode = "http://" + String(serverIP) + ":3000/notify";
  String message = 
    "{\"main\":\"" + main + "\",\"sub\":\"" + sub + "\",\"timeout\":\"" + timeout + "\",\"color\":\"" + color + "\"}";

  http.begin(urlNode);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int httpResponseCode = http.POST(message);

  http.end();
}

void error() {
  updateNotification("Đã phát hiện lỗi",
    "Vui lòng khởi động lại hệ thống",
    "5000000","red");
}

void requestUpdate() {

  HTTPClient http;

  String urlNode = "http://" + String(serverIP) + ":3000/update";
    String message = 
    "{\"command\":\"update\"}";

  http.begin(urlNode);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int httpResponseCode = http.POST(message);
  http.end();
}

void deleteUser(String identification) {
  
  HTTPClient http;

  String urlNode = "http://" + String(serverIP) + ":3000/delete";
  String message = 
    "{\"identification\":\"" + identification + "\"}";

  http.begin(urlNode);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  int httpResponseCode = http.POST(message);

  http.end();
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  String message = String((char *)data).substring(0, len);
  
  if (message.startsWith("RestartSystem")) {
    updateNotification("Đang khởi động lại hệ thống",
      "Hệ thống sẽ khởi động lại sau vài giây",
      "5000000","red");
    ESP.restart();
  }

  else if (message.startsWith("ReadyToScan")) {
    updateNotification("Hãy quét thẻ",
      "Quét thẻ để thêm người dùng",
      "5000000","green");
  }

  else if (message.startsWith("ReadyToDelete")) {
    updateNotification("Quét thẻ để xóa người dùng",
      "Vui lòng quét thẻ để lấy thông tin",
      "5000000","red");
  }

  else if (message.startsWith("ScanDone")) {
    updateNotification("Hoàn tất ghi vào thẻ",
      "Đã ghi vào thẻ mới thành công",
      "5000","green");
  }

  else if (message.startsWith("DeleteFirstScanDone:")) {
    timestampReceive = message.substring(20);

    deleteUser(timestampReceive);

    updateNotification("Quét thẻ lại để hoàn tất",
      "Vui lòng quét lại thẻ để hoàn tất",
      "5000000","red");
  }

  else if (message.startsWith("DeleteDone")) {
    requestUpdate();
    updateNotification("Hoàn tất xóa thẻ",
      "Đã xóa thẻ thành công",
      "5000","red");
  }

  else if (message.startsWith("Recall:")) {
    processMessage(message, 7);
    recallIdentifier(timestampReceive);
  }
}

void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      webSocketConnected = true;
      updateNotification("Đã  kết nối WebSocket",
            "",
            "5000","green");
      break;
    case WStype_DISCONNECTED:
      webSocketConnected = false;
      updateNotification("Đã ngắt kết nối WebSocket",
            "Vui lòng khởi động lại hệ thống",
            "5000000","red");
      break;
    case WStype_TEXT:
      handleWebSocketMessage(NULL, payload, length);
      break;
  }
}

static const char PROGMEM INDEX_HTML[] = R"rawliteral(

<!DOCTYPE HTML><html>
<head>
    <title>Access Control</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="preconnect" href="https://fonts.gstatic.com">
    <link href="https://fonts.googleapis.com/css2?family=Roboto&display=swap" rel="stylesheet">
    <script src="https://cdnjs.cloudflare.com/ajax/libs/tracking.js/1.1.3/tracking-min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/tracking.js/1.1.3/data/face-min.js"></script>
</head>

<style>
    body {font-family: 'Montserrat', sans-serif; background-color: #f4f4f4;}
    .notification {background-color: #000; display:flex; width: 440px; height:174px; margin: auto auto; border: none; border-radius: 10px; transition: all 0.3s ease; box-shadow: 0px 0px 20px rgba(0,0,0,0.2);}
    .mainObj {display: flex; flex-direction: column; margin: 12px auto; width: 840px; padding: 20px; box-shadow: 0px 0px 20px rgba(0,0,0,0.2); background-color: #fff; border-radius: 5px;}
    .mainObj > div {margin: 4px auto; padding: 4px;}
    .highContainer {display: flex; flex-direction: row; background-color: transparent;}
    .highContainer > div {padding: 0px 40px 0px 0px; margin: 0px auto;}
    .highRightContainer {text-align: center; background-color: transparent;}
    button {width: 92px; background-color: #fff; margin: 10px auto auto auto; padding: 10px; color: #000; border: solid 2px #4CAF50; border-radius: 5px; cursor: pointer;}
    button:hover {background-color: #45a049; transform: scale(1.1); color: #fff; font-weight: bold;}
    button:hover svg path,
    button:hover svg line {fill: #ffffff;stroke: #ffffff;}
    .redBtn {border: solid 2px #FF6060;}
    .redBtn:hover {background-color: #FF6060;}
    .restartContainer {position: relative; width: 40px; height: 40px;}
    .restart {position: absolute; top: 0; left: 0; width: 100%; height: 100%; border: solid 2px #FF6060; background-color: transparent; cursor: pointer;}
    .restart:hover {background-color: #fff;}
    .restartLabel {position: absolute; top: 12px; left: 2px; width: 36px; height: 36px; border: none; border-radius: 5px; pointer-events: none;}
    img {width: 352px; height: 288px; background-color: #fff;}
    .rect {border: 2px solid #00ff00; position: absolute; box-sizing: border-box;}
    .LRContainer {z-index: 9999; position: relative; display: flex; flex-direction: row; margin-top: 4px;}
    .flashContainer {display: flex; flex-direction: column; align-items: center; position: absolute; top: 10px; right: 10px; }
    .flashLabel{width: 40px; height: 40px; background-color: #4CAF50; cursor: pointer; border-radius: 10px; transition: transform 0.3s, box-shadow 0.3s; }
    .flashLabel:hover {transform: scale(1.1); box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2); }
    .barContainer {display: flex; margin-top: 0px; padding: 10px; background-color: #fff; border-radius: 10px; box-shadow: 0 4px 12px rgba(0, 0, 0, 0.1); transition: opacity 0.3s, transform 0.3s; }
    .flashBar {width: 4px; height: 140px; -webkit-appearance: slider-vertical; }
    .optionsContainer {display: flex; flex-direction: column; position: absolute; text-align: center; top: 10px; left: 10px; }
    .optionsLabel{width: 40px; height: 40px; background-color: #4CAF50; cursor: pointer; border-radius: 10px; transition: transform 0.3s, box-shadow 0.3s; }
    .optionsLabel:hover {transform: scale(1.1); box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2); }
    .featureContainer {display: flex; flex-direction: column; background:transparent; border-radius: 10px; transition: opacity 0.3s, transform 0.3s;}
    .featureButton {width: 140px;}
    .selectionContainer {display: flex; width: 140px; flex-direction: row; justify-content: flex-end; padding: 10px 0px; background:transparent; border-radius: 8px; transition: opacity 0.3s, transform 0.3s;}
    .twoColumnLayout {display: flex; flex-direction: row; width: 100%;}
    .buttonColumn {display: flex; flex-direction: column;}
    .inputColumn {display: flex; flex-direction: column; margin-left: 10px; flex-grow: 1; justify-content: flex-start;}
    .inputColumn input {margin-top: 60px; height: 38px; border: 1px solid #ccc; border-radius: 5px;}
    input:checked+.slider {background-color: #45a049}
    input:checked+.slider:before {-webkit-transform: translateX(35.5px); -ms-transform: translateX(35.5px); transform: translateX(35.5px)}
    input[type='text'] {width: 100%; padding: 10px; background-color: #fff; border: 1px solid #ccc; box-sizing: border-box; border-radius: 4px; cursor: pointer; font-size: 13px; font-weight: bolder;}
    input[type='text']:focus {outline: none; border-color: #4CAF50; box-shadow: 0 0 5px #4CAF50;}
    input::placeholder {color: #cccccc; opacity: 0.8;}
    .hidden {display: none;}
</style>

<body>
    <div class="LRContainer">
        <div class="optionsContainer">
        <input type="checkbox" class="hidden" id="optionsCheckbox" onchange="showOptions(this)">
        <label class="optionsLabel" for="optionsCheckbox">
            <svg width="40" height="40" viewBox="-6 -6 36 36" fill="#ffffff" style="position:relative; vertical-align: middle;" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" >
                <path d="M0,3.875c0-1.104,0.896-2,2-2h20.75c1.104,0,2,0.896,2,2s-0.896,2-2,2H2C0.896,5.875,0,4.979,0,3.875z M22.75,10.375H2c-1.104,0-2,0.896-2,2c0,1.104,0.896,2,2,2h20.75c1.104,0,2-0.896,2-2C24.75,11.271,23.855,10.375,22.75,10.375z M22.75,18.875H2c-1.104,0-2,0.896-2,2s0.896,2,2,2h20.75c1.104,0,2-0.896,2-2S23.855,18.875,22.75,18.875z"/>
            </svg>
        </label>
        <div class="featureContainer hidden" id="featureContainer">
            <div class="twoColumnLayout">
                <div class="buttonColumn">
                    <button id="update" class="featureButton">Cập nhật CSDL</button>
                    <button id="generate" class="featureButton">Tạo tài khoản</button>
                    <button id="delete" class="featureButton redBtn">Xóa thẻ</button>
                </div>
                <div class="inputColumn">
                    <input type="text" id="email" style="width: 156px;" placeholder="Nhập email">
                </div>
            </div>
            <div class="restartContainer">
            <button id="restart" class="restart"></button>
            <img class="restartLabel" src="https://firebasestorage.googleapis.com/v0/b/accesscontrolsystem-4f265.appspot.com/o/src%2Frestart.gif?alt=media"/>
            </div>
            <div class="selectionContainer hidden" id="selectionContainer">
                <button id="confirm" class="confirm redBtn" style="width: 140px; margin-right: 8px;">Đồng ý</button>
                <button id="cancel" class="cancel" style="width: 50px;">Hủy</button>
            </div>
        </div>
        </div>
        <div class="flashContainer">
        <input type="checkbox" class="hidden" id="barCheckbox" onchange="showBar(this)">
        <label class="flashLabel" for="barCheckbox">
            <svg width="40" height="40" viewBox="-6 -6 36 36" fill="#ffffff" style="position:relative; vertical-align: middle;" xmlns="http://www.w3.org/2000/svg" >
                <path d="M12 0a1 1 0 0 1 1 1v4a1 1 0 1 1-2 0V1a1 1 0 0 1 1-1ZM4.929 3.515a1 1 0 0 0-1.414 1.414l2.828 2.828a1 1 0 0 0 1.414-1.414L4.93 3.515ZM1 11a1 1 0 1 0 0 2h4a1 1 0 1 0 0-2H1ZM18 12a1 1 0 0 1 1-1h4a1 1 0 1 1 0 2h-4a1 1 0 0 1-1-1ZM17.657 16.243a1 1 0 0 0-1.414 1.414l2.828 2.828a1 1 0 1 0 1.414-1.414l-2.828-2.828ZM7.757 17.657a1 1 0 1 0-1.414-1.414L3.515 19.07a1 1 0 1 0 1.414 1.414l2.828-2.828ZM20.485 4.929a1 1 0 0 0-1.414-1.414l-2.828 2.828a1 1 0 1 0 1.414 1.414l2.828-2.828ZM13 19a1 1 0 1 0-2 0v4a1 1 0 1 0 2 0v-4ZM12 7a5 5 0 1 0 0 10 5 5 0 0 0 0-10Z"/>
            </svg>
        </label>
        <div class="barContainer hidden" id="barContainer">
            <input type="range" class="flashBar" id="flash" min="0" max="255" value="0" orient="vertical">
        </div>
        </div>
    </div>

    <div class="mainObj" id="mainObjContainer">
        <div class="highContainer">
        <div>
            <div style="position: relative;">
                <img id="stream" style="border: none; border-radius: 10px;" src="" />
                <div id="overlay" style="position: absolute; top: 0; left: 0; width: 352px; height: 288px; pointer-events: none;"></div>
                <svg id="camera-overlay" class="camera-overlay" width="352" height="288" style="position: absolute; top: 0; left: 0; border-radius: 10px;" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
                    <rect width="352" height="288" fill="#4CAF50" />
                    <style>
                      @import url('https://fonts.googleapis.com/css2?family=Montserrat:wght@600&amp;display=swap');
                      text {
                        font-family: 'Montserrat', sans-serif;
                        font-weight: 600;
                        font-size: 24px;
                        text-anchor: middle;
                        dominant-baseline: middle;
                        fill: white;
                      }
                    </style>
                    <g transform="translate(176, 144)">
                      <g transform="translate(-15, -30)" fill="white">
                        <path d="M23.9917 0.608C24.6839 0.608 25.3583 0.8293 25.9146 1.24C26.4708 1.6507 26.8743 2.2267 27.0644 2.888L28.1333 6.6667H31.3333C32.3942 6.6667 33.4116 7.088 34.1618 7.8382C34.912 8.5884 35.3333 9.6058 35.3333 10.6667V28C35.3333 29.0609 34.912 30.0783 34.1618 30.8284C33.4116 31.5786 32.3942 32 31.3333 32H4C2.93913 32 1.92172 31.5786 1.17157 30.8284C0.421427 30.0783 0 29.0609 0 28V10.6667C0 9.6058 0.421427 8.5884 1.17157 7.8382C1.92172 7.088 2.93913 6.6667 4 6.6667H7.2L8.26893 2.888C8.45904 2.2267 8.86249 1.6507 9.41877 1.24C9.97505 0.8293 10.6494 0.608 11.3416 0.608H23.9917ZM23.9917 4.608H11.3416L10.2667 8.3867V8.4427V8.6667H4V28H31.3333V8.6667H25.0667L23.9917 4.608ZM17.6667 10.6667C19.7884 10.6667 21.8232 11.5446 23.3235 13.1074C24.8238 14.6702 25.6667 16.7493 25.6667 18.9173C25.6667 21.0854 24.8238 23.1644 23.3235 24.7272C21.8232 26.2901 19.7884 27.168 17.6667 27.168C15.5449 27.168 13.5101 26.2901 12.0098 24.7272C10.5095 23.1644 9.66667 21.0854 9.66667 18.9173C9.66667 16.7493 10.5095 14.6702 12.0098 13.1074C13.5101 11.5446 15.5449 10.6667 17.6667 10.6667ZM17.6667 14.6667C16.6058 14.6667 15.5884 15.088 14.8382 15.8382C14.088 16.5884 13.6667 17.6058 13.6667 18.6667C13.6667 19.7275 14.088 20.7449 14.8382 21.4951C15.5884 22.2453 16.6058 22.6667 17.6667 22.6667C18.7275 22.6667 19.7449 22.2453 20.4951 21.4951C21.2453 20.7449 21.6667 19.7275 21.6667 18.6667C21.6667 17.6058 21.2453 16.5884 20.4951 15.8382C19.7449 15.088 18.7275 14.6667 17.6667 14.6667Z" />
                      </g>
                      <text y="30">Đang chờ</text>
                    </g>
                </svg>
            </div>
        </div>
        <div class="highRightContainer" id="highRightContainer">
            <iframe id="notification" class="notification"> </iframe>
            <div class="feature">
                <button id="startStream">
                    <svg width="20" height="20" fill="#4CAF50" viewBox="0 0 36 36" style="margin-right: 8px; vertical-align: middle;" xmlns="http://www.w3.org/2000/svg">
                        <path d="M23.9917 0.608C24.6839 0.608 25.3583 0.8293 25.9146 1.24C26.4708 1.6507 26.8743 2.2267 27.0644 2.888L28.1333 6.6667H31.3333C32.3942 6.6667 33.4116 7.088 34.1618 7.8382C34.912 8.5884 35.3333 9.6058 35.3333 10.6667V28C35.3333 29.0609 34.912 30.0783 34.1618 30.8284C33.4116 31.5786 32.3942 32 31.3333 32H4C2.93913 32 1.92172 31.5786 1.17157 30.8284C0.421427 30.0783 0 29.0609 0 28V10.6667C0 9.6058 0.421427 8.5884 1.17157 7.8382C1.92172 7.088 2.93913 6.6667 4 6.6667H7.2L8.26893 2.888C8.45904 2.2267 8.86249 1.6507 9.41877 1.24C9.97505 0.8293 10.6494 0.608 11.3416 0.608H23.9917ZM23.9917 4.608H11.3416L10.2667 8.3867V8.4427V8.6667H4V28H31.3333V8.6667H25.0667L23.9917 4.608ZM17.6667 10.6667C19.7884 10.6667 21.8232 11.5446 23.3235 13.1074C24.8238 14.6702 25.6667 16.7493 25.6667 18.9173C25.6667 21.0854 24.8238 23.1644 23.3235 24.7272C21.8232 26.2901 19.7884 27.168 17.6667 27.168C15.5449 27.168 13.5101 26.2901 12.0098 24.7272C10.5095 23.1644 9.66667 21.0854 9.66667 18.9173C9.66667 16.7493 10.5095 14.6702 12.0098 13.1074C13.5101 11.5446 15.5449 10.6667 17.6667 10.6667ZM17.6667 14.6667C16.6058 14.6667 15.5884 15.088 14.8382 15.8382C14.088 16.5884 13.6667 17.6058 13.6667 18.6667C13.6667 19.7275 14.088 20.7449 14.8382 21.4951C15.5884 22.2453 16.6058 22.6667 17.6667 22.6667C18.7275 22.6667 19.7449 22.2453 20.4951 21.4951C21.2453 20.7449 21.6667 19.7275 21.6667 18.6667C21.6667 17.6058 21.2453 16.5884 20.4951 15.8382C19.7449 15.088 18.7275 14.6667 17.6667 14.6667Z" />
                    </svg>
                    Phát
                </button>
                <button id="recognizeFace" class="hidden">Nhận diện</button>
                <button id="getStill" class="hidden"></button>
                <button id="stopStream" class="redBtn">
                    <svg width="20" height="20" viewBox="0 0 36 36" fill="#FF6060" style="margin-right: 8px; vertical-align: middle;" xmlns="http://www.w3.org/2000/svg">
                        <path d="M23.9917 0.608C24.6839 0.608 25.3583 0.8293 25.9146 1.24C26.4708 1.6507 26.8743 2.2267 27.0644 2.888L28.1333 6.6667H31.3333C32.3942 6.6667 33.4116 7.088 34.1618 7.8382C34.912 8.5884 35.3333 9.6058 35.3333 10.6667V28C35.3333 29.0609 34.912 30.0783 34.1618 30.8284C33.4116 31.5786 32.3942 32 31.3333 32H4C2.93913 32 1.92172 31.5786 1.17157 30.8284C0.421427 30.0783 0 29.0609 0 28V10.6667C0 9.6058 0.421427 8.5884 1.17157 7.8382C1.92172 7.088 2.93913 6.6667 4 6.6667H7.2L8.26893 2.888C8.45904 2.2267 8.86249 1.6507 9.41877 1.24C9.97505 0.8293 10.6494 0.608 11.3416 0.608H23.9917ZM23.9917 4.608H11.3416L10.2667 8.3867V8.4427V8.6667H4V28H31.3333V8.6667H25.0667L23.9917 4.608ZM17.6667 10.6667C19.7884 10.6667 21.8232 11.5446 23.3235 13.1074C24.8238 14.6702 25.6667 16.7493 25.6667 18.9173C25.6667 21.0854 24.8238 23.1644 23.3235 24.7272C21.8232 26.2901 19.7884 27.168 17.6667 27.168C15.5449 27.168 13.5101 26.2901 12.0098 24.7272C10.5095 23.1644 9.66667 21.0854 9.66667 18.9173C9.66667 16.7493 10.5095 14.6702 12.0098 13.1074C13.5101 11.5446 15.5449 10.6667 17.6667 10.6667ZM17.6667 14.6667C16.6058 14.6667 15.5884 15.088 14.8382 15.8382C14.088 16.5884 13.6667 17.6058 13.6667 18.6667C13.6667 19.7275 14.088 20.7449 14.8382 21.4951C15.5884 22.2453 16.6058 22.6667 17.6667 22.6667C18.7275 22.6667 19.7449 22.2453 20.4951 21.4951C21.2453 20.7449 21.6667 19.7275 21.6667 18.6667C21.6667 17.6058 21.2453 16.5884 20.4951 15.8382C19.7449 15.088 18.7275 14.6667 17.6667 14.6667Z" />
                        <path d="M-2 -2L38 38" stroke="#FF6060" stroke-width="3" stroke-linecap="round" />
                      </svg>
                    Dừng
                </button>
            </div>
        </div>
        </div>
           <iframe id="infoIframe" style="width: 880px; height: 300px; border: none;"></iframe>
        </div>
    </div>
        <iframe class="hidden" id="ifr"></iframe>
</body>

<script>

    document.addEventListener('DOMContentLoaded', function (event) {

    // var baseHost = document.location.origin;
    var baseHost = window.location.protocol + '//' + window.location.host;
    var streamState = false;

    const streamContainer = document.getElementById('stream');
    const overlay = document.getElementById('overlay');

    const stillBtn = document.getElementById('getStill');
    const streamBtn = document.getElementById('startStream');
    const recognizeBtn = document.getElementById('recognizeFace');
    const stopBtn = document.getElementById('stopStream');

    const ifr = document.getElementById('ifr');

    const updateBtn = document.getElementById('update');
    const generateBtn = document.getElementById('generate');
    const deleteBtn = document.getElementById('delete');
    const restartBtn = document.getElementById('restart');

    const barContainer = document.getElementById('barContainer');
    const barCheckbox = document.getElementById('barCheckbox');
    const flash = document.getElementById('flash');

    const optionsCheckbox = document.getElementById('optionsCheckbox');
    const featureContainer = document.getElementById('featureContainer');

    const camera_overlay = document.getElementById('camera-overlay');

    function showOptions(checkbox) {
        if (checkbox.checked) {
        featureContainer.classList.remove('hidden');
        } else {
            featureContainer.classList.add('hidden');
        }
    }
    
    function uncheckOptions() {
        optionsCheckbox.checked = false;
        showOptions(optionsCheckbox);
    }

    optionsCheckbox.onchange = () => showOptions(optionsCheckbox);

    const selectionContainer = document.getElementById('selectionContainer');
    const confirmBtn = document.getElementById('confirm');
    const cancelBtn = document.getElementById('cancel');

    function showSelection() {
        selectionContainer.classList.remove('hidden');
    }

    function hideSelection() {
        selectionContainer.classList.add('hidden');
    }

    barCheckbox.onchange = function () {
        if (barCheckbox.checked) {
        barContainer.classList.remove('hidden');
        } else {
            barContainer.classList.add('hidden');
        }
    };

    var timestamp;

    let canDetect = true;
    let trackerTask = null;
    let found = null;

    const tracker = new tracking.ObjectTracker('face');

    tracker.setInitialScale(4);
    tracker.setStepSize(2);
    tracker.setEdgesDensity(0.1);

    tracker.on('track', async function(event) {
      while (overlay.firstChild) {
        overlay.removeChild(overlay.firstChild);
      }

      event.data.forEach(function(rect) {
        const div = document.createElement('div');
        div.classList.add('rect');
        div.style.width = rect.width + 'px';
        div.style.height = rect.height + 'px';
        div.style.left = rect.x + 'px';
        div.style.top = rect.y + 'px';
        overlay.appendChild(div);
      });

      if (event.data.length > 0) {
        found = true;
        if (canDetect) {
            canDetect = false;

            setTimeout(() => { 
              if (found) {
                recognizeBtn.click();
              }
            }, 200);
                          
            setTimeout(() => {
              canDetect = true;
            }, 5000);
          }
      } else {
          found = false;
        }
    });

    
    // stream bằng cách still liên tục
    streamBtn.onclick = function (event) {
        streamState = true;
        trackerTask = tracking.track('#stream', tracker, { camera: false });
        streamContainer.src = baseHost +'/?getstill=' + Math.random();
        camera_overlay.classList.add('hidden');
    };

    streamContainer.onload = function (event) {
        if (!streamState) return;
        streamBtn.click();
    };

    function updateValue(flash) {
        let value;
        value = flash.value;
        var query = `${baseHost}/?flash=${value}`;
        fetch(query)
    }

    flash.onchange = () => updateValue(flash);

    recognizeBtn.onclick = () => {
      const url = `${baseHost}/?recognizeface=${Date.now()}`;
      ifr.src = url;
    };

    updateBtn.onclick = function(event) {
        const url = `${baseHost}?update`;
        ifr.src = url;
        uncheckOptions();
    }

    generateBtn.onclick = function(event) {
      const now = new Date();
      const timestampTemp = (now.getFullYear() * 10000000000 + (now.getMonth() + 1) * 100000000 + now.getDate() * 1000000 + now.getHours() * 10000 + now.getMinutes() * 100 + now.getSeconds()).toString();
      const email = document.getElementById('email').value;
      const password = Math.floor(100000 + Math.random() * 900000).toString();
      const url = `${baseHost}?generate=content1=${timestampTemp}&content2=${encodeURIComponent(email)}&content3=${password}`;
      ifr.src = url;
      document.getElementById('email').value = '';
      uncheckOptions();
    }

    deleteBtn.onclick = function(event) {
        const url = `${baseHost}?delete`;
        ifr.src = url;
        uncheckOptions();
    }

    restartBtn.onclick = function(event) {
        showSelection();
        confirmBtn.onclick = function(event) {
        socket.send("RestartSystem");
        hideSelection();
        uncheckOptions();
        };
        cancelBtn.onclick = function(event) {
        hideSelection();
        uncheckOptions();
        }
    };

    let originalHostname = window.location.hostname;

    let ipParts = originalHostname.split('.');

    if (ipParts.length === 4) {
        ipParts[3] = '100';
        let esp8266LocalIP = ipParts.join('.');

        var socket = new WebSocket('ws://' + esp8266LocalIP + ':81');

        ipParts[3] = '50';
        let serverIP = ipParts.join('.');

        const notifyIfr = document.getElementById('notification');
        notifyIfr.src = 'http://' + serverIP + ':3000/notification';

        const infoIfr = document.getElementById('infoIframe');
        infoIfr.src = 'http://' + serverIP + ':3000/information';

        socket.onopen = function() {
        console.log('Web Server - ESP8266: OK');
        };

        socket.onmessage = function(event) {
            console.log(event.data);
            if (event.data.startsWith('DeleteFirstScanDone')) {
                canDetect = false;
              } else if (event.data == 'DeleteDone') {
                  canDetect = true;
                }
        };
    }
        
    stillBtn.onclick = () => {
        stillContainer.src = `${baseHost}/?getstill=${Date.now()}`;
    };

    stopBtn.onclick = function (event) { 
        streamState=false;    
        trackerTask.stop();
        window.stop();
        camera_overlay.classList.remove('hidden');
    };
});
    
</script>
</html>)rawliteral";

void mainpage() {
  String Data="";

  if (cmd!="")
    Data = Feedback;
  else
    Data = String((const char *)INDEX_HTML);
  
  // tách dữ liệu ra để in lần lượt
  for (int Index = 0; Index < Data.length(); Index = Index+1024) {
    client.print(Data.substring(Index, Index+1024));
  } 
  //--------------------------------
}

camera_fb_t * last_fb = NULL;

// giữ lại khung ảnh
void getStill() {
  
  if (last_fb != NULL) {
    esp_camera_fb_return(last_fb);
  }

  last_fb = esp_camera_fb_get();
  
  if (!last_fb) {
    error();
  }
  
  // in dữ liệu ảnh từ buffer lên webserver
  uint8_t *fbBuf = last_fb->buf;
  size_t fbLen = last_fb->len;

  for (size_t n = 0; n < fbLen; n += 1024) {
    if (n + 1024 < fbLen) {
      client.write(fbBuf, 1024);
      fbBuf += 1024;
    } else if (fbLen % 1024 > 0) {
      size_t remainder = fbLen % 1024;
      client.write(fbBuf, remainder);
    }
  }

  // cập nhật giá trị đèn flash
  ledcAttachChannel(4, 5000, 8, 4);
  ledcWrite(4, flashValue);  
}

// gửi ảnh lên server nhận diện
void recognizeFace() {
  String response;

  if (last_fb != NULL) {
    esp_camera_fb_return(last_fb);
  }

  last_fb = esp_camera_fb_get();
  
  if (!last_fb) {
    error();
  }
  
  uint8_t *fbBuf = last_fb->buf;
  size_t fbLen = last_fb->len;
  String imageData = "";
  
  for (size_t n = 0; n < fbLen; n += 1024) {
    if (n + 1024 < fbLen) {
      imageData += String((char*)fbBuf, 1024);
      fbBuf += 1024;
    } else if (fbLen % 1024 > 0) {
      size_t remainder = fbLen % 1024;
      imageData += String((char*)fbBuf, remainder);
    }
  }

  String urlNode = "http://" + String(serverIP) + ":3000/upload";
  
  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String head = 
    "--" + boundary + "\r\n" + 
    "Content-Disposition: form-data; name=\"image\"; filename=\"image.jpg\"\r\n" + 
    "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  int contentLength = head.length() + imageData.length() + tail.length();

  HTTPClient http;

  http.begin(urlNode);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(contentLength));
  http.setTimeout(10000);

  int httpResponseCode = http.sendRequest("POST", head + imageData + tail);

  if (httpResponseCode > 0) {
    String response = http.getString();
    if (response.indexOf("error") != -1) {
      String message = "InvalidInformation";
      webSocket.sendTXT(message);
    }
  }

  http.end();

  // cập nhật giá trị đèn flash
  ledcAttachChannel(4, 5000, 8, 4);
  ledcWrite(4, flashValue);  
}

// giải mã dữ liệu nhận từ webserver
String urlDecode(const String &str) {
  String decoded = "";
  for (int i = 0; i < str.length(); ++i) {
    if (str[i] == '%') {
      if (i + 2 < str.length()) {
        int value;
        sscanf(str.substring(i + 1, i + 3).c_str(), "%x", &value);
        decoded += (char)value;
        i += 2;
      }
    } else if (str[i] == '+') {
        decoded += ' ';
      } else {
          decoded += str[i];
        }
  }
  return decoded;
}

// tách chuỗi thông tin thành từng phần riêng biệt
void parseQueryString(const String &queryString, String *content1 = nullptr, String *content2 = nullptr, String *content3 = nullptr, String *content4 = nullptr) {
    int start = 0;
    int end = queryString.indexOf('&');

    while (end >= 0) {
        String pair = queryString.substring(start, end);
        int pos = pair.indexOf('=');
        if (pos >= 0) {
            String key = pair.substring(0, pos);
            String value = pair.substring(pos + 1);
            
            if (key == "content1" && content1) {
                *content1 = value;
            } else if (key == "content2" && content2) {
                *content2 = value;
            } else if (key == "content3" && content3) {
                *content3 = value;
            } else if (key == "content4" && content4) {
                *content4 = value;
            }
        }
        start = end + 1;
        end = queryString.indexOf('&', start);
    }

    // Xử lý phần cuối cùng sau dấu '&'
    String pair = queryString.substring(start);
    int pos = pair.indexOf('=');
    if (pos >= 0) {
        String key = pair.substring(0, pos);
        String value = pair.substring(pos + 1);

        if (key == "content1" && content1) {
            *content1 = value;
        } else if (key == "content2" && content2) {
            *content2 = value;
        } else if (key == "content3" && content3) {
            *content3 = value;
        } else if (key == "content4" && content4) {
            *content4 = value;
        }
    }
}

// gửi thông tin tài khoản
void processAccount(String encodeQuery) {
  String decodedQuery = urlDecode(encodeQuery);
  parseQueryString(decodedQuery, &folderSend, &emailSend, &passwordSend, nullptr);

  String message = "Database:" + folderSend;
  webSocket.sendTXT(message);

  String account = emailSend + "$" + passwordSend;

  String urlFirebase = "https://firebasestorage.googleapis.com/v0/b/" + FIREBASE_STORAGE_BUCKET + "/o?name=" + folderSend + "/verification.txt&uploadType=media";

  HTTPClient http;

  http.begin(urlFirebase);
  http.addHeader("Content-Type", "text/plain");

  int httpResponseCode = http.POST(account);
  http.end();
  
  HTTPClient shttp;

  String urlNode = "http://" + String(serverIP) + ":3000/forward";
    String jsonData = 
    "{\"command\":\"Generate\",\"content1\":\"" + emailSend + "\",\"content2\":\"" + folderSend + "\",\"content3\":\"" + passwordSend + "\",\"content4\":\"\"}";

  shttp.begin(urlNode);
  shttp.addHeader("Content-Type", "application/json");
  shttp.setTimeout(10000);
  int shttpResponseCode = shttp.POST(jsonData);
  shttp.end();
}

// đọc chuỗi ký tự để phân tích ra câu lệnh
void getCommand(char c) {
  if (c == '?') receiveState = 1;
  if ((c == ' ')||(c == '\r')||(c == '\n')) receiveState = 0;
  
  if (receiveState == 1)
  {
    Command = Command + String(c);
    
    if (c == '=') cmdState = 0;
    if (c == ';') strState++;
  
    if ((cmdState == 1)&&((c != '?')||(questionstate == 1))) cmd = cmd + String(c);
    if ((cmdState == 0)&&(strState == 1)&&((c != '=')||(equalstate == 1))) pointer = pointer + String(c);
    
    if (c == '?') questionstate = 1;
    if (c == '=') equalstate = 1;
    if ((strState >= 9)&&(c == ';')) semicolonstate = 1;
  }
}

// xử lý các câu lệnh
void executeCommand() {

  if (cmd == "recognizeface") {
      recognizeFace();
    } else if (cmd == "update") {
        requestUpdate();
      } else if (cmd == "delete") {
          String message = "DeleteUser";
          webSocket.sendTXT(message);
        } else if (cmd == "generate") {
            processAccount(pointer);
          } else if (cmd == "flash") {
              ledcAttachChannel(4, 5000, 8, 4);
              flashValue = pointer.toInt();
              ledcWrite(4, flashValue);  
            } else Feedback = "Command is not defined.";
  if (Feedback == "") Feedback = Command;  
}

// lắng nghe câu lệnh thông qua HTTP
void listenConnection() {
  Feedback = ""; Command = ""; cmd = ""; pointer = "";
  receiveState = 0, cmdState = 1, strState = 1, questionstate = 0, equalstate = 0, semicolonstate = 0;
  
  client = server.available();

  if (client) { 
    String currentLine = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();       
        getCommand(c);
                
        if (c == '\n') {
          if (currentLine.length() == 0) {    
            if (cmd == "getstill") {
              getStill();
            } else mainpage(); 
            Feedback = "";
            break;
          } else {
              currentLine = "";
            }
        } else if (c != '\r') {
            currentLine += c;
          }

        if ((currentLine.indexOf("/?") != -1)&&(currentLine.indexOf(" HTTP") != -1)) {
          if (Command.indexOf("stop") != -1) {
            client.println();
            client.stop();
          }
          currentLine = "";
          Feedback = "";
          executeCommand();
        }
      }
    }
    delay(1);
    client.stop();
  }
}

void loop() {  
  if (!webSocketConnected) {
    webSocket.setReconnectInterval(1000);
  }
  webSocket.loop();
  listenConnection();
}
