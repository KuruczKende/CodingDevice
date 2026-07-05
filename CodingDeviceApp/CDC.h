#pragma once

#include <string>
#include <vector>
#include "serialib.h"

class CDC
{
public:
	struct Frame
	{
		uint8_t u8Cmd;
		uint8_t au8Payload[128];
		uint8_t u8Len;
		void push_back(uint8_t byte) {
			if (u8Len < sizeof(au8Payload)) {
				au8Payload[u8Len++] = byte;
			}
		}
	};

	CDC();
	~CDC();

	bool boConnect(uint8_t u8ComPort);
	bool boDisconnect();
	bool boTransact(const uint8_t u8Cmd, const std::vector<uint8_t>& vu8Payload, Frame& cReply, const uint32_t u32TimeoutMs = 1000);
	bool boReadMemory(uint8_t u8Id, uint16_t u16Offset, uint16_t u16Length, std::vector<uint8_t>& vu8Data);
	bool boWriteMemory(uint8_t u8Id, uint16_t u16Offset, const std::vector<uint8_t>& vu8Data);
	bool boReadName(uint8_t u8Id, std::string& strName);
	bool boWriteName(uint8_t u8Id, const std::string& strName);
	bool boReadActive(uint8_t& u8Active);
	bool boWriteActive(uint8_t u8Active);
	bool boReadRelay(uint8_t& u8Relay);
	bool boWriteRelay(uint8_t u8Relay);

private:
	serialib m_serial;
	bool m_connected;
	const char acHex[17] = "0123456789ABCDEF";
	uint8_t CRC8(const uint8_t* pu8Data, uint32_t u32Len) const;
	uint16_t CRC16(const uint8_t* pu8Data, uint32_t u32Len) const;
	void vByte2Hex(uint8_t u8Byte, char* pchOut) const;
	void vU162Hex(uint16_t u16Value, char* pchOut) const;
	bool boCh2U8(char chD, uint8_t* pu8D) const;
	bool boHex2Byte(char chH, char chL, uint8_t* pu8Byte) const;
	bool boBuildFrame(uint8_t u8Cmd, const std::vector<uint8_t>& vu8Payload, char* pcFrame, uint32_t* pu32FrameLen) const;
	bool boParseFrame(char* pcLine, Frame& cFrame) const;
};