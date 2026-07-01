/*
 * CDC.c
 *
 *  Created on: Jun 28, 2026
 *      Author: kuruc
 */

#include "CDC.h"
#include <string.h>
#include "OneWireCD.h"
#include "UI.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"


#define CDC_PROTO_RX_BUFFER_SIZE      (256)
#define CDC_PROTO_TX_BUFFER_SIZE      (256)
#define CDC_PROTO_MAX_PAYLOAD         (64)
#define CDC_PROTO_FRAME_TIMEOUT_MS    (100)
#define CDC_PROTO_START_CHAR          ('!')
#define CDC_PROTO_END_CHAR            ('\n')


typedef enum {
  CMD_READ_MEMORY     = 0x01,
  CMD_WRITE_MEMORY    = 0x02,
  CMD_READ_NAME       = 0x03,
  CMD_WRITE_NAME      = 0x04,
  CMD_READ_SETTINGS   = 0x05,
  CMD_WRITE_SETTINGS  = 0x06,
  CMD_READ_ID         = 0x07,
  CMD_WRITE_ID        = 0x08,
  CMD_READ_SWITCH     = 0x09,
  CMD_READ_ACTIVE     = 0x0A,
  CMD_WRITE_ACTIVE    = 0x0B,

  CMD_READ_RELAY      = 0x0E,
  CMD_WRITE_RELAY     = 0x0F,

  CMD_EASTER_EGG      = 0xEE,

  CMD_PING            = 0x7F,

  CMD_ACK             = 0xF0,
  CMD_NAK             = 0xF1
} E_CDC_PROTO_CMD;

typedef enum {
  ERR_OK                  = 0x00,
  ERR_BAD_START           = 0x01,
  ERR_BAD_HEX             = 0x02,
  ERR_BAD_HEADER_CRC      = 0x03,
  ERR_BAD_PAYLOAD_CRC     = 0x04,
  ERR_LENGTH_MISMATCH     = 0x05,
  ERR_TIMEOUT             = 0x06,
  ERR_UNKNOWN_CMD         = 0x07,
  ERR_INVALID_ARG         = 0x08,
  ERR_INTERNAL            = 0x09,
  ERR_BUFFER_OVERFLOW     = 0x0A,
  ERR_BUSY                = 0x0B
} E_CDC_PROTO_ERROR;

// Variables
static uint8_t au8CdcProtoRxBuffer[CDC_PROTO_RX_BUFFER_SIZE];
static uint16_t u16CdcProtoRxLen;
static uint32_t u32CdcProtoRxStartTick;
static bool boRxInFrame;

static const char acHex[17] = "0123456789ABCDEF";

// Helpers
static bool boCh2U8(char chD, uint8_t* pu8D){
  if(chD<'0')return false;
  if(chD<='9'){*pu8D = chD-'0';return true;}
  if(chD<'A')return false;
  if(chD<='F'){*pu8D = chD-'A'+10;return true;}
  if(chD<'a')return false;
  if(chD<='f'){*pu8D = chD-'a'+10;return true;}
  return false;
}

static bool boHex2Byte(char chH, char chL, uint8_t* pu8Byte) {
  if (pu8Byte == NULL) return false;
  uint8_t u8H, u8L;
  if ((boCh2U8(chH,&u8H) == false) || (boCh2U8(chL,&u8L) == false)) return false;
  *pu8Byte = (uint8_t)((u8H << 4) | u8L);
  return true;
}

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
      else              u8Crc <<= 1;
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
      else                 u16Crc <<= 1;
    }
  }
  return u16Crc;
}

static void vShiftRxBuffer(uint16_t u16Count) {
  if (u16Count >= u16CdcProtoRxLen) {
    u16CdcProtoRxLen = 0;
    return;
  }
  memmove(au8CdcProtoRxBuffer, &au8CdcProtoRxBuffer[u16Count], u16CdcProtoRxLen - u16Count);
  u16CdcProtoRxLen -= u16Count;
}

static bool boFindCharInRxBuffer(char chChar, uint16_t* pu16Index) {
  for (*pu16Index = 0; *pu16Index < u16CdcProtoRxLen; (*pu16Index)++) {
    if (au8CdcProtoRxBuffer[*pu16Index] == chChar) return true;
  }
  return false;
}

static void vCdcTransmitBlocking(uint8_t* pu8Data, uint16_t u16Len) {
  uint32_t u32Start = HAL_GetTick();

  do {
    if (CDC_Transmit_FS(pu8Data, u16Len) == USBD_OK) return;
  } while ((HAL_GetTick() - u32Start) < 20);
}

static void vSendFrame(uint8_t u8Cmd, const uint8_t* pu8Payload, uint8_t u8PayloadLen) {
  char acTx[CDC_PROTO_TX_BUFFER_SIZE];
  uint16_t u16Idx = 0;
  uint8_t au8Hdr[2];
  uint8_t u8HdrCrc;
  uint16_t u16MsgCrc;
  uint8_t u8Idx;

  if (u8PayloadLen > CDC_PROTO_MAX_PAYLOAD) return;

  au8Hdr[0] = u8Cmd;
  au8Hdr[1] = u8PayloadLen;
  u8HdrCrc = u8CRC8(au8Hdr, 2);

  u16MsgCrc = u16CRC16(pu8Payload, (uint16_t)(u8PayloadLen));

  acTx[u16Idx++] = CDC_PROTO_START_CHAR;
  vByte2Hex(u8Cmd, &acTx[u16Idx]);       u16Idx += 2;
  vByte2Hex(u8PayloadLen, &acTx[u16Idx]);u16Idx += 2;
  vByte2Hex(u8HdrCrc, &acTx[u16Idx]);    u16Idx += 2;

  for (u8Idx = 0; u8Idx < u8PayloadLen; u8Idx++) {
    vByte2Hex(pu8Payload[u8Idx], &acTx[u16Idx]);
    u16Idx += 2;
  }

  vU162Hex(u16MsgCrc, &acTx[u16Idx]);    u16Idx += 4;
  acTx[u16Idx++] = CDC_PROTO_END_CHAR;

  vCdcTransmitBlocking((uint8_t*)acTx, u16Idx);
}

static void vSendAck(uint8_t u8OrigCmd) {
  uint8_t au8Payload[2];
  au8Payload[0] = u8OrigCmd;
  au8Payload[1] = ERR_OK;
  vSendFrame(CMD_ACK, au8Payload, 2);
}

static void vSendNak(uint8_t u8OrigCmd, E_CDC_PROTO_ERROR eError) {
  uint8_t au8Payload[2];
  au8Payload[0] = u8OrigCmd;
  au8Payload[1] = (uint8_t)eError;
  vSendFrame(CMD_NAK, au8Payload, 2);
}

// Command dispatcher
static void vHandleCommand(uint8_t u8Cmd, const uint8_t* pu8Payload, uint8_t u8PayloadLen) {
  uint8_t au8RespPayload[CDC_PROTO_MAX_PAYLOAD];
  uint8_t u8Instance;
  uint8_t u8Offset;
  uint8_t u8Len;

  switch ((E_CDC_PROTO_CMD)u8Cmd) {
    case CMD_PING:
      au8RespPayload[0] = 0x01;
      au8RespPayload[1] = 0x00;
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, 2);
      break;

    case CMD_READ_MEMORY:
      if (u8PayloadLen != 3) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      u8Instance = pu8Payload[0];
      u8Offset   = pu8Payload[1];
      u8Len      = pu8Payload[2];
      if ((u8Instance >= 8) || (u8Offset+u8Len > 144) || (u8Len == 0) || (u8Len > CDC_PROTO_MAX_PAYLOAD)) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      if (boOWCD_ReadMemory(u8Instance, u8Offset, &au8RespPayload[0], u8Len) == false) {
        vSendNak(u8Cmd, ERR_INTERNAL);
        break;
      }
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, u8Len);
      break;

    case CMD_WRITE_MEMORY:
      if (u8PayloadLen < 3) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      u8Instance = pu8Payload[0];
      u8Offset   = pu8Payload[1];
      u8Len      = pu8Payload[2];
      if ((u8Instance >= 8) || (u8Offset+u8Len > 144) || (u8Len == 0) || (u8Len > CDC_PROTO_MAX_PAYLOAD)) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      if ((u8PayloadLen != (uint8_t)(3 + u8Len))) {
        vSendNak(u8Cmd, ERR_LENGTH_MISMATCH);
        break;
      }
      if (boOWCD_WriteMemory(u8Instance, u8Offset, (uint8_t*)&pu8Payload[3], u8Len) == false) {
        vSendNak(u8Cmd, ERR_INTERNAL);
        break;
      }
      vSendAck(u8Cmd);
      break;

    case CMD_READ_NAME:
      if (u8PayloadLen != 1) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      u8Instance = pu8Payload[0];
      if (u8Instance >= 8) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      if (boOWCD_ReadName(u8Instance, (char*)au8RespPayload, &u8Len) == false) {
        vSendNak(u8Cmd, ERR_INTERNAL);
        break;
      }
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, u8Len);
      break;

    case CMD_WRITE_NAME:
      if (u8PayloadLen <= 2) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      u8Instance = pu8Payload[0];
      if (u8Instance >= 8) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      u8Len      = pu8Payload[1];
      if ((u8Len > 32) || (u8PayloadLen != (uint8_t)(2 + u8Len))) {
        vSendNak(u8Cmd, ERR_LENGTH_MISMATCH);
        break;
      }
      if (boOWCD_WriteName(u8Instance, (char*)&pu8Payload[2], u8Len) == false) {
        vSendNak(u8Cmd, ERR_INTERNAL);
        break;
      }
      vSendAck(u8Cmd);
      break;

    case CMD_READ_SETTINGS:
      if (u8PayloadLen != 0) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      if (boOWCD_ReadSettings((uint32_t*)&au8RespPayload[0], (uint32_t*)&au8RespPayload[4], (uint32_t*)&au8RespPayload[8], (uint32_t*)&au8RespPayload[12], (uint32_t*)&au8RespPayload[16]) == false) {
        vSendNak(u8Cmd, ERR_INTERNAL);
        break;
      }
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, 20);
      break;

    case CMD_WRITE_SETTINGS:
      if (u8PayloadLen != 20) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      vOWCD_WriteSettings(*(uint32_t*)&pu8Payload[0], *(uint32_t*)&pu8Payload[4], *(uint32_t*)&pu8Payload[8], *(uint32_t*)&pu8Payload[12], *(uint32_t*)&pu8Payload[16]);
      vSendAck(u8Cmd);
      break;

    case CMD_READ_ID:
      if (u8PayloadLen > 0) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      vOWCD_ReadID(au8RespPayload);
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, 6);
      break;

    case CMD_WRITE_ID:
      if (u8PayloadLen != 6) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      vOWCD_WriteID(pu8Payload);
      vSendAck(u8Cmd);
      break;

    case CMD_READ_SWITCH:
      if (u8PayloadLen != 0) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      vUI_ReadSwitch(au8RespPayload);
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, 1);
      break;

    case CMD_READ_ACTIVE:
      if (u8PayloadLen != 0) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      vOWCD_ReadActive(au8RespPayload);
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, 1);
      break;

    case CMD_WRITE_ACTIVE:
      if ((u8PayloadLen != 1) || (pu8Payload[0]>=8)) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      if (boOWCD_WriteActive(pu8Payload[0]) == false){
        vSendNak(u8Cmd, ERR_INTERNAL);
      }
      vSendAck(u8Cmd);
      break;

    case CMD_READ_RELAY:
      if (u8PayloadLen != 0) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      au8RespPayload[0] = boUI_ReadRelay()?0x01:0x00;
      vSendFrame((uint8_t)(u8Cmd | 0x80), au8RespPayload, 1);
      break;

    case CMD_WRITE_RELAY:
      if ((u8PayloadLen != 1) || (pu8Payload[0]>=2)) {
        vSendNak(u8Cmd, ERR_INVALID_ARG);
        break;
      }
      vUI_WriteRelay(pu8Payload[0] == 0x01);
      vSendAck(u8Cmd);
      break;

    default:
      vSendNak(u8Cmd, ERR_UNKNOWN_CMD);
      break;
  }
}

// Frame parser
static void vCDC_ParseBuffer(void) {
  uint16_t u16StartIdx;
  uint16_t u16EndIdx;
  uint16_t u16FrameLen;
  uint8_t u8Cmd;
  uint8_t u8PayloadLen;
  uint8_t u8HeaderCrcRx;
  uint16_t u16PayloadCrcRx;
  uint8_t au8Payload[CDC_PROTO_MAX_PAYLOAD];
  uint8_t au8CrcBuf[2];
  uint16_t u16Idx;
  bool boExit;
  uint8_t u8Tmp;


  while (1) {
    if (boFindCharInRxBuffer(CDC_PROTO_START_CHAR, &u16StartIdx) == false) {
      u16CdcProtoRxLen = 0;
      boRxInFrame = false;
      return;
    }

    if (u16StartIdx > 0) {
      vShiftRxBuffer((uint16_t)u16StartIdx);
    }

    if (u16CdcProtoRxLen < 8) {
      boRxInFrame = true;
      return;
    }

    if ((boHex2Byte(au8CdcProtoRxBuffer[1], au8CdcProtoRxBuffer[2], &u8Cmd) == false) ||
        (boHex2Byte(au8CdcProtoRxBuffer[3], au8CdcProtoRxBuffer[4], &u8PayloadLen) == false) ||
        (boHex2Byte(au8CdcProtoRxBuffer[5], au8CdcProtoRxBuffer[6], &u8HeaderCrcRx) == false)) {
      vSendNak(0x00, ERR_BAD_HEX);
      vShiftRxBuffer(1);
      continue;
    }

    if (u8PayloadLen > CDC_PROTO_MAX_PAYLOAD) {
      vSendNak(u8Cmd, ERR_LENGTH_MISMATCH);
      vShiftRxBuffer(1);
      continue;
    }

    au8CrcBuf[0] = u8Cmd;
    au8CrcBuf[1] = u8PayloadLen;
    if (u8CRC8(au8CrcBuf, 2) != u8HeaderCrcRx) {
      vSendNak(u8Cmd, ERR_BAD_HEADER_CRC);
      vShiftRxBuffer(1);
      continue;
    }

    //                      '!' CC  LL  HH          DATA        PPPP '\n'
    u16FrameLen = (uint16_t)(1 + 2 + 2 + 2 + (u8PayloadLen * 2) + 4 + 1);

    if (u16CdcProtoRxLen < u16FrameLen) {
      boRxInFrame = true;
      return;
    }

    u16EndIdx = (u16FrameLen - 1);
    if (au8CdcProtoRxBuffer[u16EndIdx] != CDC_PROTO_END_CHAR) {
      vSendNak(u8Cmd, ERR_LENGTH_MISMATCH);
      vShiftRxBuffer(1);
      continue;
    }
    u16Idx = 0;
    boExit = false;
    while ((u16Idx < u8PayloadLen) && (boExit == false)) {
      if (boHex2Byte(au8CdcProtoRxBuffer[7 + u16Idx*2], au8CdcProtoRxBuffer[8 + u16Idx*2], &au8Payload[u16Idx]) == false) {
        vSendNak(u8Cmd, ERR_BAD_HEX);
        vShiftRxBuffer(1);
        boExit = true;
      }
      u16Idx++;
    }
    if(boExit == true){
      continue;
    }

    if (boHex2Byte(au8CdcProtoRxBuffer[7 + u8PayloadLen * 2], au8CdcProtoRxBuffer[8 + u8PayloadLen * 2], &u8Tmp) == false) {
      vSendNak(u8Cmd, ERR_BAD_HEX);
      vShiftRxBuffer(1);
      continue;
    }
    u16PayloadCrcRx = (uint16_t)u8Tmp;

    if (boHex2Byte(au8CdcProtoRxBuffer[9 + u8PayloadLen * 2], au8CdcProtoRxBuffer[10 + u8PayloadLen * 2], &u8Tmp) == false) {
      vSendNak(u8Cmd, ERR_BAD_HEX);
      vShiftRxBuffer(1);
      continue;
    }
    u16PayloadCrcRx |= ((uint16_t)u8Tmp << 8);

    if (u16CRC16(au8Payload, (uint16_t)(u8PayloadLen)) != u16PayloadCrcRx) {
      vSendNak(u8Cmd, ERR_BAD_PAYLOAD_CRC);
      vShiftRxBuffer(u16FrameLen);
      continue;
    }

    vHandleCommand(u8Cmd, au8Payload, u8PayloadLen);
    vShiftRxBuffer(u16FrameLen);
    boRxInFrame = false;
  }
}

// API

void vCDC_Init(void) {
  memset(au8CdcProtoRxBuffer, 0, CDC_PROTO_RX_BUFFER_SIZE);
  u16CdcProtoRxLen = 0;
  u32CdcProtoRxStartTick = 0;
  boRxInFrame = false;
}

void vCDC_Process(void) {
  if (boRxInFrame == true) {
    if ((HAL_GetTick() - u32CdcProtoRxStartTick) > CDC_PROTO_FRAME_TIMEOUT_MS) {
      vSendNak(0x00, ERR_TIMEOUT);
      u16CdcProtoRxLen = 0;
      boRxInFrame = false;
    }
  }
}

void vCDC_Rx(const uint8_t* pu8Buf, const uint32_t u32Len) {
  uint32_t u32Idx;

  if ((pu8Buf == NULL) || (u32Len == 0)) return;

  for (u32Idx = 0; u32Idx < u32Len; u32Idx++) {
    if (u16CdcProtoRxLen >= CDC_PROTO_RX_BUFFER_SIZE) {
      vSendNak(0x00, ERR_BUFFER_OVERFLOW);
      u16CdcProtoRxLen = 0;
      boRxInFrame = false;
      return;
    }

    if ((pu8Buf[u32Idx] == CDC_PROTO_START_CHAR) && (boRxInFrame == false)) {
      u32CdcProtoRxStartTick = HAL_GetTick();
      boRxInFrame = true;
    }

    au8CdcProtoRxBuffer[u16CdcProtoRxLen++] = pu8Buf[u32Idx];
  }

  vCDC_ParseBuffer();
}



