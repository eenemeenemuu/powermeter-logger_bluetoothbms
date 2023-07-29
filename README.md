# powermeter-logger_bluetoothbms 

Connects to Xiaoxiang BMS (www.lithiumbatterypcb.com) via Bluetooth and sends its data to a webserver over WiFi.

This work is based on https://github.com/BeaverUI/ESP32-BluetoothBMS2MQTT.

## Required stuff
* ESP32 (with Bluetooth and WiFi). This code was tested on "ESP32 NodeMCU Development Board" / ESP-WROOM-32, but should work with any ESP32.
* HTTPS webserver, running https://github.com/eenemeenemuu/powermeter (or any other software which accepts the same transmitted data).
## Programming the ESP
* Install the Arduino IDE
* Configure the IDE (see code for more info):
	* In preferences, add the board manager URL
	* Install the required libraries
* Open main.ino
* Configure the programmer:
	* Connect the ESP32 board via USB, select the correct COM port
	* See programmer-config.png for the other settings

## Configuring
To configure the module, change the values in config.h to your needs, especially:
* WiFi (SSID + password)
* external host (server + path + key)
* BLE name and address
