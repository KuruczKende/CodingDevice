/*
 * CDC.h
 *
 *  Created on: Jun 28, 2026
 *      Author: kuruc
 */

#ifndef INC_CDC_H_
#define INC_CDC_H_

#include "main.h"

void vCDC_Init(void);
void vCDC_Process(void);
void vCDC_Rx(const uint8_t* pu8Buf, const uint32_t u32Len);

#endif /* INC_CDC_H_ */
