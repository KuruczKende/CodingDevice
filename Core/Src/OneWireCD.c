/*
 * OneWireCD.c
 *
 *  Created on: Jun 26, 2026
 *      Author: kuruc
 */
#include "OneWireCD.h"
#include "gpio.h"
#include "tim.h"
#include "w25q_mem.h"
#include <string.h>

typedef enum{
  SEQ_END = 0,
  SEQ_RESET,
  SEQ_PRESENCE,
  SEQ_W,
  SEQ_R,
  SEQ_MAX
}E_SEQ_TYPE;

typedef enum{
  ST_IDLE = 0,
  ST_RESET,
  ST_ROM_CMD,
  ST_ROM_FUNC,
  ST_MEM_CMD,
  ST_MEM_FUNC,
  ST_MAX
}E_STATE;

typedef struct{
  uint8_t data[144];
  char name[33];
}C_CODING_DEVICE;

typedef union{
  uint64_t data;
  struct{
    uint8_t u8FamilyCode;
    uint8_t au8SerialNum[6];
    uint8_t u8CRC;
  }str;
}U_ID;
//U_ID uID={.str={.u8FamilyCode=0x2d, .au8SerialNum={0}, .u8CRC=0xd7}};

typedef struct{
  uint32_t u32Read1Time;//3
  uint32_t u32Read0Time;//20
  uint32_t u32ResetWait;//40
  uint32_t u32PresenceTime;//155
  uint32_t u32TimeOut;//1000
}C_SETTINGS;

union{
  uint8_t data;
  struct{
    uint8_t E : 3;
    uint8_t : 2;
    uint8_t PF : 1;
    uint8_t : 1;
    uint8_t AA : 1;
  }bitfield;
}uESreg;

typedef struct{
  U_ID uID;
  uint8_t u8ActiveCD;
  C_SETTINGS cSettings;
  C_CODING_DEVICE acCD[8];
}S_EEPROM_DATA;
S_EEPROM_DATA sWorkingCopy;

enum{
  E_SAVE_NONE = 0,
  E_SAVE_ID,
  E_SAVE_ACTIVE,
  E_SAVE_SETTINGS,
  E_SAVE_D0,
  E_SAVE_D1,
  E_SAVE_D2,
  E_SAVE_D3,
  E_SAVE_D4,
  E_SAVE_D5,
  E_SAVE_D6,
  E_SAVE_D7
}eSaveRequested;


#define PULL W1_OUT_GPIO_Port->BSRR = 0x00000001
#define RELE W1_OUT_GPIO_Port->BSRR = 0x00010000
#define EEPROM_DATA_ADDRESS       (0x00000100)


static E_STATE eState;
static E_SEQ_TYPE eSequence;
static uint16_t u16Len;
static uint16_t u16FuncVar;
static uint16_t u16Treg;
static void (*pvFunc)();
static uint8_t WriteReg;
static uint8_t ReadReg;
static bool RC_Flag;
static bool boProgramming;
static uint16_t u16MemAddr;
static uint16_t u16Crc;
static uint8_t au8ScratchPad[8];

// Helper functions
static uint16_t _CRC16(uint8_t cmd, bool boES){
    uint16_t crc = 0;

    static const uint8_t oddparity[16] = { 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 };

    uint16_t cdata = 0;

    cdata = cmd;
    cdata = (cdata ^ crc) & 0xff;
    crc >>= 8;
    if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) crc ^= 0xC001;
    cdata <<= 6;
    crc ^= cdata;
    cdata <<= 1;
    crc ^= cdata;

    cdata = (u16Treg)&0xff;
    cdata = (cdata ^ crc) & 0xff;
    crc >>= 8;
    if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) crc ^= 0xC001;
    cdata <<= 6;
    crc ^= cdata;
    cdata <<= 1;
    crc ^= cdata;

    cdata = (u16Treg>>8)&0xff;
    cdata = (cdata ^ crc) & 0xff;
    crc >>= 8;
    if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) crc ^= 0xC001;
    cdata <<= 6;
    crc ^= cdata;
    cdata <<= 1;
    crc ^= cdata;

    if(boES){
      cdata = (uESreg.data)&0xff;
      cdata = (cdata ^ crc) & 0xff;
      crc >>= 8;
      if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) crc ^= 0xC001;
      cdata <<= 6;
      crc ^= cdata;
      cdata <<= 1;
      crc ^= cdata;
    }

    for (uint8_t idx = u16Treg&0x0007;idx<=uESreg.bitfield.E;idx++) {
      cdata = au8ScratchPad[idx];
      cdata = (cdata ^ crc) & 0xff;
      crc >>= 8;
      if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) crc ^= 0xC001;
      cdata <<= 6;
      crc ^= cdata;
      cdata <<= 1;
      crc ^= cdata;
    }
    return ~crc;
}
static void updateCRC8(){
  uint8_t crc = 0xde;//cdc containing 0x2d
  for(uint8_t j=0;j<6;j++) {
    uint8_t inbyte = sWorkingCopy.uID.str.au8SerialNum[j];
    for (uint8_t i = 8; i; i--) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  sWorkingCopy.uID.str.u8CRC = crc;
}
// Rom functions
static void vFuncRomRead(){
  if(u16FuncVar<8){
    RC_Flag = false;
    ReadReg = (uint8_t)(sWorkingCopy.uID.data>>(u16FuncVar*8));
    eSequence = SEQ_R;
    u16Len=8;
  }
  else{
    eState = ST_MEM_CMD;
    eSequence = SEQ_W;
    u16Len=8;
  }
  u16FuncVar++;
}
static void vFuncRomMatch(){
  if(u16FuncVar==0){//first
    RC_Flag = false;
    eSequence = SEQ_W;
    u16Len=1;
  }
  else if( ((WriteReg>>7)&0x01) == ((sWorkingCopy.uID.data>>(u16FuncVar-1))&0x01) ){
    if(u16FuncVar==64){//finished
      RC_Flag = true;
      eState = ST_MEM_CMD;
      eSequence = SEQ_W;
      u16Len=8;
    }
    else{
      ReadReg = (((sWorkingCopy.uID.data>>u16FuncVar)&0x01)==0x01)?0x01:0x02;
      eSequence = SEQ_W;
      u16Len=1;
    }
  }
  else{
    eState=ST_IDLE;
    eSequence=SEQ_END;
  }
  u16FuncVar++;
}
static void vFuncRomSearch(){
  if(u16FuncVar==0){//first
    RC_Flag = false;
    ReadReg = ((sWorkingCopy.uID.data>>(u16FuncVar/2))&0x01)?0x01:0x02;
    eSequence = SEQ_R;
    u16Len=2;
  }
  else if((u16FuncVar%2)==1){//read
    eSequence = SEQ_W;
    u16Len=1;
  }
  else if(u16FuncVar==128){//finished
    RC_Flag = true;
    eState = ST_MEM_CMD;
    eSequence = SEQ_W;
    u16Len=8;
  }
  else if( ((WriteReg>>7)&0x01) == ((sWorkingCopy.uID.data>>(u16FuncVar/2-1))&0x01) ){//write with match
    ReadReg = (((sWorkingCopy.uID.data >> (u16FuncVar / 2)) & 0x01) == 0x01) ? 0x01 : 0x02;
    eSequence = SEQ_R;
    u16Len = 2;
  }
  else {
    eState = ST_IDLE;
    eSequence = SEQ_END;
  }
  u16FuncVar++;
}
static void vFuncRomSkip(){
  RC_Flag = false;
  eState = ST_MEM_CMD;
  eSequence = SEQ_W;
  u16Len=8;
}
static void vFuncRomResume(){
  if(RC_Flag){
    eState = ST_MEM_CMD;
    eSequence = SEQ_W;
    u16Len=8;
  }
  else{
    eState = ST_IDLE;
    eSequence = SEQ_END;
  }
}
// Memory functions
static void vFuncMemWriteScr(){
  if(u16FuncVar==0){//init
    eSequence = SEQ_W;
    u16Len=8;
  }
  else if(u16FuncVar==1){//read first byte
    u16Treg = ((uint16_t)WriteReg);
    eSequence = SEQ_W;
    u16Len=8;
  }
  else if(u16FuncVar==2){//read second byte
    u16Treg |= ((uint16_t)WriteReg)<<8;
    uESreg.data = 0;
    uESreg.bitfield.AA = 0;
    uESreg.bitfield.PF = 1;
    uESreg.bitfield.E = u16Treg&0x0007;
    eSequence = SEQ_W;
    u16Len=8;
  }
  else if(u16FuncVar==200){
    ReadReg = (u16Crc>>8)&0xff;
    eSequence = SEQ_R;
    u16Len=8;
  }
  else if(u16FuncVar==201){
    eState = ST_IDLE;
    eSequence = SEQ_END;
  }
  else{
    au8ScratchPad[uESreg.bitfield.E & 0x07] = WriteReg;
    if((uESreg.bitfield.E & 0x07)==0x07){
      if((u16Treg&0x07)==0){
        uESreg.bitfield.PF = 0;
      }
      u16Crc = _CRC16(0x0F,false);
      ReadReg = u16Crc&0xff;
      eSequence = SEQ_R;
      u16Len=8;
      u16FuncVar = 200-1;
    }
    else{
      uESreg.bitfield.E++;
      eSequence = SEQ_W;
      u16Len=8;
    }
  }
  u16FuncVar++;
}
static void vFuncMemReadScr(){
  if(u16FuncVar==0){//init
    ReadReg = u16Treg&0xff;
    eSequence = SEQ_R;
    u16Len=8;
  }
  else if(u16FuncVar==1){
    ReadReg = (u16Treg>>8)&0xff;
    eSequence = SEQ_R;
    u16Len=8;
  }
  else if(u16FuncVar==2){
    ReadReg = uESreg.data;
    eSequence = SEQ_R;
    u16Len=8;
  }
  else if(u16FuncVar==200){
    u16Crc = _CRC16(0xAA,true);
    ReadReg = u16Crc&0xff;
    eSequence = SEQ_R;
    u16Len=8;
  }
  else if(u16FuncVar==201){
    ReadReg = (u16Crc>>8)&0xff;
    eSequence = SEQ_R;
    u16Len=8;
  }
  else if(u16FuncVar==202){
    eState = ST_IDLE;
    eSequence = SEQ_END;
  }
  else{
    if(u16FuncVar==3){
      u16MemAddr = u16Treg&0x07;
    }
      ReadReg = au8ScratchPad[u16MemAddr];
      eSequence = SEQ_R;
      u16Len=8;
    if(u16MemAddr==uESreg.bitfield.E){
      u16FuncVar = 200-1;
    }
    else{
      u16MemAddr++;
    }
  }
  u16FuncVar++;
}
static void vFuncMemCopyScr(){
  if(u16FuncVar==0){//init
    eSequence = SEQ_W;
    u16Len=8;
  }
  else if(u16FuncVar==1){//read first byte
    if(WriteReg == (u16Treg&0xff)){
      eSequence = SEQ_W;
      u16Len=8;
    }
    else{
      eState = ST_IDLE;
      eSequence = SEQ_END;
    }
  }
  else if(u16FuncVar==2){//read second byte
    if(WriteReg == ((u16Treg>>8)&0xff)){
      eSequence = SEQ_W;
      u16Len=8;
    }
    else{
      eState = ST_IDLE;
      eSequence = SEQ_END;
    }
  }
  else if(u16FuncVar==3){//read es
    if((WriteReg == uESreg.data)&&(u16Treg<0x0090)&&(uESreg.bitfield.PF==0)){
      uESreg.bitfield.AA = 1;
      memcpy(&sWorkingCopy.acCD[sWorkingCopy.u8ActiveCD].data[u16Treg], au8ScratchPad, 8);
      boProgramming = true;
      ReadReg = 0xAA;
      eSequence = SEQ_R;
      u16Len=8;
    }
    else{
      eState = ST_IDLE;
      eSequence = SEQ_END;
    }
  }
  else{
    ReadReg = 0xAA;
    eSequence = SEQ_R;
    u16Len=8;
  }
  u16FuncVar++;
}
static void vFuncMemRead(){
  if(u16FuncVar==0){//init
    eSequence = SEQ_W;
    u16Len=8;
  }
  else if(u16FuncVar==1){//read first byte
    u16Treg = ((uint16_t)WriteReg);
    eSequence = SEQ_W;
    u16Len=8;
  }
  else{
    if(u16FuncVar==2){//read second byte
      u16Treg |= ((uint16_t)WriteReg)<<8;
      u16MemAddr = u16Treg;
    }
    if(u16MemAddr<0x0090){
      ReadReg = sWorkingCopy.acCD[sWorkingCopy.u8ActiveCD].data[u16MemAddr];
      u16MemAddr++;
      eSequence = SEQ_R;
      u16Len=8;
    }
    else{
      eState = ST_IDLE;
      eSequence = SEQ_END;
    }
  }
  u16FuncVar++;//can not overflow even if reading out all bytes(144)
}
// Base Logic
static inline void sequenceFinished(){
  switch(eState){
  case ST_ROM_CMD:
    eState = ST_ROM_FUNC;
    switch(WriteReg){
    case 0x33:pvFunc=vFuncRomRead;break;
    case 0x55:pvFunc=vFuncRomMatch;break;
    case 0xF0:pvFunc=vFuncRomSearch;break;
    case 0xCC:pvFunc=vFuncRomSkip;break;
    case 0xA5:pvFunc=vFuncRomResume;break;
    default:eState=ST_IDLE; pvFunc=NULL;break;
    }
    u16FuncVar = 0;
    if(pvFunc!=NULL) pvFunc();
    break;
  case ST_ROM_FUNC:
  case ST_MEM_FUNC:
    if(pvFunc!=NULL) pvFunc();
    break;
  case ST_MEM_CMD:
    eState = ST_MEM_FUNC;
    switch(WriteReg){
    case 0x0F:pvFunc=vFuncMemWriteScr;break;
    case 0xAA:pvFunc=vFuncMemReadScr;break;
    case 0x55:pvFunc=vFuncMemCopyScr;break;
    case 0xF0:pvFunc=vFuncMemRead;break;
    default:eState=ST_IDLE; pvFunc=NULL;break;
    }
    u16FuncVar = 0;
    if(pvFunc!=NULL) pvFunc();
    break;
  case ST_IDLE:
  default: pvFunc=NULL;break;
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
  if(W1_IN_GPIO_Port->IDR & 0x0002){
    //HIGH
    if(TIM2->CNT > 450){
      //reset
      boProgramming = false;
      eState = ST_RESET;
      eSequence = SEQ_RESET;
    }
    else{
      switch(eSequence){
      case SEQ_PRESENCE:
        eState = ST_ROM_CMD;
        eSequence = SEQ_W;
        u16Len = 8+1;
      case SEQ_W:
        WriteReg>>=1;
        WriteReg|=(TIM2->CNT>50)?0x00:0x80;
        u16Len--;
        if(u16Len==0){
          sequenceFinished();
        }
        break;
      case SEQ_R:
        ReadReg>>=1;
        u16Len--;
        if(u16Len==0){
          sequenceFinished();
        }
        break;
      default: break;
      }
    }
  }
  else{
    switch(eSequence){
    case SEQ_RESET:
      eSequence = SEQ_PRESENCE;
      break;
    case SEQ_R:
      PULL;
      break;
    default: break;
    }
  }
  TIM2->CNT = 0;
}

// Interface functions
void vOWCD_Init(){
  eState=ST_IDLE;
  eSequence=SEQ_END;
  RELE;
  // Read from EEPROM to Working Copy
  if(W25Q_ReadRaw((uint8_t*)&sWorkingCopy, sizeof(S_EEPROM_DATA), EEPROM_DATA_ADDRESS) != W25Q_OK)
      vM_SetError(ME_EEPROM_READ);

}

void vOWCD_Process(){
  if((boProgramming == false) && (TIM2->CNT>sWorkingCopy.cSettings.u32TimeOut)){
    eState=ST_IDLE;
    eSequence=SEQ_END;
    RELE;
  }
  else{
    HAL_NVIC_DisableIRQ(EXTI1_IRQn);
    switch(eSequence){
    case SEQ_RESET:
      if(TIM2->CNT>sWorkingCopy.cSettings.u32ResetWait) PULL;
      break;
    case SEQ_PRESENCE:
      if(TIM2->CNT>sWorkingCopy.cSettings.u32PresenceTime) RELE;
      break;
    case SEQ_W:
      //write has no time defined switch
      break;
    case SEQ_R:
      if(TIM2->CNT>(((ReadReg&0x01)!=0)?sWorkingCopy.cSettings.u32Read1Time:sWorkingCopy.cSettings.u32Read0Time)){
        RELE;
      }
      break;
    default: break;
    }
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
  }
}

bool boOWCD_WriteMemory(const uint8_t u8Instance, const uint8_t u8Offset, const uint8_t* pu8Data, const uint8_t u8Len){
  if(u8Instance > 7) return false;
  if(u8Offset + u8Len > 144) return false;
  memcpy(&sWorkingCopy.acCD[u8Instance].data[u8Offset], pu8Data, u8Len);
  uint32_t u32AddressOffset = ((uint8_t*)&sWorkingCopy.acCD[u8Instance].data[u8Offset]) - ((uint8_t*)&sWorkingCopy);
  if (W25Q_ProgramRaw(&sWorkingCopy.acCD[u8Instance].data[u8Offset], u8Len, EEPROM_DATA_ADDRESS + u32AddressOffset) != W25Q_OK){
    vM_SetError(ME_EEPROM_WRITE);
    return false;
  }
  return true;
}

bool boOWCD_ReadMemory(const uint8_t u8Instance, const uint8_t u8Offset, uint8_t* pu8Data, const uint8_t u8Len){
  if(u8Instance >= 8) return false;
  if(u8Offset + u8Len > 144) return false;
  memcpy(pu8Data, &sWorkingCopy.acCD[u8Instance].data[u8Offset], u8Len);
  return true;
}

bool boOWCD_WriteName(const uint8_t u8Instance, const char* pchName, const uint8_t u8Len){
  if(u8Instance >= 8) return false;
  strncpy(sWorkingCopy.acCD[u8Instance].name, pchName, u8Len);
  sWorkingCopy.acCD[u8Instance].name[u8Len] = '\0';
  uint32_t u32AddressOffset = ((uint8_t*)&sWorkingCopy.acCD[u8Instance].name) - ((uint8_t*)&sWorkingCopy);
  if (W25Q_ProgramRaw((uint8_t*)&sWorkingCopy.acCD[u8Instance].name, u8Len, EEPROM_DATA_ADDRESS + u32AddressOffset) != W25Q_OK){
    vM_SetError(ME_EEPROM_WRITE);
    return false;
  }
  return true;
}

bool boOWCD_ReadName(const uint8_t u8Instance, char* pchName, uint8_t* pu8Len){
  if(u8Instance >= 8) return false;
  *pu8Len = strlen(sWorkingCopy.acCD[u8Instance].name);
  strcpy(pchName, sWorkingCopy.acCD[u8Instance].name);
  return true;
}

void vOWCD_WriteSettings(const uint32_t u32Read1Time, const uint32_t u32Read0Time, const uint32_t u32ResetWait, const uint32_t u32PresenceTime, const uint32_t u32TimeOut){
  sWorkingCopy.cSettings.u32Read1Time = u32Read1Time;
  sWorkingCopy.cSettings.u32Read0Time = u32Read0Time;
  sWorkingCopy.cSettings.u32ResetWait = u32ResetWait;
  sWorkingCopy.cSettings.u32PresenceTime = u32PresenceTime;
  sWorkingCopy.cSettings.u32TimeOut = u32TimeOut;
  uint32_t u32AddressOffset = ((uint8_t*)&sWorkingCopy.cSettings) - ((uint8_t*)&sWorkingCopy);
  if (W25Q_ProgramRaw((uint8_t*)&sWorkingCopy.cSettings, sizeof(C_SETTINGS), EEPROM_DATA_ADDRESS + u32AddressOffset) != W25Q_OK){
    vM_SetError(ME_EEPROM_WRITE);
  }
}

bool boOWCD_ReadSettings(uint32_t* pu32Read1Time, uint32_t* pu32Read0Time, uint32_t* pu32ResetWait, uint32_t* pu32PresenceTime, uint32_t* pu32TimeOut){
  if (!pu32Read1Time || !pu32Read0Time || !pu32ResetWait || !pu32PresenceTime || !pu32TimeOut) return false;
  *pu32Read1Time = sWorkingCopy.cSettings.u32Read1Time;
  *pu32Read0Time = sWorkingCopy.cSettings.u32Read0Time;
  *pu32ResetWait = sWorkingCopy.cSettings.u32ResetWait;
  *pu32PresenceTime = sWorkingCopy.cSettings.u32PresenceTime;
  *pu32TimeOut = sWorkingCopy.cSettings.u32TimeOut;
  return true;
}

void vOWCD_WriteID(const uint8_t* pu8ID){
  memcpy(sWorkingCopy.uID.str.au8SerialNum, pu8ID, 6);
  updateCRC8();
  uint32_t u32AddressOffset = ((uint8_t*)&sWorkingCopy.uID.data) - ((uint8_t*)&sWorkingCopy);
  if (W25Q_ProgramRaw((uint8_t*)&sWorkingCopy.uID.data, sizeof(U_ID), EEPROM_DATA_ADDRESS + u32AddressOffset) != W25Q_OK){
    vM_SetError(ME_EEPROM_WRITE);
  }
}

void vOWCD_ReadID(uint8_t* pu8ID){
  memcpy(pu8ID, sWorkingCopy.uID.str.au8SerialNum, 6);
}

bool boOWCD_WriteActive(const uint8_t u8Active){
  if(u8Active >= 8) return false;
  sWorkingCopy.u8ActiveCD = u8Active;
  uint32_t u32AddressOffset = ((uint8_t*)&sWorkingCopy.u8ActiveCD) - ((uint8_t*)&sWorkingCopy);
  if (W25Q_ProgramRaw((uint8_t*)&sWorkingCopy.u8ActiveCD, 1, EEPROM_DATA_ADDRESS + u32AddressOffset) != W25Q_OK){
    vM_SetError(ME_EEPROM_WRITE);
    return false;
  }
  return true;
}

void vOWCD_ReadActive(uint8_t* pu8Active){
  *pu8Active = sWorkingCopy.u8ActiveCD;

}
