#include "CDC.h"

CDC::CDC() : m_connected(false)
{
}
CDC::~CDC()
{
	boDisconnect();
}

bool CDC::boConnect(uint8_t u8ComPort)
{
	if (m_connected) {
		boDisconnect();
	}
	char portName[16];
	sprintf_s(portName, sizeof(portName), "\\\\.\\COM%u", u8ComPort);
	if (m_serial.openDevice(portName, 115200) != 1)
		return false;
	m_connected = true;
	return true;
}
bool CDC::boDisconnect()
{
	if (m_connected) {
		m_serial.closeDevice();
		m_connected = false;
	}
	return true;
}

bool CDC::boReadMemory(uint8_t u8Id, uint16_t u16Offset, uint16_t u16Length, std::vector<uint8_t>& vu8Data)
{
	if (u16Length == 0 || u16Length > 32)
		return false;
	std::vector<uint8_t> payload;
	payload.push_back(u8Id);
	payload.push_back(u16Offset);
	payload.push_back(u16Length);
	Frame reply;
	if (!boTransact(0x01, payload, reply, 1000))
		return false;
	vu8Data.insert(vu8Data.end(), reply.au8Payload, reply.au8Payload + u16Length);
	return true;
}

bool CDC::boWriteMemory(uint8_t u8Id, uint16_t u16Offset, const std::vector<uint8_t>& vu8Data)
{
	if (vu8Data.size() == 0 || vu8Data.size() > 32)
		return false;
	std::vector<uint8_t> payload;
	payload.push_back(u8Id);
	payload.push_back(u16Offset);
	payload.push_back(vu8Data.size());
	payload.insert(payload.end(), vu8Data.begin(), vu8Data.end());
	Frame reply;
	if (!boTransact(0x02, payload, reply, 1000))
		return false;
	return true;
}

bool CDC::boReadName(uint8_t u8Id, std::string& strName)
{
	std::vector<uint8_t> payload;
	payload.push_back(u8Id);
	Frame reply;
	if (!boTransact(0x03, payload, reply, 1000))
		return false;
	strName.assign(reply.au8Payload, reply.au8Payload + reply.u8Len);
	return true;
}

bool CDC::boWriteName(uint8_t u8Id, const std::string& strName)
{
	std::vector<uint8_t> data(strName.begin(), strName.end());
	if (data.size() > 32)
		data.resize(32);
	std::vector<uint8_t> payload;
	payload.push_back(u8Id);
	payload.push_back(data.size());
	payload.insert(payload.end(), data.begin(), data.end());
	Frame reply;
	if (!boTransact(0x04, payload, reply, 1000))
		return false;
	return true;
}

bool CDC::boReadActive(uint8_t& u8Active)
{
	std::vector<uint8_t> payload;
	Frame reply;
	if (!boTransact(0x0A, payload, reply, 1000))
		return false;
	u8Active = reply.au8Payload[0];
	return true;
}

bool CDC::boWriteActive(uint8_t u8Active)
{
	std::vector<uint8_t> payload;
	payload.push_back(u8Active);
	Frame reply;
	if (!boTransact(0x0B, payload, reply, 1000))
		return false;
	return true;
}

bool CDC::boReadRelay(uint8_t& u8Relay)
{
	std::vector<uint8_t> payload;
	Frame reply;
	if (!boTransact(0x0E, payload, reply, 1000))
		return false;
	u8Relay = reply.au8Payload[0];
	return true;
}

bool CDC::boWriteRelay(uint8_t u8Relay)
{
	std::vector<uint8_t> payload;
	payload.push_back(u8Relay);
	Frame reply;
	if (!boTransact(0x0F, payload, reply, 1000))
		return false;
	return true;
}

bool CDC::boTransact(const uint8_t u8Cmd, const std::vector<uint8_t>& vu8Payload, Frame& cReply, const uint32_t u32TimeoutMs)
{
	if (!m_connected)
		return false;
	char frame[1024];
	uint32_t frameLen = 0;
	if (!boBuildFrame(u8Cmd, vu8Payload, frame, &frameLen))
		return false;
	frame[frameLen] = '\0';
	if (m_serial.writeString(frame) != 1)
		return false;
	char response[1024];
	int bytesRead = m_serial.readString(response, '\n', sizeof(response), u32TimeoutMs);
	if (bytesRead <= 0)
		return false;
	if (!boParseFrame(response, cReply))
		return false;
	return true;
}

bool CDC::boBuildFrame(uint8_t u8Cmd, const std::vector<uint8_t>& vu8Payload, char* pcFrame, uint32_t* pu32FrameLen) const
{
	uint8_t au8CmdLen[2] = { u8Cmd, (uint8_t)vu8Payload.size() };
	uint8_t crc8 = CRC8(au8CmdLen, 2);
	uint16_t crc16 = CRC16(vu8Payload.data(), vu8Payload.size());

	*pu32FrameLen = 0;
	pcFrame[*pu32FrameLen] = '!';								*pu32FrameLen += 1;
	vByte2Hex(u8Cmd, &pcFrame[*pu32FrameLen]);					*pu32FrameLen += 2;
	vByte2Hex(vu8Payload.size(), &pcFrame[*pu32FrameLen]);		*pu32FrameLen += 2;
	vByte2Hex(crc8, &pcFrame[*pu32FrameLen]);					*pu32FrameLen += 2;

	for (uint8_t u8Idx = 0; u8Idx < vu8Payload.size(); u8Idx++) {
		vByte2Hex(vu8Payload[u8Idx], &pcFrame[*pu32FrameLen]);	*pu32FrameLen += 2;
	}

	vU162Hex(crc16, &pcFrame[*pu32FrameLen]);					*pu32FrameLen += 4;
	pcFrame[*pu32FrameLen] = '\n';								*pu32FrameLen += 1;
	pcFrame[*pu32FrameLen] = '\0';
	return true;
}

bool CDC::boParseFrame(char* pcLine, Frame& cFrame) const
{
	if (pcLine[0] != '!')
		return false;
	uint8_t u8Crc;
	uint16_t u16Crc;
	if (boHex2Byte(pcLine[1], pcLine[2], &cFrame.u8Cmd) == false)
		return false;
	if (boHex2Byte(pcLine[3], pcLine[4], &cFrame.u8Len) == false)
		return false;
	if (boHex2Byte(pcLine[5], pcLine[6], &u8Crc) == false)
		return false;

	uint8_t au8CmdLen[2] = { cFrame.u8Cmd, cFrame.u8Len };
	uint8_t u8ComputedCrc = CRC8(au8CmdLen, 2);
	if (u8ComputedCrc != u8Crc)
		return false;

	for (uint8_t u8Idx = 0; u8Idx < cFrame.u8Len; u8Idx++) {
		uint8_t u8Byte;
		if (boHex2Byte(pcLine[7 + u8Idx * 2], pcLine[7 + u8Idx * 2 + 1], &u8Byte) == false)
			return false;
		cFrame.au8Payload[u8Idx] = u8Byte;
	}

	if (boHex2Byte(pcLine[7 + cFrame.u8Len * 2], pcLine[7 + cFrame.u8Len * 2 + 1], (uint8_t*)&u16Crc) == false || boHex2Byte(pcLine[7 + cFrame.u8Len * 2 + 2], pcLine[7 + cFrame.u8Len * 2 + 3], ((uint8_t*)&u16Crc) + 1) == false)
		return false;
	uint16_t computedCrc16 = CRC16(cFrame.au8Payload, cFrame.u8Len);
	if (computedCrc16 != u16Crc)
		return false;
	return true;
}

uint8_t CDC::CRC8(const uint8_t* pu8Data, uint32_t u32Len) const {
	uint8_t u8Crc = 0x00;
	uint16_t u16Idx;
	uint8_t u8Bit;

	for (u16Idx = 0; u16Idx < u32Len; u16Idx++) {
		u8Crc ^= pu8Data[u16Idx];
		for (u8Bit = 0; u8Bit < 8; u8Bit++) {
			if (u8Crc & 0x80) u8Crc = (uint8_t)((u8Crc << 1) ^ 0x07);
			else              u8Crc <<= 1;
		}
	}
	return u8Crc;
}

uint16_t CDC::CRC16(const uint8_t* pu8Data, uint32_t u32Len) const {
	uint16_t u16Crc = 0xFFFF;
	uint16_t u16Idx;
	uint8_t u8Bit;

	for (u16Idx = 0; u16Idx < u32Len; u16Idx++) {
		u16Crc ^= ((uint16_t)pu8Data[u16Idx] << 8);
		for (u8Bit = 0; u8Bit < 8; u8Bit++) {
			if (u16Crc & 0x8000) u16Crc = (uint16_t)((u16Crc << 1) ^ 0x1021);
			else                 u16Crc <<= 1;
		}
	}
	return u16Crc;
}

void CDC::vByte2Hex(uint8_t u8Byte, char* pchOut) const {
	pchOut[0] = acHex[(u8Byte >> 4) & 0x0F];
	pchOut[1] = acHex[u8Byte & 0x0F];
}

void CDC::vU162Hex(uint16_t u16Value, char* pchOut) const {
	pchOut[2] = acHex[(u16Value >> 12) & 0x0F];
	pchOut[3] = acHex[(u16Value >> 8) & 0x0F];
	pchOut[0] = acHex[(u16Value >> 4) & 0x0F];
	pchOut[1] = acHex[u16Value & 0x0F];
}

bool CDC::boCh2U8(char chD, uint8_t* pu8D) const {
	if (chD < '0')return false;
	if (chD <= '9') { *pu8D = chD - '0'; return true; }
	if (chD < 'A')return false;
	if (chD <= 'F') { *pu8D = chD - 'A' + 10; return true; }
	if (chD < 'a')return false;
	if (chD <= 'f') { *pu8D = chD - 'a' + 10; return true; }
	return false;
}

bool CDC::boHex2Byte(char chH, char chL, uint8_t* pu8Byte) const {
	if (pu8Byte == NULL) return false;
	uint8_t u8H, u8L;
	if ((boCh2U8(chH, &u8H) == false) || (boCh2U8(chL, &u8L) == false)) return false;
	*pu8Byte = (uint8_t)((u8H << 4) | u8L);
	return true;
}