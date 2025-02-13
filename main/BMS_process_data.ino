bool isPacketValid(byte *packet) //check if packet is valid
{
    if (packet == nullptr){
        return false;
    }

    bmsPacketHeaderStruct *pHeader = (bmsPacketHeaderStruct *)packet;
    int checksumPos = pHeader->dataLen + 2; // status + data len + data

    int offset = 2; // header 0xDD and command type are not in data length

    if (packet[0] != 0xDD){
        // start bit missing
        return false;
    }

    if (packet[offset + checksumPos + 2] != 0x77){
        // stop bit missing
        return false;
    }

    byte checksum = 0;
    for (int i = 0; i < checksumPos; i++){
        checksum += packet[offset + i];
    }
    checksum = ((checksum ^ 0xFF) + 1) & 0xFF;

    if (checksum != packet[offset + checksumPos + 1]){
        return false;
    }
    
    return true;
}

bool processBasicInfo(packBasicInfoStruct *output, byte *data, unsigned int dataLen)
{
    // Expected data len
    if (dataLen != 0x1B)
    {
        return false;
    }

    output->Volts = ((uint32_t)two_ints_into16(data[0], data[1])) * 10; // Resolution 10 mV -> convert to milivolts   eg 4895 > 48950mV
    output->Amps = ((int32_t)two_ints_into16(data[2], data[3])) * 10;   // Resolution 10 mA -> convert to miliamps

    output->Watts = output->Volts * output->Amps / 1000000; // W

    output->CapacityRemainAh = ((uint16_t)two_ints_into16(data[4], data[5])) * 10;
    output->CapacityRemainPercent = ((uint8_t)data[19]);

    output->Temp1 = (((uint16_t)two_ints_into16(data[23], data[24])) - 2731);
    output->Temp2 = (((uint16_t)two_ints_into16(data[25], data[26])) - 2731);
    output->BalanceCodeLow = (two_ints_into16(data[12], data[13]));
    output->BalanceCodeHigh = (two_ints_into16(data[14], data[15]));
    output->MosfetStatus = ((byte)data[20]);

    return true;
}

bool processCellInfo(packCellInfoStruct *output, byte *data, unsigned int dataLen)
{
    uint16_t _cellSum;
    uint16_t _cellMin = 5000;
    uint16_t _cellMax = 0;
    uint16_t _cellAvg;
    uint16_t _cellDiff;

    output->NumOfCells = dataLen / 2; // data contains 2 bytes per cell

    //go trough individual cells
    for (byte i = 0; i < dataLen / 2; i++)
    {
        output->CellVolt[i] = ((uint16_t)two_ints_into16(data[i * 2], data[i * 2 + 1])); // Resolution 1 mV
        _cellSum += output->CellVolt[i];
        if (output->CellVolt[i] > _cellMax)
        {
            _cellMax = output->CellVolt[i];
        }
        if (output->CellVolt[i] < _cellMin)
        {
            _cellMin = output->CellVolt[i];
        }
    }
    
    output->CellMin = _cellMin;
    output->CellMax = _cellMax;
    output->CellDiff = _cellMax - _cellMin;
    output->CellAvg = _cellSum / output->NumOfCells;

    return true;
}

bool bmsProcessPacket(byte *packet)
{
    bool isValid = isPacketValid(packet);

    if (isValid != true)
    {
        Serial.println("Invalid packer received");
        return false;
    }

    bmsPacketHeaderStruct *pHeader = (bmsPacketHeaderStruct *)packet;
    byte *data = packet + sizeof(bmsPacketHeaderStruct); // TODO Fix this ugly hack
    unsigned int dataLen = pHeader->dataLen;

    bool result = false;

    // find packet type (basic info or cell info)
    switch (pHeader->type)
    {
    case cBasicInfo:
    {
        // Process basic info
        result = processBasicInfo(&packBasicInfo, data, dataLen);
        if(result==true){
          ble_packets_received |= 0b01;
          bms_last_update_time=millis();
        }
        
        break;
    }

    case cCellInfo:
    {
        // Process cell info
        result = processCellInfo(&packCellInfo, data, dataLen);
        if(result==true){
          ble_packets_received |= 0b10;
          bms_last_update_time=millis();
        }
        break;
    }

    default:
        result = false;
        Serial.printf("Unsupported packet type detected. Type: %d", pHeader->type);
    }

    return result;
}

bool bleCollectPacket(char *data, uint32_t dataSize) // reconstruct packet, called by notifyCallback function
{
    static uint8_t packetstate = 0; //0 - empty, 1 - first half of packet received, 2- second half of packet received

    // packet sizes: 
    //  (packet ID 03) = 4 (header) + 23 + 2*N_NTCs + 2 (checksum) + 1 (stop)
    //  (packet ID 04) = 4 (header) + 2*NUM_CELLS   + 2 (checksum) + 1 (stop)
    static uint8_t packetbuff[4 + 2*25 + 2 + 1] = {0x0}; // buffer size suitable for up to 25 cells
    
    static uint32_t totalDataSize = 0;
    bool retVal = false;
    //hexDump(data,dataSize);
      
    if(totalDataSize + dataSize > sizeof(packetbuff)){
      Serial.printf("ERROR: datasize is overlength.");

      totalDataSize = 0;
      packetstate = 0;
      
      retVal = false;
    }
    else if (data[0] == 0xdd && packetstate == 0) // probably got 1st half of packet
    {
        packetstate = 1;
        for (uint8_t i = 0; i < dataSize; i++)
        {
            packetbuff[i] = data[i];
        }
        totalDataSize = dataSize;
        retVal = true;
    }
    else if (data[dataSize - 1] == 0x77 && packetstate == 1) //probably got 2nd half of the packet
    {
        packetstate = 2;
        for (uint8_t i = 0; i < dataSize; i++)
        {
            packetbuff[i + totalDataSize] = data[i];
        }
        totalDataSize += dataSize;
        retVal = true;
    }
    
    if (packetstate == 2) //got full packet
    {
        uint8_t packet[totalDataSize];
        memcpy(packet, packetbuff, totalDataSize);

        bmsProcessPacket(packet); //pass pointer to retrieved packet to processing function
        packetstate = 0;
        totalDataSize = 0;
        retVal = true;
    }
    return retVal;
}

bool bmsRequestBasicInfo(){
    // header status command length data checksum footer
    //   DD     A5      03     00    FF     FD      77
    uint8_t data[7] = {0xdd, 0xa5, cBasicInfo, 0x0, 0xff, 0xfd, 0x77};
    return sendCommand(data, sizeof(data));
}

bool bmsRequestCellInfo(){
    // header status command length data checksum footer
    //  DD      A5      04     00    FF     FC      77
    uint8_t data[7] = {0xdd, 0xa5, cCellInfo, 0x0, 0xff, 0xfc, 0x77};
    return sendCommand(data, sizeof(data));
}

/*
void printBasicInfo() //debug all data to uart
{
    Serial.printf("Total voltage: %f\n", (float)packBasicInfo.Volts / 1000);
    Serial.printf("Amps: %f\n", (float)packBasicInfo.Amps / 1000);
    Serial.printf("CapacityRemainAh: %f\n", (float)packBasicInfo.CapacityRemainAh / 1000);
    Serial.printf("CapacityRemainPercent: %d\n", packBasicInfo.CapacityRemainPercent);
    Serial.printf("Temp1: %f\n", (float)packBasicInfo.Temp1 / 10);
    Serial.printf("Temp2: %f\n", (float)packBasicInfo.Temp2 / 10);
    Serial.printf("Balance Code Low: 0x%x\n", packBasicInfo.BalanceCodeLow);
    Serial.printf("Balance Code High: 0x%x\n", packBasicInfo.BalanceCodeHigh);
    Serial.printf("Mosfet Status: 0x%x\n", packBasicInfo.MosfetStatus);
}

void printCellInfo() //debug all data to uart
{
    Serial.printf("Number of cells: %u\n", packCellInfo.NumOfCells);
    for (byte i = 1; i <= packCellInfo.NumOfCells; i++)
    {
        Serial.printf("Cell no. %u", i);
        Serial.printf("   %f\n", (float)packCellInfo.CellVolt[i - 1] / 1000);
    }
    Serial.printf("Max cell volt: %f\n", (float)packCellInfo.CellMax / 1000);
    Serial.printf("Min cell volt: %f\n", (float)packCellInfo.CellMin / 1000);
    Serial.printf("Difference cell volt: %f\n", (float)packCellInfo.CellDiff / 1000);
    Serial.printf("Average cell volt: %f\n", (float)packCellInfo.CellAvg / 1000);
    Serial.println();
}

void constructBigString() //debug all data to uart
{
    stringBuffer[0] = '\0'; //clear old data
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Total voltage: %f\n", (float)packBasicInfo.Volts / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Amps: %f\n", (float)packBasicInfo.Amps / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "CapacityRemainAh: %f\n", (float)packBasicInfo.CapacityRemainAh / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "CapacityRemainPercent: %d\n", packBasicInfo.CapacityRemainPercent);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Temp1: %f\n", (float)packBasicInfo.Temp1 / 10);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Temp2: %f\n", (float)packBasicInfo.Temp2 / 10);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Balance Code Low: 0x%x\n", packBasicInfo.BalanceCodeLow);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Balance Code High: 0x%x\n", packBasicInfo.BalanceCodeHigh);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Mosfet Status: 0x%x\n", packBasicInfo.MosfetStatus);

    snprintf(stringBuffer, STRINGBUFFERSIZE, "Number of cells: %u\n", packCellInfo.NumOfCells);
    for (byte i = 1; i <= packCellInfo.NumOfCells; i++)
    {
        snprintf(stringBuffer, STRINGBUFFERSIZE, "Cell no. %u", i);
        snprintf(stringBuffer, STRINGBUFFERSIZE, "   %f\n", (float)packCellInfo.CellVolt[i - 1] / 1000);
    }
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Max cell volt: %f\n", (float)packCellInfo.CellMax / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Min cell volt: %f\n", (float)packCellInfo.CellMin / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Difference cell volt: %f\n", (float)packCellInfo.CellDiff / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Average cell volt: %f\n", (float)packCellInfo.CellAvg / 1000);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "\n");
}

void hexDump(const char *data, uint32_t dataSize) //debug function
{
    Serial.println("HEX data:");

    for (int i = 0; i < dataSize; i++)
    {
        Serial.printf("0x%x, ", data[i]);
    }
    Serial.println("");
}
*/

int16_t two_ints_into16(int highbyte, int lowbyte) // turns two bytes into a single long integer
{
    int16_t result = (highbyte);
    result <<= 8;                //Left shift 8 bits,
    result = (result | lowbyte); //OR operation, merge the two
    return result;
}
