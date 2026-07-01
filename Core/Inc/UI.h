/*
 * UI.h
 *
 *  Created on: Jun 28, 2026
 *      Author: kuruc
 */

#ifndef INC_UI_H_
#define INC_UI_H_

#include "main.h"

void vUI_Init(void);
void vUI_Process(void);
void vUI_ReadSwitch(uint8_t* pu8Switch);
void vWriteLed(const uint8_t u8Leds);
void vUI_WriteRelay(const bool boOn);
bool boUI_ReadRelay(void);


#endif /* INC_UI_H_ */
