/*
 * OneWireCD.h
 *
 *  Created on: Jun 26, 2026
 *      Author: kuruc
 */

#ifndef INC_ONEWIRECD_H_
#define INC_ONEWIRECD_H_

#include "main.h"


void vOWCD_Init();
void vOWCD_Process();
bool boOWCD_WriteMemory(const uint8_t u8Instance, const uint8_t u8Offset, const uint8_t* pu8Data, const uint8_t u8Len);
bool boOWCD_ReadMemory(const uint8_t u8Instance, const uint8_t u8Offset, uint8_t* pu8Data, const uint8_t u8Len);
bool boOWCD_WriteName(const uint8_t u8Instance, const char* pchName, const uint8_t u8Len);
bool boOWCD_ReadName(const uint8_t u8Instance, char* pchName, uint8_t* pu8Len);
void vOWCD_WriteSettings(const uint32_t u32Read1Time, const uint32_t u32Read0Time, const uint32_t u32ResetWait, const uint32_t u32PresenceTime, const uint32_t u32TimeOut);
bool boOWCD_ReadSettings(uint32_t* pu32Read1Time, uint32_t* pu32Read0Time, uint32_t* pu32ResetWait, uint32_t* pu32PresenceTime, uint32_t* pu32TimeOut);
void vOWCD_WriteID(const uint8_t* pu8ID);
void vOWCD_ReadID(uint8_t* pu8ID);
void vOWCD_ReadActive(uint8_t* pu8Active);
bool boOWCD_WriteActive(const uint8_t u8Active);
#endif /* INC_ONEWIRECD_H_ */
