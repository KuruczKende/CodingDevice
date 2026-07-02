// CodingDevice.cpp
#pragma comment(lib, "OneCore.lib")

#include <windows.h>
#include <string>
#include <vector>
#include <stdint.h>
#include <algorithm>
#include <sstream>

#include "resource.h"
#include "CDC.h"

#define MAX_LOADSTRING 100
#define EN_ENTER 1000

#define ID_COMBO_COM 1001
#define ID_BTN_CONNECT 1002
#define ID_BTN_RELAY 1003

#define ID_CB_EDIT_NAME 1101
#define ID_CB_EDIT_ID 1102
#define ID_EDIT_NAME 1103
#define ID_CB_STYLE 1104
#define ID_BTN_READ 1105
#define ID_BTN_WRITE 1106
#define ID_PANEL_EDITOR 1107

#define ID_ACTIVE_NAME 1201
#define ID_ACTIVE_ID 1202

#define ID_EDIT_NAME_ENTER 1301
#define ID_PAGE_EDIT_BASE 2000

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING] = L"Coding Device Config";
WCHAR szWindowClass[MAX_LOADSTRING] = L"CodingDeviceClass";

HFONT hFontBold = NULL;
HBRUSH hbrYellow = NULL, hbrBlue = NULL, hbrRed = NULL, hbrPurple = NULL;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void BuildEditorPanel(HWND hParent);
void ClearEditorPanel(HWND panel);
void RefreshCombosFromCache(HWND hWnd);
bool ReadAllNames(HWND hWnd);
bool ReadActiveAndRelay(HWND hWnd);
bool ReadCurrentDevice(HWND hWnd);
bool WriteCurrentDevice(HWND hWnd);
void LoadEverythingFromDevice(HWND hWnd);

CDC g_cdc;

wchar_t deviceNames[8][33] = {
    L"DeviceA", L"DeviceB", L"DeviceC", L"DeviceD",
    L"DeviceE", L"DeviceF", L"DeviceG", L"DeviceH"
};

const wchar_t* deviceIDs[8] = {
    L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7"
};

uint8_t g_au8Memory[8][144] = {};
uint8_t g_u8Active = 0;
bool g_boRelayOpen = false;
bool g_boConnected = false;

WNDPROC g_OldEditProc = nullptr;

static void ShowError(HWND hWnd, const wchar_t* text)
{
    MessageBoxW(hWnd, text, L"Hiba", MB_OK | MB_ICONERROR);
}

static void ShowInfo(HWND hWnd, const wchar_t* text)
{
    MessageBoxW(hWnd, text, L"Info", MB_OK | MB_ICONINFORMATION);
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), NULL, 0, NULL, NULL);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], len, NULL, NULL);
    return out;
}

static int GetSelectedIndex(HWND hWnd, int comboId)
{
    return (int)SendMessageW(GetDlgItem(hWnd, comboId), CB_GETCURSEL, 0, 0);
}

static void SetSelectedIndex(HWND hWnd, int comboId, int idx)
{
    SendMessageW(GetDlgItem(hWnd, comboId), CB_SETCURSEL, idx, 0);
}

static std::wstring GetControlText(HWND hWnd, int ctrlId)
{
    wchar_t buf[1024] = { 0 };
    GetWindowTextW(GetDlgItem(hWnd, ctrlId), buf, _countof(buf));
    return buf;
}

static void SetControlText(HWND hWnd, int ctrlId, const wchar_t* text)
{
    SetWindowTextW(GetDlgItem(hWnd, ctrlId), text);
}

static std::wstring GetSelectedComPort(HWND hWnd)
{
    int sel = GetSelectedIndex(hWnd, ID_COMBO_COM);
    if (sel < 0) return L"";

    wchar_t buf[128] = { 0 };
    SendMessageW(GetDlgItem(hWnd, ID_COMBO_COM), CB_GETLBTEXT, sel, (LPARAM)buf);
    return buf;
}

static std::wstring FormatHexLine(const uint8_t* pData, int len)
{
    std::wstringstream ss;
    for (int i = 0; i < len; ++i)
    {
        wchar_t tmp[8];
        wsprintfW(tmp, L"%02X", pData[i]);
        ss << tmp;
        if (i + 1 < len) ss << L" ";
    }
    return ss.str();
}

static bool ParseHexLine(const std::wstring& ws, std::vector<uint8_t>& out, int maxCount)
{
    out.clear();
    std::wstringstream ss(ws);
    std::wstring token;

    while (ss >> token)
    {
        if ((int)out.size() >= maxCount) break;
        unsigned int v = 0;
        if (swscanf_s(token.c_str(), L"%x", &v) != 1)
            return false;
        out.push_back((uint8_t)(v & 0xFF));
    }
    return true;
}

static void UpdateRelayButton(HWND hWnd)
{
    SetWindowTextW(GetDlgItem(hWnd, ID_BTN_RELAY), g_boRelayOpen ? L"Close" : L"Open");
}

void AddBoldLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id = -1)
{
    HWND s = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, (HMENU)id, hInst, nullptr);
    SendMessageW(s, WM_SETFONT, (WPARAM)hFontBold, TRUE);
}

LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        SendMessage(GetParent(hwnd), WM_COMMAND, ID_EDIT_NAME_ENTER, (LPARAM)hwnd);
        return 0;
    }
    return CallWindowProc(g_OldEditProc, hwnd, uMsg, wParam, lParam);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = szWindowClass;
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    hbrYellow = CreateSolidBrush(RGB(255, 220, 80));
    hbrBlue = CreateSolidBrush(RGB(200, 230, 255));
    hbrRed = CreateSolidBrush(RGB(255, 120, 120));
    hbrPurple = CreateSolidBrush(RGB(190, 150, 255));

    LOGFONT lf = {};
    lf.lfWeight = FW_BOLD;
    lf.lfHeight = -12;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    hFontBold = CreateFontIndirect(&lf);

    HWND hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 760, 900,
        nullptr, nullptr,
        hInstance, nullptr
    );

    if (!hWnd) return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

void RefreshCombosFromCache(HWND hWnd)
{
    HWND hEditNameCB = GetDlgItem(hWnd, ID_CB_EDIT_NAME);
    HWND hEditIDCB = GetDlgItem(hWnd, ID_CB_EDIT_ID);
    HWND hActiveNameCB = GetDlgItem(hWnd, ID_ACTIVE_NAME);
    HWND hActiveIDCB = GetDlgItem(hWnd, ID_ACTIVE_ID);

    SendMessageW(hEditNameCB, CB_RESETCONTENT, 0, 0);
    SendMessageW(hEditIDCB, CB_RESETCONTENT, 0, 0);
    SendMessageW(hActiveNameCB, CB_RESETCONTENT, 0, 0);
    SendMessageW(hActiveIDCB, CB_RESETCONTENT, 0, 0);

    for (int i = 0; i < 8; ++i)
    {
        SendMessageW(hEditNameCB, CB_ADDSTRING, 0, (LPARAM)deviceNames[i]);
        SendMessageW(hEditIDCB, CB_ADDSTRING, 0, (LPARAM)deviceIDs[i]);
        SendMessageW(hActiveNameCB, CB_ADDSTRING, 0, (LPARAM)deviceNames[i]);
        SendMessageW(hActiveIDCB, CB_ADDSTRING, 0, (LPARAM)deviceIDs[i]);
    }

    SetSelectedIndex(hWnd, ID_CB_EDIT_NAME, g_u8Active);
    SetSelectedIndex(hWnd, ID_CB_EDIT_ID, g_u8Active);
    SetSelectedIndex(hWnd, ID_ACTIVE_NAME, g_u8Active);
    SetSelectedIndex(hWnd, ID_ACTIVE_ID, g_u8Active);

    SetControlText(hWnd, ID_EDIT_NAME, deviceNames[g_u8Active]);
    UpdateRelayButton(hWnd);
}

bool ReadAllNames(HWND hWnd)
{
    for (uint8_t i = 0; i < 8; ++i)
    {
        std::string name;
        if (!g_cdc.ReadName(i, name))
            return false;

        std::wstring ws = Utf8ToWide(name);
        wcsncpy_s(deviceNames[i], _countof(deviceNames[i]), ws.c_str(), 32);
    }

    RefreshCombosFromCache(hWnd);
    return true;
}

bool ReadActiveAndRelay(HWND hWnd)
{
    uint8_t active = 0;
    uint8_t relay = 0;

    if (!g_cdc.ReadActive(active))
        return false;
    if (active >= 8)
        return false;
    if (!g_cdc.ReadRelay(relay))
        return false;

    g_u8Active = active;
    g_boRelayOpen = (relay != 0);

    RefreshCombosFromCache(hWnd);
    return true;
}

bool ReadCurrentDevice(HWND hWnd)
{
    int idx = GetSelectedIndex(hWnd, ID_CB_EDIT_ID);
    if (idx < 0 || idx >= 8)
        return false;

    std::vector<uint8_t> data;
    if (!g_cdc.ReadMemory((uint8_t)idx, 0, 144, data))
        return false;
    if (data.size() != 144)
        return false;

    memcpy(g_au8Memory[idx], data.data(), 144);

    std::string name;
    if (!g_cdc.ReadName((uint8_t)idx, name))
        return false;

    std::wstring ws = Utf8ToWide(name);
    wcsncpy_s(deviceNames[idx], _countof(deviceNames[idx]), ws.c_str(), 32);

    SetControlText(hWnd, ID_EDIT_NAME, deviceNames[idx]);

    HWND hPanel = GetDlgItem(hWnd, ID_PANEL_EDITOR);
    for (int row = 0; row < 4; ++row)
    {
        int offset = row * 36;
        std::wstring line = FormatHexLine(&g_au8Memory[idx][offset], 36);
        SetWindowTextW(GetDlgItem(hPanel, ID_PAGE_EDIT_BASE + row), line.c_str());
    }

    RefreshCombosFromCache(hWnd);
    SetSelectedIndex(hWnd, ID_CB_EDIT_NAME, idx);
    SetSelectedIndex(hWnd, ID_CB_EDIT_ID, idx);
    SetControlText(hWnd, ID_EDIT_NAME, deviceNames[idx]);
    return true;
}

bool WriteCurrentDevice(HWND hWnd)
{
    int idx = GetSelectedIndex(hWnd, ID_CB_EDIT_ID);
    if (idx < 0 || idx >= 8)
        return false;

    std::wstring wsName = GetControlText(hWnd, ID_EDIT_NAME);
    if (wsName.size() > 32)
        wsName.resize(32);

    if (!g_cdc.WriteName((uint8_t)idx, WideToUtf8(wsName)))
        return false;

    wcsncpy_s(deviceNames[idx], _countof(deviceNames[idx]), wsName.c_str(), 32);

    std::vector<uint8_t> full(144, 0);
    HWND hPanel = GetDlgItem(hWnd, ID_PANEL_EDITOR);

    for (int row = 0; row < 4; ++row)
    {
        std::wstring wsLine;
        {
            wchar_t buf[1024] = { 0 };
            GetWindowTextW(GetDlgItem(hPanel, ID_PAGE_EDIT_BASE + row), buf, _countof(buf));
            wsLine = buf;
        }

        std::vector<uint8_t> chunk;
        if (!ParseHexLine(wsLine, chunk, 36))
            return false;

        int offset = row * 36;
        for (size_t i = 0; i < chunk.size(); ++i)
            full[offset + (int)i] = chunk[i];
    }

    if (!g_cdc.WriteMemory((uint8_t)idx, 0, full))
        return false;

    memcpy(g_au8Memory[idx], full.data(), 144);
    RefreshCombosFromCache(hWnd);
    return true;
}

void LoadEverythingFromDevice(HWND hWnd)
{
    if (!ReadAllNames(hWnd))
    {
        ShowError(hWnd, L"Nem sikerült a nevek beolvasása.");
        return;
    }

    if (!ReadActiveAndRelay(hWnd))
    {
        ShowError(hWnd, L"Nem sikerült az active/relay állapot beolvasása.");
        return;
    }

    if (!ReadCurrentDevice(hWnd))
    {
        ShowError(hWnd, L"Nem sikerült az aktuális device beolvasása.");
        return;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hComboCom, hBtnConnect, hBtnRelay;
    static HWND hEditNameCB, hEditIDCB, hNameEdit, hStyleCB, hBtnRead, hBtnWrite, hPanel;
    static HWND hActiveNameCB, hActiveIDCB;

    switch (message)
    {
    case WM_CREATE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        int rightX = 380;

        AddBoldLabel(hWnd, L"Connect", 12, 12, 60, 24);
        hComboCom = CreateWindowExW(
            0L, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER | WS_VSCROLL,
            80, 10, 90, 500,
            hWnd, (HMENU)ID_COMBO_COM, hInst, nullptr
        );

        hBtnConnect = CreateWindowW(
            L"BUTTON", L"Connect",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER,
            180, 8, 100, 26,
            hWnd, (HMENU)ID_BTN_CONNECT, hInst, nullptr
        );

        AddBoldLabel(hWnd, L"Relay", rightX, 12, 50, 24);
        hBtnRelay = CreateWindowW(
            L"BUTTON", L"Open",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER,
            rightX + 60, 8, 120, 26,
            hWnd, (HMENU)ID_BTN_RELAY, hInst, nullptr
        );

        int y = 50;
        AddBoldLabel(hWnd, L"Edit Coding Device", 12, y, 160, 22);
        hEditNameCB = CreateWindowW(
            L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER,
            12, y + 28, 270, 200,
            hWnd, (HMENU)ID_CB_EDIT_NAME, hInst, nullptr
        );
        hEditIDCB = CreateWindowW(
            L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER,
            290, y + 28, 50, 200,
            hWnd, (HMENU)ID_CB_EDIT_ID, hInst, nullptr
        );

        y += 80;
        AddBoldLabel(hWnd, L"Edit Name:", 12, y, 100, 22);
        hNameEdit = CreateWindowW(
            L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
            12, y + 26, 328, 26,
            hWnd, (HMENU)ID_EDIT_NAME, hInst, nullptr
        );
        g_OldEditProc = (WNDPROC)SetWindowLongPtr(hNameEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

        y += 70;
        AddBoldLabel(hWnd, L"Editor Style:", 12, y, 120, 22);
        hStyleCB = CreateWindowW(
            L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER,
            12, y + 28, 160, 120,
            hWnd, (HMENU)ID_CB_STYLE, hInst, nullptr
        );
        SendMessageW(hStyleCB, CB_ADDSTRING, 0, (LPARAM)L"Hex blocks");
        SendMessageW(hStyleCB, CB_SETCURSEL, 0, 0);

        hBtnRead = CreateWindowW(
            L"BUTTON", L"Read",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER,
            175, y + 26, 75, 26,
            hWnd, (HMENU)ID_BTN_READ, hInst, nullptr
        );
        hBtnWrite = CreateWindowW(
            L"BUTTON", L"Write",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER,
            255, y + 26, 75, 26,
            hWnd, (HMENU)ID_BTN_WRITE, hInst, nullptr
        );

        y += 90;
        hPanel = CreateWindowW(
            L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE | SS_LEFT | WS_BORDER,
            12, y, width - 24, height - y - 24,
            hWnd, (HMENU)ID_PANEL_EDITOR, hInst, nullptr
        );

        int rx = rightX;
        int ry = 60;
        AddBoldLabel(hWnd, L"Active Coding Device", rx, ry, 200, 22);
        hActiveNameCB = CreateWindowW(
            L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER,
            rx, ry + 28, 220, 200,
            hWnd, (HMENU)ID_ACTIVE_NAME, hInst, nullptr
        );
        hActiveIDCB = CreateWindowW(
            L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER,
            rx + 228, ry + 28, 50, 200,
            hWnd, (HMENU)ID_ACTIVE_ID, hInst, nullptr
        );

        BuildEditorPanel(hPanel);
        RefreshCombosFromCache(hWnd);
        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId)
        {
        case ID_COMBO_COM:
            if (HIWORD(wParam) == CBN_DROPDOWN)
            {
                ULONG portNumbers[256];
                ULONG portsFound = 0;
                GetCommPorts(portNumbers, 256, &portsFound);

                SendMessageW(hComboCom, CB_RESETCONTENT, 0, 0);
                for (ULONG i = 0; i < portsFound; ++i)
                {
                    wchar_t name[64];
                    wsprintfW(name, L"COM%lu", portNumbers[i]);
                    SendMessageW(hComboCom, CB_ADDSTRING, 0, (LPARAM)name);
                }
            }
            break;

        case ID_BTN_CONNECT:
        {
            if (!g_boConnected)
            {
                std::wstring port = GetSelectedComPort(hWnd);
                if (port.empty())
                {
                    ShowError(hWnd, L"Először válassz COM portot.");
                    break;
                }

                std::string portA = WideToUtf8(port);
                if (!g_cdc.Connect(portA.c_str(), 115200))
                {
                    ShowError(hWnd, L"Nem sikerült csatlakozni.");
                    break;
                }

                g_boConnected = true;
                SetWindowTextW(hBtnConnect, L"Disconnect");
                LoadEverythingFromDevice(hWnd);
                ShowInfo(hWnd, L"Csatlakozva.");
            }
            else
            {
                g_cdc.Disconnect();
                g_boConnected = false;
                SetWindowTextW(hBtnConnect, L"Connect");
                ShowInfo(hWnd, L"Kapcsolat bontva.");
            }
            break;
        }

        case ID_BTN_RELAY:
        {
            if (!g_boConnected)
            {
                ShowError(hWnd, L"Nincs kapcsolat.");
                break;
            }

            bool next = !g_boRelayOpen;
            if (!g_cdc.WriteRelay(next ? 1 : 0))
            {
                ShowError(hWnd, L"Relay írás sikertelen.");
                break;
            }

            g_boRelayOpen = next;
            UpdateRelayButton(hWnd);
            break;
        }

        case ID_CB_EDIT_NAME:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = GetSelectedIndex(hWnd, ID_CB_EDIT_NAME);
                SetSelectedIndex(hWnd, ID_CB_EDIT_ID, idx);
                SetControlText(hWnd, ID_EDIT_NAME, deviceNames[idx]);
            }
            break;

        case ID_CB_EDIT_ID:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = GetSelectedIndex(hWnd, ID_CB_EDIT_ID);
                SetSelectedIndex(hWnd, ID_CB_EDIT_NAME, idx);
                SetControlText(hWnd, ID_EDIT_NAME, deviceNames[idx]);
            }
            break;

        case ID_EDIT_NAME_ENTER:
        {
            if (!g_boConnected)
            {
                ShowError(hWnd, L"Nincs kapcsolat.");
                break;
            }

            int idx = GetSelectedIndex(hWnd, ID_CB_EDIT_ID);
            if (idx < 0 || idx >= 8)
                break;

            std::wstring ws = GetControlText(hWnd, ID_EDIT_NAME);
            if (ws.size() > 32) ws.resize(32);

            if (!g_cdc.WriteName((uint8_t)idx, WideToUtf8(ws)))
            {
                ShowError(hWnd, L"Név írás sikertelen.");
                break;
            }

            wcsncpy_s(deviceNames[idx], _countof(deviceNames[idx]), ws.c_str(), 32);
            RefreshCombosFromCache(hWnd);
            SetSelectedIndex(hWnd, ID_CB_EDIT_NAME, idx);
            SetSelectedIndex(hWnd, ID_CB_EDIT_ID, idx);
            SetControlText(hWnd, ID_EDIT_NAME, deviceNames[idx]);
            break;
        }

        case ID_ACTIVE_NAME:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = GetSelectedIndex(hWnd, ID_ACTIVE_NAME);
                SetSelectedIndex(hWnd, ID_ACTIVE_ID, idx);

                if (g_boConnected)
                {
                    if (!g_cdc.WriteActive((uint8_t)idx))
                    {
                        ShowError(hWnd, L"Active device írás sikertelen.");
                        break;
                    }
                    g_u8Active = (uint8_t)idx;
                }
            }
            break;

        case ID_ACTIVE_ID:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = GetSelectedIndex(hWnd, ID_ACTIVE_ID);
                SetSelectedIndex(hWnd, ID_ACTIVE_NAME, idx);

                if (g_boConnected)
                {
                    if (!g_cdc.WriteActive((uint8_t)idx))
                    {
                        ShowError(hWnd, L"Active device írás sikertelen.");
                        break;
                    }
                    g_u8Active = (uint8_t)idx;
                }
            }
            break;

        case ID_CB_STYLE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                BuildEditorPanel(GetDlgItem(hWnd, ID_PANEL_EDITOR));
                if (g_boConnected)
                    ReadCurrentDevice(hWnd);
            }
            break;

        case ID_BTN_READ:
            if (!g_boConnected)
            {
                ShowError(hWnd, L"Nincs kapcsolat.");
                break;
            }
            if (!ReadCurrentDevice(hWnd))
            {
                ShowError(hWnd, L"Olvasás sikertelen.");
            }
            break;

        case ID_BTN_WRITE:
            if (!g_boConnected)
            {
                ShowError(hWnd, L"Nincs kapcsolat.");
                break;
            }
            if (!WriteCurrentDevice(hWnd))
            {
                ShowError(hWnd, L"Írás sikertelen.");
            }
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }

    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        int id = GetDlgCtrlID(ctrl);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        if (id == ID_BTN_CONNECT || id == ID_BTN_READ || id == ID_BTN_WRITE || id == ID_BTN_RELAY)
            return (LRESULT)hbrYellow;
        break;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)hbrBlue;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        int id = GetDlgCtrlID(ctrl);
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, RGB(0, 0, 0));

        if (id == ID_CB_EDIT_NAME || id == ID_CB_EDIT_ID || id == ID_ACTIVE_NAME || id == ID_ACTIVE_ID || id == ID_CB_STYLE)
            return (LRESULT)hbrRed;

        if (id == ID_PANEL_EDITOR)
            return (LRESULT)hbrPurple;

        break;
    }

    case WM_DESTROY:
        if (g_boConnected)
            g_cdc.Disconnect();

        DeleteObject(hbrYellow);
        DeleteObject(hbrBlue);
        DeleteObject(hbrRed);
        DeleteObject(hbrPurple);
        DeleteObject(hFontBold);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

void BuildEditorPanel(HWND hPanel)
{
    if (!hPanel) return;
    ClearEditorPanel(hPanel);

    RECT rc;
    GetClientRect(hPanel, &rc);
    int w = rc.right - rc.left;
    int margin = 10;
    int rowH = 34;

    for (int i = 0; i < 4; ++i)
    {
        wchar_t lbl[32];
        wsprintfW(lbl, L"Page%d", i);

        HWND hLbl = CreateWindowW(
            L"STATIC", lbl,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            margin, margin + i * (rowH + 8), 80, rowH,
            hPanel, NULL, hInst, nullptr
        );
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        CreateWindowW(
            L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            margin + 90, margin + i * (rowH + 8), w - (margin * 2 + 90), rowH,
            hPanel, (HMENU)(ID_PAGE_EDIT_BASE + i), hInst, nullptr
        );
    }
}

void ClearEditorPanel(HWND panel)
{
    HWND child = NULL;
    while ((child = FindWindowExW(panel, NULL, NULL, NULL)) != NULL)
        DestroyWindow(child);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_CODINGDEVICE, szWindowClass, MAX_LOADSTRING);

    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CODINGDEVICE));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}