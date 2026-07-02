#include "CDC.h"

#include <windows.h>
#include <sstream>
#include <algorithm>

enum E_CDC_PROTO_CMD : uint8_t
{
    CMD_READ_MEMORY = 0x01,
    CMD_WRITE_MEMORY = 0x02,
    CMD_READ_NAME = 0x03,
    CMD_WRITE_NAME = 0x04,
    CMD_READ_SETTINGS = 0x05,
    CMD_WRITE_SETTINGS = 0x06,
    CMD_READ_ID = 0x07,
    CMD_WRITE_ID = 0x08,
    CMD_READ_SWITCH = 0x09,
    CMD_READ_ACTIVE = 0x0A,
    CMD_WRITE_ACTIVE = 0x0B,
    CMD_READ_RELAY = 0x0E,
    CMD_WRITE_RELAY = 0x0F,
    CMD_PING = 0x7F,
    CMD_ACK = 0xF0,
    CMD_NAK = 0xF1
};

enum E_CDC_PROTO_ERROR : uint8_t
{
    ERR_OK = 0x00,
    ERR_BAD_START = 0x01,
    ERR_BAD_HEX = 0x02,
    ERR_BAD_HEADER_CRC = 0x03,
    ERR_BAD_PAYLOAD_CRC = 0x04,
    ERR_LENGTH_MISMATCH = 0x05,
    ERR_TIMEOUT = 0x06,
    ERR_UNKNOWN_CMD = 0x07,
    ERR_INVALID_ARG = 0x08,
    ERR_INTERNAL = 0x09,
    ERR_BUFFER_OVERFLOW = 0x0A,
    ERR_BUSY = 0x0B
};

static constexpr uint8_t CDC_PROTO_MAX_PAYLOAD = 64;

CDC::CDC()
    : m_connected(false),
    m_lastError(ERR_OK)
{
}

CDC::~CDC()
{
    Disconnect();
}

bool CDC::Connect(const char* portName, uint32_t baudrate)
{
    Disconnect();

    if (portName == nullptr || portName[0] == '\0')
    {
        SetError(ERR_INVALID_ARG, "Invalid COM port name");
        return false;
    }

    int ret = m_serial.openDevice(portName, baudrate);
    if (ret < 0)
    {
        SetError(ERR_INTERNAL, "openDevice failed");
        return false;
    }

    m_connected = true;
    Sleep(50);

    if (!Ping())
    {
        Disconnect();
        return false;
    }

    SetError(ERR_OK, "");
    return true;
}

void CDC::Disconnect()
{
    if (m_connected)
    {
        m_serial.closeDevice();
        m_connected = false;
    }
}

bool CDC::IsConnected() const
{
    return m_connected;
}

uint8_t CDC::GetLastError() const
{
    return m_lastError;
}

std::string CDC::GetLastErrorText() const
{
    return m_lastErrorText;
}

void CDC::SetError(uint8_t err, const std::string& txt)
{
    m_lastError = err;
    m_lastErrorText = txt;
}

uint8_t CDC::CRC8(const uint8_t* data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b)
        {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
            else crc <<= 1;
        }
    }
    return crc;
}

uint16_t CDC::CRC16(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; ++i)
    {
        crc ^= (uint16_t)(data[i] << 8);
        for (uint8_t b = 0; b < 8; ++b)
        {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc <<= 1;
        }
    }
    return crc;
}

bool CDC::HexToByte(char hi, char lo, uint8_t& out)
{
    auto nib = [](char c, uint8_t& v)->bool
        {
            if (c >= '0' && c <= '9') { v = (uint8_t)(c - '0'); return true; }
            if (c >= 'A' && c <= 'F') { v = (uint8_t)(c - 'A' + 10); return true; }
            if (c >= 'a' && c <= 'f') { v = (uint8_t)(c - 'a' + 10); return true; }
            return false;
        };

    uint8_t h = 0, l = 0;
    if (!nib(hi, h) || !nib(lo, l)) return false;
    out = (uint8_t)((h << 4) | l);
    return true;
}

char CDC::NibbleToHex(uint8_t n)
{
    return (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
}

void CDC::AppendByteHex(std::string& s, uint8_t v)
{
    s.push_back(NibbleToHex((v >> 4) & 0x0F));
    s.push_back(NibbleToHex(v & 0x0F));
}

void CDC::AppendU16HexLE(std::string& s, uint16_t v)
{
    uint8_t lo = (uint8_t)(v & 0xFF);
    uint8_t hi = (uint8_t)((v >> 8) & 0xFF);
    AppendByteHex(s, lo);
    AppendByteHex(s, hi);
}

uint32_t CDC::ReadU32LE(const uint8_t* p)
{
    return ((uint32_t)p[0]) |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

void CDC::PushU32LE(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 24) & 0xFF));
}

std::string CDC::BuildFrame(uint8_t cmd, const std::vector<uint8_t>& payload) const
{
    std::string s;
    uint8_t hdr[2] = { cmd, (uint8_t)payload.size() };
    uint8_t hdrCrc = CRC8(hdr, 2);
    uint16_t payloadCrc = CRC16(payload.data(), (uint16_t)payload.size());

    s.push_back('!');
    AppendByteHex(s, cmd);
    AppendByteHex(s, (uint8_t)payload.size());
    AppendByteHex(s, hdrCrc);

    for (uint8_t b : payload)
        AppendByteHex(s, b);

    AppendU16HexLE(s, payloadCrc);
    s.push_back('\n');
    return s;
}

bool CDC::ParseFrame(const std::string& line, Frame& frame)
{
    if (line.size() < 12)
    {
        SetError(ERR_LENGTH_MISMATCH, "Frame too short");
        return false;
    }

    if (line.front() != '!' || line.back() != '\n')
    {
        SetError(ERR_BAD_START, "Invalid frame markers");
        return false;
    }

    uint8_t cmd = 0, payloadLen = 0, hdrCrcRx = 0;
    if (!HexToByte(line[1], line[2], cmd) ||
        !HexToByte(line[3], line[4], payloadLen) ||
        !HexToByte(line[5], line[6], hdrCrcRx))
    {
        SetError(ERR_BAD_HEX, "Header hex decode failed");
        return false;
    }

    size_t expectedLen = 1 + 2 + 2 + 2 + (payloadLen * 2) + 4 + 1;
    if (line.size() != expectedLen)
    {
        SetError(ERR_LENGTH_MISMATCH, "Frame length mismatch");
        return false;
    }

    uint8_t hdr[2] = { cmd, payloadLen };
    if (CRC8(hdr, 2) != hdrCrcRx)
    {
        SetError(ERR_BAD_HEADER_CRC, "Header CRC error");
        return false;
    }

    std::vector<uint8_t> payload;
    payload.resize(payloadLen);

    for (uint8_t i = 0; i < payloadLen; ++i)
    {
        if (!HexToByte(line[7 + i * 2], line[8 + i * 2], payload[i]))
        {
            SetError(ERR_BAD_HEX, "Payload hex decode failed");
            return false;
        }
    }

    uint8_t crcLo = 0, crcHi = 0;
    if (!HexToByte(line[7 + payloadLen * 2], line[8 + payloadLen * 2], crcLo) ||
        !HexToByte(line[9 + payloadLen * 2], line[10 + payloadLen * 2], crcHi))
    {
        SetError(ERR_BAD_HEX, "Payload CRC hex decode failed");
        return false;
    }

    uint16_t payloadCrcRx = (uint16_t)crcLo | ((uint16_t)crcHi << 8);
    uint16_t payloadCrc = CRC16(payload.data(), payloadLen);
    if (payloadCrc != payloadCrcRx)
    {
        SetError(ERR_BAD_PAYLOAD_CRC, "Payload CRC error");
        return false;
    }

    frame.cmd = cmd;
    frame.payload = payload;
    SetError(ERR_OK, "");
    return true;
}

bool CDC::WriteRaw(const std::string& data)
{
    if (!m_connected)
    {
        SetError(ERR_INTERNAL, "Not connected");
        return false;
    }

    int ret = m_serial.writeBytes((void*)data.data(), (unsigned int)data.size());
    if (ret < 0)
    {
        SetError(ERR_INTERNAL, "writeBytes failed");
        return false;
    }
    return true;
}

bool CDC::ReadLine(std::string& outLine, uint32_t timeoutMs)
{
    outLine.clear();

    DWORD t0 = GetTickCount();
    char ch = 0;

    while ((GetTickCount() - t0) < timeoutMs)
    {
        int ret = m_serial.readChar(&ch, 20);
        if (ret == 1)
        {
            outLine.push_back(ch);
            if (ch == '\n')
                return true;
        }
        else if (ret < 0)
        {
            SetError(ERR_INTERNAL, "readChar failed");
            return false;
        }
    }

    SetError(ERR_TIMEOUT, "Read timeout");
    return false;
}

bool CDC::Transact(uint8_t cmd, const std::vector<uint8_t>& payload, Frame& reply, uint32_t timeoutMs)
{
    if (!m_connected)
    {
        SetError(ERR_INTERNAL, "Not connected");
        return false;
    }

    std::string tx = BuildFrame(cmd, payload);
    if (!WriteRaw(tx))
        return false;

    std::string rx;
    if (!ReadLine(rx, timeoutMs))
        return false;

    if (!ParseFrame(rx, reply))
        return false;

    if (reply.cmd == CMD_NAK)
    {
        if (reply.payload.size() >= 2)
            SetError(reply.payload[1], "Device returned NAK");
        else
            SetError(ERR_INTERNAL, "Malformed NAK");
        return false;
    }

    if (reply.cmd == CMD_ACK)
    {
        if (reply.payload.size() >= 2)
        {
            if (reply.payload[0] != cmd || reply.payload[1] != ERR_OK)
            {
                SetError(ERR_INTERNAL, "ACK payload mismatch");
                return false;
            }
        }
        else
        {
            SetError(ERR_INTERNAL, "Malformed ACK");
            return false;
        }
        return true;
    }

    uint8_t expectedReplyCmd = (uint8_t)(cmd | 0x80);
    if (reply.cmd != expectedReplyCmd)
    {
        SetError(ERR_INTERNAL, "Unexpected reply command");
        return false;
    }

    SetError(ERR_OK, "");
    return true;
}

bool CDC::Ping()
{
    Frame r;
    std::vector<uint8_t> p;
    if (!Transact(CMD_PING, p, r, 300))
        return false;

    if (r.payload.size() != 2)
    {
        SetError(ERR_LENGTH_MISMATCH, "Ping payload size invalid");
        return false;
    }

    return true;
}

bool CDC::ReadName(uint8_t instance, std::string& outName)
{
    if (instance >= 8)
    {
        SetError(ERR_INVALID_ARG, "Invalid instance");
        return false;
    }

    Frame r;
    std::vector<uint8_t> p = { instance };
    if (!Transact(CMD_READ_NAME, p, r))
        return false;

    outName.assign(r.payload.begin(), r.payload.end());
    return true;
}

bool CDC::WriteName(uint8_t instance, const std::string& name)
{
    if (instance >= 8)
    {
        SetError(ERR_INVALID_ARG, "Invalid instance");
        return false;
    }

    std::string trimmed = name;
    if (trimmed.size() > 32)
        trimmed.resize(32);

    Frame r;
    std::vector<uint8_t> p;
    p.push_back(instance);
    p.push_back((uint8_t)trimmed.size());
    p.insert(p.end(), trimmed.begin(), trimmed.end());

    return Transact(CMD_WRITE_NAME, p, r);
}

bool CDC::ReadMemory(uint8_t instance, uint8_t offset, uint8_t len, std::vector<uint8_t>& outData)
{
    if (instance >= 8 || len == 0 || (uint16_t)offset + len > 144)
    {
        SetError(ERR_INVALID_ARG, "Invalid memory read args");
        return false;
    }

    outData.clear();
    uint8_t remaining = len;
    uint8_t currentOffset = offset;

    while (remaining > 0)
    {
        uint8_t chunk = (remaining > CDC_PROTO_MAX_PAYLOAD) ? CDC_PROTO_MAX_PAYLOAD : remaining;

        Frame r;
        std::vector<uint8_t> p = { instance, currentOffset, chunk };
        if (!Transact(CMD_READ_MEMORY, p, r))
            return false;

        if (r.payload.size() != chunk)
        {
            SetError(ERR_LENGTH_MISMATCH, "Memory read chunk size mismatch");
            return false;
        }

        outData.insert(outData.end(), r.payload.begin(), r.payload.end());
        currentOffset = (uint8_t)(currentOffset + chunk);
        remaining = (uint8_t)(remaining - chunk);
    }

    return true;
}

bool CDC::WriteMemory(uint8_t instance, uint8_t offset, const std::vector<uint8_t>& data)
{
    if (instance >= 8 || data.empty() || (uint16_t)offset + data.size() > 144)
    {
        SetError(ERR_INVALID_ARG, "Invalid memory write args");
        return false;
    }

    size_t pos = 0;
    uint8_t currentOffset = offset;

    while (pos < data.size())
    {
        uint8_t chunk = (uint8_t)std::min<size_t>(CDC_PROTO_MAX_PAYLOAD, data.size() - pos);

        Frame r;
        std::vector<uint8_t> p;
        p.push_back(instance);
        p.push_back(currentOffset);
        p.push_back(chunk);
        p.insert(p.end(), data.begin() + pos, data.begin() + pos + chunk);

        if (!Transact(CMD_WRITE_MEMORY, p, r))
            return false;

        currentOffset = (uint8_t)(currentOffset + chunk);
        pos += chunk;
    }

    return true;
}

bool CDC::ReadActive(uint8_t& outActive)
{
    Frame r;
    std::vector<uint8_t> p;
    if (!Transact(CMD_READ_ACTIVE, p, r))
        return false;

    if (r.payload.size() != 1 || r.payload[0] >= 8)
    {
        SetError(ERR_LENGTH_MISMATCH, "Invalid active payload");
        return false;
    }

    outActive = r.payload[0];
    return true;
}

bool CDC::WriteActive(uint8_t active)
{
    if (active >= 8)
    {
        SetError(ERR_INVALID_ARG, "Invalid active index");
        return false;
    }

    Frame r;
    std::vector<uint8_t> p = { active };
    return Transact(CMD_WRITE_ACTIVE, p, r);
}

bool CDC::ReadRelay(uint8_t& outRelay)
{
    Frame r;
    std::vector<uint8_t> p;
    if (!Transact(CMD_READ_RELAY, p, r))
        return false;

    if (r.payload.size() != 1)
    {
        SetError(ERR_LENGTH_MISMATCH, "Invalid relay payload");
        return false;
    }

    outRelay = r.payload[0];
    return true;
}

bool CDC::WriteRelay(uint8_t relay)
{
    if (relay > 1)
    {
        SetError(ERR_INVALID_ARG, "Invalid relay value");
        return false;
    }

    Frame r;
    std::vector<uint8_t> p = { relay };
    return Transact(CMD_WRITE_RELAY, p, r);
}

bool CDC::ReadSwitch(uint8_t& outSwitch)
{
    Frame r;
    std::vector<uint8_t> p;
    if (!Transact(CMD_READ_SWITCH, p, r))
        return false;

    if (r.payload.size() != 1)
    {
        SetError(ERR_LENGTH_MISMATCH, "Invalid switch payload");
        return false;
    }

    outSwitch = r.payload[0];
    return true;
}

bool CDC::ReadID(std::vector<uint8_t>& outId6)
{
    Frame r;
    std::vector<uint8_t> p;
    if (!Transact(CMD_READ_ID, p, r))
        return false;

    if (r.payload.size() != 6)
    {
        SetError(ERR_LENGTH_MISMATCH, "Invalid ID payload");
        return false;
    }

    outId6 = r.payload;
    return true;
}

bool CDC::WriteID(const std::vector<uint8_t>& id6)
{
    if (id6.size() != 6)
    {
        SetError(ERR_INVALID_ARG, "ID must be 6 bytes");
        return false;
    }

    Frame r;
    return Transact(CMD_WRITE_ID, id6, r);
}

bool CDC::ReadSettings(uint32_t& read1Time,
    uint32_t& read0Time,
    uint32_t& resetWait,
    uint32_t& presenceTime,
    uint32_t& timeOut)
{
    Frame r;
    std::vector<uint8_t> p;
    if (!Transact(CMD_READ_SETTINGS, p, r))
        return false;

    if (r.payload.size() != 20)
    {
        SetError(ERR_LENGTH_MISMATCH, "Invalid settings payload");
        return false;
    }

    read1Time = ReadU32LE(&r.payload[0]);
    read0Time = ReadU32LE(&r.payload[4]);
    resetWait = ReadU32LE(&r.payload[8]);
    presenceTime = ReadU32LE(&r.payload[12]);
    timeOut = ReadU32LE(&r.payload[16]);
    return true;
}

bool CDC::WriteSettings(uint32_t read1Time,
    uint32_t read0Time,
    uint32_t resetWait,
    uint32_t presenceTime,
    uint32_t timeOut)
{
    Frame r;
    std::vector<uint8_t> p;
    PushU32LE(p, read1Time);
    PushU32LE(p, read0Time);
    PushU32LE(p, resetWait);
    PushU32LE(p, presenceTime);
    PushU32LE(p, timeOut);

    return Transact(CMD_WRITE_SETTINGS, p, r);
}