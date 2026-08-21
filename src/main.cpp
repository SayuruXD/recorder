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
constexpr int HOTKEY_ID = 7;

Recorder g_recorder;
HWND g_status = nullptr;
HWND g_start = nullptr;

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

std::wstring OutputPath() {
    wchar_t docs[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))) {
        std::filesystem::path p(docs);
        p /= L"Recorder";
        p /= L"Minecraft_";
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t name[128]{};
        swprintf_s(name, L"%04d-%02d-%02d_%02d-%02d-%02d.mp4", st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond);
        p += name;
        return p.wstring();
    }
    return L"Minecraft_recording.mp4";
}

void SetStatus(const wchar_t* text) {
    if (g_status) SetWindowTextW(g_status, text);
}

void ToggleRecording() {
    if (g_recorder.IsRecording()) {
        g_recorder.Stop();
        SetStatus(L"Ready  |  Recording saved");
        SetWindowTextW(g_start, L"Start Recording (F8)");
        return;
    }

    HWND minecraft = FindMinecraftWindow();
    if (!minecraft) {
        SetStatus(L"Minecraft was not found. Start Minecraft first.");
        return;
    }

    if (!g_recorder.Start(minecraft, OutputPath(), 30, 1280)) {
        SetStatus(g_recorder.LastError().c_str());
        return;
    }

    SetStatus(L"RECORDING  |  720p target  |  30 FPS");
    SetWindowTextW(g_start, L"Stop Recording (F8)");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_CREATE:
        g_status = CreateWindowW(L"STATIC", L"Ready  |  Press F8 to record Minecraft",
            WS_CHILD | WS_VISIBLE, 24, 32, 390, 28, hwnd, (HMENU)ID_STATUS, nullptr, nullptr);
        g_start = CreateWindowW(L"BUTTON", L"Start Recording (F8)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 78, 210, 38, hwnd,
            (HMENU)ID_START, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Open Recordings",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 248, 78, 166, 38, hwnd,
            (HMENU)ID_OPEN, nullptr, nullptr);
        RegisterHotKey(hwnd, HOTKEY_ID, 0, VK_F8);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_START) ToggleRecording();
        if (LOWORD(wParam) == ID_OPEN) {
            wchar_t docs[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))) {
                std::filesystem::path p(docs); p /= L"Recorder";
                std::filesystem::create_directories(p);
                ShellExecuteW(hwnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
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
}

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
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 170, nullptr, nullptr, instance, nullptr);
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
