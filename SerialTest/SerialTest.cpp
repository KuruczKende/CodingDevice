// SerialTest.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "serialib.h"

#define SERIAL_PORT "\\\\.\\COM6"

static const char acHex[17] = "0123456789ABCDEF";

static void vByte2Hex(uint8_t u8Byte, char* pchOut) {
    pchOut[0] = acHex[(u8Byte >> 4) & 0x0F];
    pchOut[1] = acHex[u8Byte & 0x0F];
}

static void vU162Hex(uint16_t u16Value, char* pchOut) {
    pchOut[2] = acHex[(u16Value >> 12) & 0x0F];
    pchOut[3] = acHex[(u16Value >> 8) & 0x0F];
    pchOut[0] = acHex[(u16Value >> 4) & 0x0F];
    pchOut[1] = acHex[u16Value & 0x0F];
}

static uint8_t u8CRC8(const uint8_t* pu8Data, uint16_t u16Len) {
    uint8_t u8Crc = 0x00;
    uint16_t u16Idx;
    uint8_t u8Bit;

    for (u16Idx = 0; u16Idx < u16Len; u16Idx++) {
        u8Crc ^= pu8Data[u16Idx];
        for (u8Bit = 0; u8Bit < 8; u8Bit++) {
            if (u8Crc & 0x80) u8Crc = (uint8_t)((u8Crc << 1) ^ 0x07);
            else u8Crc <<= 1;
        }
    }
    return u8Crc;
}

static uint16_t u16CRC16(const uint8_t* pu8Data, uint16_t u16Len) {
    uint16_t u16Crc = 0xFFFF;
    uint16_t u16Idx;
    uint8_t u8Bit;

    for (u16Idx = 0; u16Idx < u16Len; u16Idx++) {
        u16Crc ^= ((uint16_t)pu8Data[u16Idx] << 8);
        for (u8Bit = 0; u8Bit < 8; u8Bit++) {
            if (u16Crc & 0x8000) u16Crc = (uint16_t)((u16Crc << 1) ^ 0x1021);
            else u16Crc <<= 1;
        }
    }
    return u16Crc;
}

int main()
{

    // Serial object
    serialib serial;

    // Connection to serial port
    char errorOpening = serial.openDevice(SERIAL_PORT, 115200);


    // If connection fails, return the error code otherwise, display a success message
    if (errorOpening != 1) return errorOpening;
    printf("Successful connection to %s\n", SERIAL_PORT);


    char acFrame[64];
    uint16_t u16Idx = 0;
    uint8_t au8Hdr[2];
    uint8_t u8HdrCrc;

    uint16_t u16PayloadCrc;

    au8Hdr[1] = 0;
    
    au8Hdr[0] = 0x08;
    uint8_t au8Payload[] = {'K','e','n','d','e','0'};
    au8Hdr[1] = sizeof(au8Payload);

    /*uint8_t au8Payload[20];
    *((uint32_t*)(&au8Payload[0])) = (uint32_t)3;
    *((uint32_t*)(&au8Payload[4])) = (uint32_t)20;
    *((uint32_t*)(&au8Payload[8])) = (uint32_t)40;
    *((uint32_t*)(&au8Payload[12])) = (uint32_t)155;
    *((uint32_t*)(&au8Payload[16])) = (uint32_t)1000;*/


    u8HdrCrc = u8CRC8(au8Hdr, 2);
    u16PayloadCrc = u16CRC16(au8Payload, au8Hdr[1]);

    acFrame[u16Idx++] = '!';
    vByte2Hex(au8Hdr[0], &acFrame[u16Idx]); u16Idx += 2;
    vByte2Hex(au8Hdr[1], &acFrame[u16Idx]); u16Idx += 2;
    vByte2Hex(u8HdrCrc, &acFrame[u16Idx]);  u16Idx += 2;
    for (int i = 0; i < au8Hdr[1]; i++) {
        vByte2Hex(au8Payload[i], &acFrame[u16Idx]); u16Idx += 2;
    }
    vU162Hex(u16PayloadCrc, &acFrame[u16Idx]); u16Idx += 4;
    acFrame[u16Idx++] = '\n';
    acFrame[u16Idx] = '\0';


    // Create the string
    char buffer[64] = "!0B019000F0E1\n";

    // Write the string on the serial device
    serial.writeString(acFrame);
    printf("String sent: %s", acFrame);

    // Read the string
    int bytesRead = serial.readString(buffer, '\n', 64, 1000);
    printf("String read: \"%s\"", buffer);
    printf("Last byte: %02x\n", buffer[bytesRead - 1]);

    // Close the serial device
    serial.closeDevice();

    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
