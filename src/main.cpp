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
constexpr int ID_STATUS = 1003;
constexpr int ID_FPS = 1004;
constexpr int ID_QUALITY = 1005;
constexpr int ID_CURSOR = 1006;
constexpr int ID_INFO = 1007;
constexpr int HOTKEY_ID = 7;

Recorder g_recorder;
HWND g_status = nullptr;
HWND g_start = nullptr;
HWND g_fps = nullptr;
HWND g_quality = nullptr;
HWND g_cursor = nullptr;
HWND g_info = nullptr;

bool ContainsMinecraft(std::wstring title) {
    std::transform(title.begin(), title.end(), title.begin(), [](wchar_t c) { return std::towlower(c); });
    return title.find(L"minecraft") != std::wstring::npos;
}

BOOL CALLBACK FindMinecraftProc(HWND hwnd, LPARAM param) {
    if (!IsWindowVisible(hwnd)) return TRUE;
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

void SetStatus(const std::wstring& text) {
    if (g_status) SetWindowTextW(g_status, text.c_str());
}

void ToggleRecording() {
    if (g_recorder.IsRecording()) {
        g_recorder.Stop();
        SetStatus(L"READY  |  Recording saved to Documents\\Recorder");
        SetWindowTextW(g_start, L"Start Recording  (F8)");
        return;
    }

    HWND minecraft = FindMinecraftWindow();
    if (!minecraft) {
        SetStatus(L"Minecraft was not found. Start Minecraft first.");
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
        return;
    }

    SetStatus(L"● RECORDING  |  " + std::to_wstring(config.maxWidth) + L"x" +
              std::to_wstring(config.maxHeight) + L"  |  " + std::to_wstring(config.fps) +
              L" FPS  |  " + g_recorder.EncoderName());
    SetWindowTextW(g_start, L"Stop Recording  (F8)");
}

void OpenRecordings(HWND hwnd) {
    const std::wstring folder = RecordingsFolder();
    ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"RECORDER", WS_CHILD | WS_VISIBLE, 24, 18, 180, 30,
                      hwnd, nullptr, nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"READY  |  Press F8 to record Minecraft",
            WS_CHILD | WS_VISIBLE, 24, 52, 500, 28, hwnd, (HMENU)ID_STATUS, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"FPS", WS_CHILD | WS_VISIBLE, 24, 94, 45, 22,
                      hwnd, nullptr, nullptr, nullptr);
        g_fps = CreateWindowW(L"COMBOBOX", L"30", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            68, 90, 90, 120, hwnd, (HMENU)ID_FPS, nullptr, nullptr);
        SendMessageW(g_fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30"));
        SendMessageW(g_fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"60"));
        SendMessageW(g_fps, CB_SETCURSEL, 0, 0);

        CreateWindowW(L"STATIC", L"Quality", WS_CHILD | WS_VISIBLE, 176, 94, 55, 22,
                      hwnd, nullptr, nullptr, nullptr);
        g_quality = CreateWindowW(L"COMBOBOX", L"26", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            234, 90, 100, 120, hwnd, (HMENU)ID_QUALITY, nullptr, nullptr);
        SendMessageW(g_quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"22"));
        SendMessageW(g_quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"26"));
        SendMessageW(g_quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30"));
        SendMessageW(g_quality, CB_SETCURSEL, 1, 0);

        g_cursor = CreateWindowW(L"BUTTON", L"Capture cursor", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            350, 92, 125, 24, hwnd, (HMENU)ID_CURSOR, nullptr, nullptr);
        SendMessageW(g_cursor, BM_SETCHECK, BST_CHECKED, 0);

        g_start = CreateWindowW(L"BUTTON", L"Start Recording  (F8)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 132, 220, 40, hwnd,
            (HMENU)ID_START, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Open Recordings",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 254, 132, 180, 40, hwnd,
            (HMENU)ID_OPEN, nullptr, nullptr);

        g_info = CreateWindowW(L"STATIC",
            L"Low-end mode: 720p target | 30 FPS default | Intel Quick Sync preferred",
            WS_CHILD | WS_VISIBLE, 24, 184, 500, 24, hwnd, (HMENU)ID_INFO, nullptr, nullptr);

        RegisterHotKey(hwnd, HOTKEY_ID, 0, VK_F8);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_START) ToggleRecording();
        if (LOWORD(wParam) == ID_OPEN) OpenRecordings(hwnd);
        return 0;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) ToggleRecording();
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        g_recorder.Stop();
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Recorder | Minecraft Low-End Recorder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 260, nullptr, nullptr, instance, nullptr);
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
