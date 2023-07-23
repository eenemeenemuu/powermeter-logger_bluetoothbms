// ==== CONFIGURATION ====
// BMS
#define BMS_MAX_CELLS 15 // defines size of data types
#define BMS_POLLING_INTERVAL 10*1000 // data output interval (shorter = connect more often = more battery consumption from BMS) in ms

// BLE
#define BLE_MIN_RSSI -75 // minimum signal strength before connection is attempted
#define BLE_NAME "xiaoxiang" // name of BMS
#define BLE_ADDRESS "a4:c1:38:1a:0c:49" // address of BMS

#define BLE_SCAN_DURATION 1 // duration of scan in seconds
#define BLE_REQUEST_DELAY 500 // package request delay after connecting - make this large enough to have the connection established in ms
#define BLE_TIMEOUT 10*1000 // timeout of scan + gathering packets (too short will fail collecting all packets) in ms

#define BLE_CALLBACK_DEBUG true // send debug messages via serial in callbacks (handy for finding your BMS address, name, RSSI, etc)

// WiFi
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"

// External host configuration
#define EXTERNAL_SERVER "example.com" // external host for logging and display
#define EXTERNAL_PATH "test_path/" // path on external host - must end with trailing slash (/)
#define EXTERNAL_KEY "test_key" // auth key must match on external host

// Time settings
#define TIME_ntpServer "pool.ntp.org"
#define TIME_gmtOffset_sec 3600
#define TIME_daylightOffset_sec 3600

// watchdog timeout
#define WATCHDOG_TIMEOUT (BLE_TIMEOUT+10*1000) // go to sleep after x seconds
