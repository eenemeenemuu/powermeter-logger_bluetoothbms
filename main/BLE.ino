BLEScan* pBLEScan = nullptr;
BLEClient* pClient = nullptr;
BLEAdvertisedDevice* pRemoteDevice = nullptr;
BLERemoteService* pRemoteService = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic_rx = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic_tx = nullptr;

// 0000ff01-0000-1000-8000-00805f9b34fb
// Notifications from this characteristic is received data from BMS
// NOTIFY, READ

// 0000ff02-0000-1000-8000-00805f9b34fb
// Write this characteristic to send data to BMS
// READ, WRITE, WRITE NO RESPONSE

boolean doScan = false; // becomes true when BLE is initialized and scanning is allowed
boolean doConnect = false; // becomes true when correct ID is found during scanning

boolean ble_client_connected = false; // true when fully connected

unsigned int ble_packets_requested = 0b00; // keeps track of requested packets
unsigned int ble_packets_received = 0b00; // keeps track of received packets


// ======= CALLBACKS =========

void MyEndOfScanCallback(BLEScanResults pBLEScanResult){
    bms_status=false; // BMS not found
    
    if(BLE_CALLBACK_DEBUG){
      Serial.println("Scan finished.");
    }
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks{
  // called for each advertising BLE server

  void onResult(BLEAdvertisedDevice advertisedDevice){
    // found a device


    if(BLE_CALLBACK_DEBUG){
      Serial.print("BLE: found ");
      Serial.println(advertisedDevice.toString().c_str());
    }
    
    

    // Check if found device is the one we are looking for
    if(
        strcmp(advertisedDevice.getName().c_str(), BLE_NAME)==0 &&
        strcmp(advertisedDevice.getAddress().toString().c_str(), BLE_ADDRESS)==0 &&
        advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(serviceUUID)
      ){
    
      if(BLE_CALLBACK_DEBUG){
      }
      
      pBLEScan->stop();

      if(advertisedDevice.getRSSI() >= BLE_MIN_RSSI){
 
        // delete old remote device, create new one
        if(pRemoteDevice != nullptr){
          delete pRemoteDevice;
        }
        pRemoteDevice = new BLEAdvertisedDevice(advertisedDevice);

        doConnect = true;        
      }else{
        if(BLE_CALLBACK_DEBUG){
        }
      }
    }
  }
};

class MyClientCallback : public BLEClientCallbacks{
  // called on connect/disconnect
  void onConnect(BLEClient* pclient){
    
    if(BLE_CALLBACK_DEBUG){
    }
  }

  void onDisconnect(BLEClient* pclient){
    ble_client_connected = false;
    doConnect = false;
   
    if(BLE_CALLBACK_DEBUG){
    }

  }
};

static void MyNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify){
  //this is called when BLE server sents data via notification
  //hexDump((char*)pData, length);
  if(!bleCollectPacket((char *)pData, length)){
  }
}





// ======= OTHERS =========

void handleBLE(){
  static unsigned long prev_millis_standby = 0;
  
  prev_millis_standby = millis();
  
  while(true){ // loop until we hit a timeout or gathered all packets
    
    if((ble_packets_received == BLE_PACKETSRECEIVED_BEFORE_STANDBY) || (millis()>prev_millis_standby+BLE_TIMEOUT)){    
      if(ble_packets_received == BLE_PACKETSRECEIVED_BEFORE_STANDBY){
        bms_status=true; // BMS was connected, data up-to-date
        
      }else{
        bms_status=false; // BMS not (fully) connected
      }

      break; // we're done with BLE, exit while loop
    }
    else if (doConnect){
      
      // found the desired BLE server, now connect to it
      if (connectToServer()){
        ble_client_connected = true;
        ble_packets_received=0;
        ble_packets_requested=0;
  
      }else{
        ble_client_connected = false;
      }
      
      doConnect = false;
    }
  
    if (ble_client_connected){
      // if connected to BLE server, request all data
      if((ble_packets_requested & 0b01)!=0b01){
        // request packet 0b01
        delay(50);
        if(bmsRequestBasicInfo()){
          ble_packets_requested |= 0b01;
        }
        
      }else if(((ble_packets_received & 0b01)==0b01) && ((ble_packets_requested & 0b10)!=0b10)){
        // request packet 0b10 after 0b01 has been received
        delay(50);
        if(bmsRequestCellInfo()){
          ble_packets_requested |= 0b10;
        }
      }
      
    }else if ((!doConnect)&&(doScan)){
      // we are not connected, so we can scan for devices
      Serial.print("BLE is not connected, starting scan");

      // Disconnect client
      if((pClient != nullptr)&&(pClient->isConnected())){
        pClient->disconnect();
      }

      // stop scan (if running) and start a new one
      pBLEScan->setActiveScan(true);
      pBLEScan->setInterval(1 << 8); // 160 ms
      pBLEScan->setWindow(1 << 7); // 80 ms
      pBLEScan->start(BLE_SCAN_DURATION, MyEndOfScanCallback, false); // non-blocking, use a callback
      
      doScan=false;
    }
  }
}

void bleGatherPackets(){
  bleStart();
  handleBLE();
  blePause();
  BLEDevice::deinit(false);
}

void bleStart(){
  Serial.print("Starting BLE... ");

  BLEDevice::init("");
  //esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); // release some unused memory
  
  // Retrieve a BLE client
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  
  // Retrieve a BLE scanner
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());

  bleContinue();
  Serial.println("done");
}

void blePause(){
  // stop scanning and disconnect from all devices
  doScan=false;

  // Disconnect client
  if((pClient != nullptr)&&(pClient->isConnected())){
    pClient->disconnect();
  }
  
  delay(50);
    
  pBLEScan->stop();
  
  ble_client_connected=false;
  doConnect=false;
  ble_packets_received=0;
  ble_packets_requested=0;

}


void bleContinue(){
  // Prepare for scanning
  ble_client_connected=false;
  doConnect=false;
  ble_packets_received=0;
  
  doScan=true; // start scanning for new devices
}

bool connectToServer(){
  if(pRemoteDevice==nullptr){
    Serial.println("Invalid remote device, can't connect");
    return false;
  }

  // Disconnect client
  if((pClient != nullptr)&&(pClient->isConnected())){
    pClient->disconnect();
  }

  Serial.print("Forming a connection to ");
  Serial.println(pRemoteDevice->getAddress().toString().c_str());
  
  delay(100);

  // Connect to the remote BLE Server.
  pClient->connect(pRemoteDevice);
  if(!(pClient->isConnected())){
    Serial.println("Failed to connect to server");
    pClient->disconnect();
    return false;   
  }
  
  Serial.println(" - Connected to server");


  // Get remote service
  pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr){
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  
  // Get BMS receive characteristic
  pRemoteCharacteristic_rx = pRemoteService->getCharacteristic(charUUID_rx);
  if (pRemoteCharacteristic_rx == nullptr){
    Serial.print("Failed to find rx UUID: ");
    Serial.println(charUUID_rx.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found RX characteristic");


  // Register callback for remote characteristic (receive channel)
  if (pRemoteCharacteristic_rx->canNotify()){
      pRemoteCharacteristic_rx->registerForNotify(MyNotifyCallback);
  }else{
    Serial.println("Failed to register notification of remote characteristic");
    pClient->disconnect();
    return false;   
  }
  Serial.println(" - Registered remote characteristic for notification");


  // Get BMS transmit characteristic
  pRemoteCharacteristic_tx = pRemoteService->getCharacteristic(charUUID_tx);
  if (pRemoteCharacteristic_tx == nullptr){
    Serial.print("Failed to find tx UUID: ");
    Serial.println(charUUID_tx.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found TX characteristic");


  // Check whether tx is writeable
  if (!(pRemoteCharacteristic_tx->canWriteNoResponse())){
    Serial.println("Failed TX remote characteristic is not writable");
    pClient->disconnect();
    return false;
  }
  Serial.println(" - TX is writable");

  
  delay(BLE_REQUEST_DELAY); // wait, otherwise writeValue doesn't work for some reason
                            // to do: fix this ugly hack
  
  return true;
}

bool sendCommand(uint8_t *data, uint32_t dataLen){
  if((pClient!=nullptr)&&(pClient->isConnected())){
    pRemoteCharacteristic_tx->writeValue(data, dataLen, false);
    return true;
  }else{
    return false;
  }
}
