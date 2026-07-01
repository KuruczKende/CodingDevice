// CodingDevice.cpp : Native device programmer/editor UI (single-window Win32)
// Implements a two-column layout: left = editor, right = connection/actions.
// Colored semantic controls: yellow buttons, blue text, red dropdowns, purple editor panel.
#pragma comment(lib, "OneCore.lib")

#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "serialib.h"
#include <string>
#include <vector>

#define MAX_LOADSTRING 100

// Custom notification for Enter pressed in name edit
#define EN_ENTER 1000

// Control IDs
#define ID_COMBO_COM        1001
#define ID_BTN_CONNECT      1002
#define ID_BTN_RELAY        1003

#define ID_CB_EDIT_NAME     1101
#define ID_CB_EDIT_ID       1102
#define ID_EDIT_NAME        1103
#define ID_CB_STYLE         1104
#define ID_BTN_READ         1105
#define ID_BTN_WRITE        1106
#define ID_PANEL_EDITOR     1107

#define ID_ACTIVE_NAME      1201
#define ID_ACTIVE_ID        1202

#define ID_EDIT_NAME_ENTER  1301

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING] = L"Coding Device Config";
WCHAR szWindowClass[MAX_LOADSTRING] = L"CodingDeviceClass";

HFONT hFontBold = NULL;
HBRUSH hbrYellow = NULL, hbrBlue = NULL, hbrRed = NULL, hbrPurple = NULL;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

void BuildEditorPanel(HWND hParent);
void ClearEditorPanel(HWND panel);

// Simple sample device list (names <-> IDs)
wchar_t deviceNames[8][32+1] = { L"DeviceA", L"DeviceB", L"DeviceC", L"DeviceD", L"DeviceE", L"DeviceF", L"DeviceG", L"DeviceH" };
const wchar_t* deviceIDs[8] = { L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7" };

serialib serial;

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    ZeroMemory(&wcex, sizeof(wcex));

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // light gray background
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = szWindowClass;
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    // Prepare brushes and font
    hbrYellow = CreateSolidBrush(RGB(255, 220, 80));
    hbrBlue = CreateSolidBrush(RGB(200, 230, 255));
    hbrRed = CreateSolidBrush(RGB(255, 120, 120));
    hbrPurple = CreateSolidBrush(RGB(190, 150, 255));
    LOGFONT lf = { 0 };
    lf.lfWeight = FW_BOLD;
    lf.lfHeight = -12;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    hFontBold = CreateFontIndirect(&lf);

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 700, 900, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

void AddBoldLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id = -1)
{
    HWND s = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, parent, (HMENU)id, hInst, nullptr);
    SendMessageW(s, WM_SETFONT, (WPARAM)hFontBold, TRUE);
}

WNDPROC g_OldEditProc = nullptr;
// Subclass for the name edit to capture Enter key and post a notification to parent
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        SendMessage(GetParent(hwnd), WM_COMMAND, ID_EDIT_NAME_ENTER, (LPARAM)hwnd); // notify parent
        return 0;
    }
    return CallWindowProc(g_OldEditProc, hwnd, uMsg, wParam, lParam);
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
        int rightX = 350;

        // Top-left: Connect row
        AddBoldLabel(hWnd, L"Connect", 12, 12, 60, 24);
        hComboCom = CreateWindowExW(0L, L"COMBOBOX", nullptr, 0x40000000L | 0x10000000L | 0x0003L | 0x00800000L, 80, 10, 70, 500, hWnd, (HMENU)ID_COMBO_COM, hInst, nullptr);

        hBtnConnect = CreateWindowW(L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER, 160, 8, 80, 26, hWnd, (HMENU)ID_BTN_CONNECT, hInst, nullptr);

        // Top-right: Relay row
        AddBoldLabel(hWnd, L"Relay", rightX, 12, 50, 24);
        hBtnRelay = CreateWindowW(L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER, rightX + 60, 8, 120, 26, hWnd, (HMENU)ID_BTN_RELAY, hInst, nullptr);

        // Left panel editor rows start below top (y = 50)
        int y = 50;
        // 1. Edit Coding Device
        AddBoldLabel(hWnd, L"Edit Coding Device", 12, y, 160, 22);
        hEditNameCB = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER, 12, y + 28, 270, 200, hWnd, (HMENU)ID_CB_EDIT_NAME, hInst, nullptr);
        hEditIDCB = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER, 290, y + 28, 40, 200, hWnd, (HMENU)ID_CB_EDIT_ID, hInst, nullptr);

        for (int i = 0; i < 8; ++i)
        {
            SendMessageW(hEditNameCB, CB_ADDSTRING, 0, (LPARAM)deviceNames[i]);
            SendMessageW(hEditIDCB, CB_ADDSTRING, 0, (LPARAM)deviceIDs[i]);
        }
        SendMessageW(hEditNameCB, CB_SETCURSEL, 0, 0);
        SendMessageW(hEditIDCB, CB_SETCURSEL, 0, 0);

        y += 80;
        // 2. Edit Name
        AddBoldLabel(hWnd, L"Edit Name:", 12, y, 100, 22);
        hNameEdit = CreateWindowW(L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 12, y + 26, 318, 26, hWnd, (HMENU)ID_EDIT_NAME, hInst, nullptr);

        // Subclass the name edit so Enter can be detected and forwarded to parent
        g_OldEditProc = (WNDPROC)SetWindowLongPtr(hNameEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SendMessageW(GetDlgItem(hWnd, ID_EDIT_NAME), WM_SETTEXT, 0, (LPARAM)deviceNames[0]);

        y += 70;
        // 3. Editor Style
        AddBoldLabel(hWnd, L"Editor Style:", 12, y, 120, 22);
        hStyleCB = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER, 12, y + 28, 160, 120, hWnd, (HMENU)ID_CB_STYLE, hInst, nullptr);
        SendMessageW(hStyleCB, CB_ADDSTRING, 0, (LPARAM)L"ExampleStyle");
        SendMessageW(hStyleCB, CB_ADDSTRING, 0, (LPARAM)L"AnotherStyle");
        SendMessageW(hStyleCB, CB_SETCURSEL, 0, 0);

        hBtnRead = CreateWindowW(L"BUTTON", L"Read", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER, 175, y + 26, 75, 26, hWnd, (HMENU)ID_BTN_READ, hInst, nullptr);
        hBtnWrite = CreateWindowW(L"BUTTON", L"Write", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_BORDER, 255, y + 26, 75, 26, hWnd, (HMENU)ID_BTN_WRITE, hInst, nullptr);

        y += 90;
        // Large purple content panel (editor)
        hPanel = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_LEFT | WS_BORDER, 12, y, width - 24, height - y - 24, hWnd, (HMENU)ID_PANEL_EDITOR, hInst, nullptr);

        // Right-side Active Coding Device
        int rx = rightX;
        int ry = 60;
        AddBoldLabel(hWnd, L"Active Coding Device", rx, ry, 200, 22);
        hActiveNameCB = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER, rx, ry + 28, 270, 200, hWnd, (HMENU)ID_ACTIVE_NAME, hInst, nullptr);
        hActiveIDCB = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER, rx + 278, ry + 28, 40, 200, hWnd, (HMENU)ID_ACTIVE_ID, hInst, nullptr);
        for (int i = 0; i < 8; ++i)
        {
            SendMessageW(hActiveNameCB, CB_ADDSTRING, 0, (LPARAM)deviceNames[i]);
            SendMessageW(hActiveIDCB, CB_ADDSTRING, 0, (LPARAM)deviceIDs[i]);
        }
        SendMessageW(hActiveNameCB, CB_SETCURSEL, 0, 0);
        SendMessageW(hActiveIDCB, CB_SETCURSEL, 0, 0);

        // Initial build of editor content
        BuildEditorPanel(hPanel);
        break;
    }
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        HWND src = (HWND)lParam;
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
                    wchar_t szTitle[128];
                    wsprintf(szTitle, L"COM%lu", portNumbers[i]);
                    SendMessageW(hComboCom, CB_ADDSTRING, 0, (LPARAM)szTitle);
                }
			}
			break;
        case ID_BTN_CONNECT:
        {
            // Simulate opening COM port
            int sel = (int)SendMessageW(GetDlgItem(hWnd, ID_COMBO_COM), CB_GETCURSEL, 0, 0);
            wchar_t buf[128]; wsprintfW(buf, L"Opening %s...", sel >= 0 ? L"selected COM" : L"(none)");
            MessageBoxW(hWnd, buf, L"Connect", MB_OK);
            break;
        }
        case ID_BTN_RELAY:
        {
            // Toggle relay button text
            HWND btn = GetDlgItem(hWnd, ID_BTN_RELAY);
            wchar_t cur[64]; GetWindowTextW(btn, cur, _countof(cur));
            if (wcscmp(cur, L"Open") == 0)
                SetWindowTextW(btn, L"Close");
            else
                SetWindowTextW(btn, L"Open");
            break;
        }
        case ID_CB_EDIT_NAME:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = (int)SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_NAME), CB_GETCURSEL, 0, 0);
                SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_ID), CB_SETCURSEL, idx, 0);
                // update edit name text
                SendMessageW(GetDlgItem(hWnd, ID_EDIT_NAME), WM_SETTEXT, 0, (LPARAM)deviceNames[idx]);
            }
            break;
        case ID_CB_EDIT_ID:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = (int)SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_ID), CB_GETCURSEL, 0, 0);
                SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_NAME), CB_SETCURSEL, idx, 0);
                SendMessageW(GetDlgItem(hWnd, ID_EDIT_NAME), WM_SETTEXT, 0, (LPARAM)deviceNames[idx]);
            }
            break;
        case ID_EDIT_NAME_ENTER:
            // Handle custom Enter notification posted by subclass
            if (true)
            {
                int idx = (int)SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_ID), CB_GETCURSEL, 0, 0);
                // Example action: treat Enter as "Write" trigger for this edit
                wchar_t buf[256] = { 0 };
                GetWindowTextW(GetDlgItem(hWnd, ID_EDIT_NAME), buf, _countof(buf));
                wcsncpy_s(deviceNames[idx], _countof(deviceNames[idx]), buf, min(_countof(buf),32));
                SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_NAME), CB_DELETESTRING, idx, 0);
                SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_NAME), CB_INSERTSTRING, idx, (LPARAM)deviceNames[idx]);
                SendMessageW(GetDlgItem(hWnd, ID_CB_EDIT_NAME), CB_SETCURSEL, idx, 0);

                SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_NAME), CB_DELETESTRING, idx, 0);
                SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_NAME), CB_INSERTSTRING, idx, (LPARAM)deviceNames[idx]);
                if (idx == (int)SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_ID), CB_GETCURSEL, 0, 0)) {
                    SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_NAME), CB_SETCURSEL, idx, 0);
                }
            }
            break;
        case ID_ACTIVE_NAME:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = (int)SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_NAME), CB_GETCURSEL, 0, 0);
                SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_ID), CB_SETCURSEL, idx, 0);
            }
            break;
        case ID_ACTIVE_ID:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int idx = (int)SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_ID), CB_GETCURSEL, 0, 0);
                SendMessageW(GetDlgItem(hWnd, ID_ACTIVE_NAME), CB_SETCURSEL, idx, 0);
            }
            break;
        case ID_CB_STYLE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                // Rebuild purple editor based on style
                BuildEditorPanel(GetDlgItem(hWnd, ID_PANEL_EDITOR));
            }
            break;
        case ID_BTN_READ:
        {
            // Simulate reading into the editor by filling fields
            HWND panel = GetDlgItem(hWnd, ID_PANEL_EDITOR);
            for (int i = 0; i < 4; ++i)
            {
                wchar_t ctrlName[32];
                wsprintfW(ctrlName, L"PAGE_EDIT_%d", i);
                HWND h = FindWindowExW(panel, NULL, L"EDIT", NULL);
                // Instead of complex lookup, write sample data to child edit controls by enumerating
            }
            // For brevity: show a simple message and set a sample text in first child edit
            HWND firstEdit = FindWindowExW(GetDlgItem(hWnd, ID_PANEL_EDITOR), NULL, L"EDIT", NULL);
            if (firstEdit) SetWindowTextW(firstEdit, L"ReadValue1");
            MessageBoxW(hWnd, L"Read completed (simulated).", L"Read", MB_OK);
            break;
        }
        case ID_BTN_WRITE:
        {
            // Simulate write: collect data from editor fields (here only demonstrates)
            HWND firstEdit = FindWindowExW(GetDlgItem(hWnd, ID_PANEL_EDITOR), NULL, L"EDIT", NULL);
            wchar_t buf[256] = { 0 };
            if (firstEdit) GetWindowTextW(firstEdit, buf, _countof(buf));
            MessageBoxW(hWnd, buf[0] ? buf : L"Write completed (simulated).", L"Write", MB_OK);
            break;
        }
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        int id = GetDlgCtrlID(ctrl);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        if (id == ID_BTN_CONNECT || id == ID_BTN_READ || id == ID_BTN_WRITE || id == ID_BTN_RELAY)
        {
            return (LRESULT)hbrYellow;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        int id = GetDlgCtrlID(ctrl);
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, RGB(0, 0, 0));
        if (id == ID_EDIT_NAME)
            return (LRESULT)hbrBlue;
        // editor panel edits will also be blue
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
        // Combobox static portion and dropdown list use LISTBOX/STATIC messages; color by ID
        if (id == ID_CB_EDIT_NAME || id == ID_CB_EDIT_ID || id == ID_ACTIVE_NAME || id == ID_ACTIVE_ID || id == ID_CB_STYLE)
            return (LRESULT)hbrRed;
        if (id == ID_PANEL_EDITOR)
            return (LRESULT)hbrPurple;
        // Labels and other static controls get default
        break;
    }
    case WM_DESTROY:
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

// Build a simple example style inside the purple panel: Page0..Page3 with label + long blue edit
void BuildEditorPanel(HWND hPanel)
{
    if (!hPanel) return;
    ClearEditorPanel(hPanel);
    RECT rc; GetClientRect(hPanel, &rc);
    int w = rc.right - rc.left;
    int margin = 10;
    int rowH = 34;
    for (int i = 0; i < 4; ++i)
    {
        wchar_t lbl[32]; wsprintfW(lbl, L"Page%d", i);
        HWND hLbl = CreateWindowW(L"STATIC", lbl, WS_CHILD | WS_VISIBLE | SS_LEFT, margin, margin + i * (rowH + 8), 80, rowH, hPanel, NULL, hInst, nullptr);
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        HWND hEd = CreateWindowW(L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, margin + 90, margin + i * (rowH + 8), w - (margin * 2 + 90), rowH, hPanel, NULL, hInst, nullptr);
        // color for edit handled in WM_CTLCOLOREDIT (returns blue brush)
    }
    // leave empty space below for other styles
}

void ClearEditorPanel(HWND panel)
{
    HWND child = NULL;
    while ((child = FindWindowExW(panel, NULL, NULL, NULL)) != NULL)
    {
        DestroyWindow(child);
    }
}

// CodingDevice.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "CodingDevice.h"

#define MAX_LOADSTRING 100

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_CODINGDEVICE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CODINGDEVICE));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}
// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
