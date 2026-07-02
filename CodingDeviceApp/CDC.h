#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "serialib.h"

class CDC
{
public:
    CDC();
    ~CDC();

    bool Connect(const char* portName, uint32_t baudrate = 115200);
    void Disconnect();
    bool IsConnected() const;

    bool Ping();

    bool ReadName(uint8_t instance, std::string& outName);
    bool WriteName(uint8_t instance, const std::string& name);

    bool ReadMemory(uint8_t instance, uint8_t offset, uint8_t len, std::vector<uint8_t>& outData);
    bool WriteMemory(uint8_t instance, uint8_t offset, const std::vector<uint8_t>& data);

    bool ReadActive(uint8_t& outActive);
    bool WriteActive(uint8_t active);

    bool ReadRelay(uint8_t& outRelay);
    bool WriteRelay(uint8_t relay);

    bool ReadSwitch(uint8_t& outSwitch);

    bool ReadID(std::vector<uint8_t>& outId6);
    bool WriteID(const std::vector<uint8_t>& id6);

    bool ReadSettings(uint32_t& read1Time,
        uint32_t& read0Time,
        uint32_t& resetWait,
        uint32_t& presenceTime,
        uint32_t& timeOut);

    bool WriteSettings(uint32_t read1Time,
        uint32_t read0Time,
        uint32_t resetWait,
        uint32_t presenceTime,
        uint32_t timeOut);

    uint8_t GetLastError() const;
    std::string GetLastErrorText() const;

private:
    struct Frame
    {
        uint8_t cmd = 0;
        std::vector<uint8_t> payload;
    };

    serialib m_serial;
    bool m_connected;
    uint8_t m_lastError;
    std::string m_lastErrorText;

    bool WriteRaw(const std::string& data);
    bool ReadLine(std::string& outLine, uint32_t timeoutMs);
    bool Transact(uint8_t cmd,
        const std::vector<uint8_t>& payload,
        Frame& reply,
        uint32_t timeoutMs = 300);

    bool ParseFrame(const std::string& line, Frame& frame);
    std::string BuildFrame(uint8_t cmd, const std::vector<uint8_t>& payload) const;

    void SetError(uint8_t err, const std::string& txt);

    static uint8_t CRC8(const uint8_t* data, uint16_t len);
    static uint16_t CRC16(const uint8_t* data, uint16_t len);
    static bool HexToByte(char hi, char lo, uint8_t& out);
    static char NibbleToHex(uint8_t n);
    static void AppendByteHex(std::string& s, uint8_t v);
    static void AppendU16HexLE(std::string& s, uint16_t v);
    static uint32_t ReadU32LE(const uint8_t* p);
    static void PushU32LE(std::vector<uint8_t>& v, uint32_t x);
};