/*
 * Send data of Xiaoxiang BMS to HTTPS server via WiFi
 * by eenemeenemuu - https://github.com/eenemeenemuu/powermeter-logger_bluetoothbms
 * 
 * Based on original work from https://github.com/kolins-cz/Smart-BMS-Bluetooth-ESP32/blob/master/README.md
 * Based on further work by Bas Vermulst from https://github.com/BeaverUI/ESP32-BluetoothBMS2MQTT
 * 
 
   === configuring ===
   Using the #define parameters in the config.h file, do the following:
   1) configure WiFi via WIFI_SSID and WIFI_PASSWORD
   2) configure external host
   3) configue time settings
   4) ensure the BMS settings are OK. You can verify the name and address using the "BLE Scanner" app on an Android phone.
   

   === compiling ===
   1) Add ESP-WROVER to the board manager:
   - File --> Preferences, add to board manager URLs: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   - Then, Tools --> Board --> Board manager... --> install ESP32 package v2.0.3 or later.

   2) Configure board:
   Select Tools --> Board --> "ESP32 Arduino" --> "ESP WRover module"

   3) Connect board via USB.
   
   4) Configure programming options:
   Select appropriate programming port under Tools --> Port
   Select correct partitioning: 1.9MB app (with OTA) or 2MB app with 2 MB SPIFFS - otherwise it won't fit

   5) Program and go!
 
 */
 
// ==== MAIN CODE ====
#include "config.h"
#include "datatypes.h" // for brevity the BMS stuff is in this file
#include <WiFi.h> // for WiFi
#include <BLEDevice.h> // for BLE

#include <driver/adc.h> // to read ESP battery voltage
#include <rom/rtc.h> // to get reset reason

#include <HTTPClient.h> // for HTTP
#include <WiFiClientSecure.h> // for HTTPS
#include "time.h" // to get current time

// Init BMS
static BLEUUID serviceUUID("0000ff00-0000-1000-8000-00805f9b34fb"); //xiaoxiang bms service
static BLEUUID charUUID_rx("0000ff01-0000-1000-8000-00805f9b34fb"); //xiaoxiang bms rx id
static BLEUUID charUUID_tx("0000ff02-0000-1000-8000-00805f9b34fb"); //xiaoxiang bms tx id

const byte cBasicInfo = 3; //datablock 3=basic info
const byte cCellInfo = 4;  //datablock 4=individual cell info
packBasicInfoStruct packBasicInfo;
packCellInfoStruct packCellInfo;
unsigned long bms_last_update_time=0;
bool bms_status;
#define BLE_PACKETSRECEIVED_BEFORE_STANDBY 0b11 // packets to gather before disconnecting

// Other stuff
String debug_log_string="";
hw_timer_t * wd_timer = NULL;

void setup(){
  // use a watchdog to set the sleep timer, this avoids issues with the crashing BLE stack
  // at some point we should smash this bug, but for now this workaround ensures reliable operation
  enableWatchdogTimer();
  
  Serial.begin(115200);
  
  // connect BLE, gather data from BMS, then disconnect and unload BLE
  bms_status=false;
  while (!bms_status) {
    bleGatherPackets();
  }

  // Start networking
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // optional: fixed IP address, it is recommended to assign a fixed IP via the DHCP server instead
  // IPAddress ip(192,168,1,31); IPAddress gateway(192,168,1,1); IPAddress subnet(255,255,0,0); WiFi.config(ip, gateway, subnet);
  Serial.print("Attempting to connect to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  Serial.println("Connected");
  Serial.println("IP address: " + IPAddressString(WiFi.localIP()));

  // get current time
  configTime(0, 0, TIME_ntpServer); // 0, 0 because we will use TZ in the next line
  setenv("TZ", TIME_TZ, 1);         // Set environment variable with your time zone
  tzset();                          // Credits: https://werner.rothschopf.net/microcontroller/202103_arduino_esp32_ntp_en.htm
  struct tm timeinfo;
  char date[20];
  char time[20];
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  } else {
    strftime(date, 11, "%d.%m.%Y", &timeinfo);
    strftime(time, 9, "%H:%M:%S", &timeinfo);
    Serial.println("Date: " + String(date));
    Serial.println("Time: " + String(time));

    HTTPClient http_client;
    WiFiClientSecure https_client;
    https_client.setInsecure();

    String stats_string = String(date) + 
        "," + String(time) + 
        "," + String((float)packBasicInfo.Amps/1000,2) + 
        "," + String((float)packBasicInfo.CapacityRemainAh/1000,2) + 
        "," + String((float)packCellInfo.CellMax/1000,3) + 
        "," + String((float)packCellInfo.CellMin/1000,3) +
        "," + String((float)packCellInfo.CellAvg/1000,3); 

    Serial.println();
    for (String url : urls) {
      Serial.println(url.substring(0, url.indexOf("?")));
      if (url.substring(0, 5) == "https") {
        String external_server = url.substring(8).substring(0, url.substring(8).indexOf("/"));
        String external_path = url.substring(8).substring(url.substring(8).indexOf("/"));
        if (!https_client.connect(external_server.c_str(), 443)) {
          Serial.println("Connection failed!");
        } else {
          Serial.println("Connected to server!");
          https_client.println("GET " + external_path + stats_string + " HTTP/1.0");
          https_client.println("Host: " + external_server);
          https_client.println("Connection: close");
          https_client.println();

          while (https_client.connected()) {
            String line = https_client.readStringUntil('\n');
            if (line == "\r") {
              //Serial.println("headers received");
              break;
            }
          }
          https_client.stop();
        }
      } else {
        String serverPath = url + stats_string;
        http_client.begin(serverPath.c_str());
        int httpResponseCode = http_client.GET();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        http_client.end();
      }
      Serial.println();
    }
  }
  Serial.println("All done, disconnecting.");
  WiFi.disconnect();
  Serial.flush();
  delay(1000); // give it 1 second to flush everything
  // done, now we wait for the wdt timer interrupt that puts us in deep sleep
}


// === Main stuff ====
void loop(){

}


// enable watchdog timer -- a very ugly hack to overcome crashes of the BLE stack
// (desperate times ask for desperate measures)
void enableWatchdogTimer(){
  wd_timer = timerBegin(0, 80, true);  
  timerAttachInterrupt(wd_timer, &WatchDogTimeoutHandler, true);
  timerAlarmWrite(wd_timer, WATCHDOG_TIMEOUT*1e3, false);
  timerAlarmEnable(wd_timer);
}


// WDT handler to put ESP in deep sleep after data has been obtained
void ARDUINO_ISR_ATTR WatchDogTimeoutHandler()
{ 
  esp_sleep_enable_timer_wakeup((BMS_POLLING_INTERVAL - WATCHDOG_TIMEOUT) * 1e3); // standby period is in ms, function accepts us
  esp_deep_sleep_start(); // sweet dreams
}

// ===== Helper functions =====
String GetResetReason(int core)
{
  RESET_REASON reason=rtc_get_reset_reason(core);
  switch (reason)
  {
    case 1 : return String("POWERON_RESET");break;          /**<1, Vbat power on reset*/
    case 3 : return String("SW_RESET");break;               /**<3, Software reset digital core*/
    case 4 : return String("OWDT_RESET");break;             /**<4, Legacy watch dog reset digital core*/
    case 5 : return String("DEEPSLEEP_RESET");break;        /**<5, Deep Sleep reset digital core*/
    case 6 : return String("SDIO_RESET");break;             /**<6, Reset by SLC module, reset digital core*/
    case 7 : return String("TG0WDT_SYS_RESET");break;       /**<7, Timer Group0 Watch dog reset digital core*/
    case 8 : return String("TG1WDT_SYS_RESET");break;       /**<8, Timer Group1 Watch dog reset digital core*/
    case 9 : return String("RTCWDT_SYS_RESET");break;       /**<9, RTC Watch dog Reset digital core*/
    case 10 : return String("INTRUSION_RESET");break;       /**<10, Instrusion tested to reset CPU*/
    case 11 : return String("TGWDT_CPU_RESET");break;       /**<11, Time Group reset CPU*/
    case 12 : return String("SW_CPU_RESET");break;          /**<12, Software reset CPU*/
    case 13 : return String("RTCWDT_CPU_RESET");break;      /**<13, RTC Watch dog Reset CPU*/
    case 14 : return String("EXT_CPU_RESET");break;         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : return String("RTCWDT_BROWN_OUT_RESET");break;/**<15, Reset when the vdd voltage is not stable*/
    case 16 : return String("RTCWDT_RTC_RESET");break;      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : return String("NO_MEAN");
  }
}
