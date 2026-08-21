#include "recorder.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace {
constexpr int ID_START = 1001;
constexpr int ID_OPEN = 1002;
constexpr int ID_FPS = 1004;
constexpr int ID_QUALITY = 1005;
constexpr int ID_CURSOR = 1006;
constexpr int ID_PROFILE = 1007;
constexpr int ID_MINIMIZE = 1008;
constexpr int HOTKEY_ID = 7;

constexpr COLORREF BG = RGB(15, 18, 24);
constexpr COLORREF PANEL = RGB(24, 29, 38);
constexpr COLORREF PANEL2 = RGB(31, 37, 48);
constexpr COLORREF TEXT = RGB(235, 239, 245);
constexpr COLORREF MUTED = RGB(150, 160, 175);
constexpr COLORREF ACCENT = RGB(55, 210, 140);
constexpr COLORREF RED = RGB(235, 75, 90);

Recorder g_recorder;
HWND g_status = nullptr;
HWND g_start = nullptr;
HWND g_fps = nullptr;
HWND g_quality = nullptr;
HWND g_cursor = nullptr;
HWND g_profile = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_normalFont = nullptr;
HFONT g_smallFont = nullptr;

bool ContainsMinecraft(std::wstring title) {
    std::transform(title.begin(), title.end(), title.begin(), [](wchar_t c) { return std::towlower(c); });
    return title.find(L"minecraft") != std::wstring::npos;
}

BOOL CALLBACK FindMinecraftProc(HWND hwnd, LPARAM param) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, 511);
    if (ContainsMinecraft(title)) {
        *reinterpret_cast<HWND*>(param) = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindMinecraftWindow() {
    HWND result = nullptr;
    EnumWindows(FindMinecraftProc, reinterpret_cast<LPARAM>(&result));
    return result;
}

std::wstring RecordingsFolder() {
    wchar_t docs[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))) {
        std::filesystem::path p(docs);
        p /= L"Recorder";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p.wstring();
    }
    return L".";
}

std::wstring OutputPath() {
    std::filesystem::path p(RecordingsFolder());
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[128]{};
    swprintf_s(name, L"Minecraft_%04d-%02d-%02d_%02d-%02d-%02d.mp4",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    p /= name;
    return p.wstring();
}

int ComboValue(HWND combo, int fallback) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0) return fallback;
    wchar_t text[32]{};
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
    return _wtoi(text);
}

void SetStatus(const std::wstring& text, bool recording = false) {
    if (g_status) SetWindowTextW(g_status, text.c_str());
    if (g_start) {
        SetWindowTextW(g_start, recording ? L"STOP RECORDING   •   F8" : L"START RECORDING   •   F8");
        InvalidateRect(g_start, nullptr, TRUE);
    }
}

void ToggleRecording() {
    if (g_recorder.IsRecording()) {
        g_recorder.Stop();
        SetStatus(L"Ready. Your recording was saved to Documents\\Recorder.");
        return;
    }

    HWND minecraft = FindMinecraftWindow();
    if (!minecraft) {
        SetStatus(L"Minecraft was not found. Open the game first.");
        MessageBeep(MB_ICONWARNING);
        return;
    }

    RecordingConfig config;
    config.fps = ComboValue(g_fps, 30);
    config.quality = ComboValue(g_quality, 26);
    config.maxWidth = 1280;
    config.maxHeight = 720;
    config.lowEndMode = true;
    config.captureCursor = SendMessageW(g_cursor, BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (!g_recorder.Start(minecraft, OutputPath(), config)) {
        SetStatus(g_recorder.LastError());
        MessageBeep(MB_ICONERROR);
        return;
    }

    SetStatus(L"Recording Minecraft  •  " + std::to_wstring(config.fps) + L" FPS  •  " + g_recorder.EncoderName(), true);
}

void OpenRecordings(HWND hwnd) {
    const std::wstring folder = RecordingsFolder();
    ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ApplyFont(HWND h, HFONT font) {
    if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void CreateFonts() {
    g_titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_normalFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_smallFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateFonts();

        HWND title = CreateWindowW(L"STATIC", L"Recorder", WS_CHILD | WS_VISIBLE,
            24, 20, 300, 36, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(title, g_titleFont);

        HWND subtitle = CreateWindowW(L"STATIC", L"Lightweight Minecraft capture", WS_CHILD | WS_VISIBLE,
            26, 55, 300, 22, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(subtitle, g_smallFont);

        g_status = CreateWindowW(L"STATIC", L"Ready. Press F8 or click Start Recording.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 88, 560, 34, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(g_status, g_normalFont);

        HWND profileLabel = CreateWindowW(L"STATIC", L"PROFILE", WS_CHILD | WS_VISIBLE,
            24, 137, 80, 20, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(profileLabel, g_smallFont);
        g_profile = CreateWindowW(L"COMBOBOX", L"Low-end 720p", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            24, 160, 170, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROFILE)), nullptr, nullptr);
        SendMessageW(g_profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Low-end 720p"));
        SendMessageW(g_profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"720p 60 FPS"));
        SendMessageW(g_profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1080p 30 FPS"));
        SendMessageW(g_profile, CB_SETCURSEL, 0, 0);
        ApplyFont(g_profile, g_normalFont);

        HWND fpsLabel = CreateWindowW(L"STATIC", L"FPS", WS_CHILD | WS_VISIBLE,
            216, 137, 60, 20, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(fpsLabel, g_smallFont);
        g_fps = CreateWindowW(L"COMBOBOX", L"30", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            216, 160, 80, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FPS)), nullptr, nullptr);
        SendMessageW(g_fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30"));
        SendMessageW(g_fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"60"));
        SendMessageW(g_fps, CB_SETCURSEL, 0, 0);
        ApplyFont(g_fps, g_normalFont);

        HWND qualityLabel = CreateWindowW(L"STATIC", L"QUALITY", WS_CHILD | WS_VISIBLE,
            318, 137, 80, 20, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(qualityLabel, g_smallFont);
        g_quality = CreateWindowW(L"COMBOBOX", L"26", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            318, 160, 80, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_QUALITY)), nullptr, nullptr);
        SendMessageW(g_quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"22"));
        SendMessageW(g_quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"26"));
        SendMessageW(g_quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30"));
        SendMessageW(g_quality, CB_SETCURSEL, 1, 0);
        ApplyFont(g_quality, g_normalFont);

        g_cursor = CreateWindowW(L"BUTTON", L"Capture cursor", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            414, 160, 130, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CURSOR)), nullptr, nullptr);
        SendMessageW(g_cursor, BM_SETCHECK, BST_CHECKED, 0);
        ApplyFont(g_cursor, g_normalFont);

        g_start = CreateWindowW(L"BUTTON", L"START RECORDING   •   F8",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 24, 215, 270, 48, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_START)), nullptr, nullptr);
        ApplyFont(g_start, g_normalFont);

        HWND open = CreateWindowW(L"BUTTON", L"Open recordings",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 310, 215, 170, 48, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN)), nullptr, nullptr);
        ApplyFont(open, g_normalFont);

        HWND footer = CreateWindowW(L"STATIC",
            L"GPU-first capture  •  Intel Quick Sync preferred  •  No preview = less FPS loss",
            WS_CHILD | WS_VISIBLE, 24, 278, 560, 22, hwnd, nullptr, nullptr, nullptr);
        ApplyFont(footer, g_smallFont);

        RegisterHotKey(hwnd, HOTKEY_ID, 0, VK_F8);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, BG);
        SetTextColor(dc, (reinterpret_cast<HWND>(lParam) == g_status) ? TEXT : MUTED);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, PANEL2);
        SetTextColor(dc, TEXT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID != ID_START) break;
        HBRUSH brush = CreateSolidBrush(g_recorder.IsRecording() ? RED : ACCENT);
        FillRect(dis->hDC, &dis->rcItem, brush);
        DeleteObject(brush);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, RGB(10, 14, 18));
        const wchar_t* label = g_recorder.IsRecording() ? L"STOP RECORDING   •   F8" : L"START RECORDING   •   F8";
        DrawTextW(dis->hDC, label, -1, const_cast<LPRECT>(&dis->rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (dis->itemState & ODS_FOCUS) FrameRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_WINDOWFRAME));
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_START: ToggleRecording(); break;
        case ID_OPEN: OpenRecordings(hwnd); break;
        case ID_PROFILE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const int p = static_cast<int>(SendMessageW(g_profile, CB_GETCURSEL, 0, 0));
                if (p == 0) { SendMessageW(g_fps, CB_SETCURSEL, 0, 0); }
                if (p == 1) { SendMessageW(g_fps, CB_SETCURSEL, 1, 0); }
                if (p == 2) { SendMessageW(g_fps, CB_SETCURSEL, 0, 0); }
            }
            break;
        }
        return 0;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) ToggleRecording();
        return 0;
    case WM_CLOSE:
        if (g_recorder.IsRecording()) {
            const int answer = MessageBoxW(hwnd, L"A recording is still running. Stop and close Recorder?",
                                           L"Recorder", MB_YESNO | MB_ICONQUESTION);
            if (answer != IDYES) return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        g_recorder.Stop();
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_normalFont) DeleteObject(g_normalFont);
        if (g_smallFont) DeleteObject(g_smallFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const wchar_t CLASS_NAME[] = L"RecorderMainWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(BG);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Recorder | Minecraft Capture",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 610, 350, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
