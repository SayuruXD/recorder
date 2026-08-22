#include "recorder.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {
std::wstring Quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t c : value) { if (c == L'\"') out += L'\\'; out += c; }
    out += L"\"";
    return out;
}

std::wstring FindFfmpeg(std::wstring& detail) {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) { detail = L"Could not locate Recorder.exe."; return {}; }
    const auto local = std::filesystem::path(modulePath).parent_path() / L"ffmpeg.exe";
    if (std::filesystem::exists(local)) { detail = L"Using bundled FFmpeg."; return local.wstring(); }
    wchar_t resolved[MAX_PATH]{};
    const DWORD n = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, resolved, nullptr);
    if (n && n < MAX_PATH) { detail = L"Using FFmpeg from PATH."; return resolved; }
    detail = L"FFmpeg was not found beside Recorder.exe.";
    return {};
}

HWND FindMinecraftWindow() {
    HWND found = nullptr;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
        wchar_t title[512]{};
        GetWindowTextW(hwnd, title, 511);
        std::wstring s(title);
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return static_cast<wchar_t>(std::towlower(c)); });
        if (s.find(L"minecraft") == std::wstring::npos) return TRUE;
        *reinterpret_cast<HWND*>(param) = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool SameRect(const RECT& a, const RECT& b) {
    const int tolerance = 12;
    return std::abs(a.left-b.left) <= tolerance && std::abs(a.top-b.top) <= tolerance &&
           std::abs(a.right-b.right) <= tolerance && std::abs(a.bottom-b.bottom) <= tolerance;
}

std::wstring BuildMonitorInput(const RECT& r, int fps, bool cursor) {
    const int width = std::max(2L, r.right - r.left) & ~1;
    const int height = std::max(2L, r.bottom - r.top) & ~1;
    std::wstringstream s;
    s << L"gfxcapture=monitor_idx=0:max_framerate=" << std::clamp(fps, 15, 120)
      << L":capture_cursor=" << (cursor ? 1 : 0)
      << L":width=" << width << L":height=" << height;
    return s.str();
}

std::wstring BuildWindowInput(HWND hwnd, int fps, bool cursor) {
    std::wstringstream s;
    s << L"gfxcapture=hwnd=" << reinterpret_cast<uintptr_t>(hwnd)
      << L":capture_cursor=" << (cursor ? 1 : 0)
      << L":max_framerate=" << std::clamp(fps, 15, 120);
    return s.str();
}

HANDLE OpenNulHandle() {
    return CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
}

Recorder::~Recorder() { Stop(); }

bool Recorder::IsProcessAlive() const {
    if (!process_) return false;
    DWORD code = 0;
    return GetExitCodeProcess(process_, &code) && code == STILL_ACTIVE;
}

void Recorder::CloseProcessHandles() {
    if (stdinWrite_) { CloseHandle(stdinWrite_); stdinWrite_ = nullptr; }
    if (process_) { CloseHandle(process_); process_ = nullptr; }
}

bool Recorder::Launch(const std::wstring& commandLine) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE stdinRead = nullptr, stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0)) {
        lastError_ = L"Could not create the FFmpeg control pipe."; return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    HANDLE nulOut = OpenNulHandle(), nulErr = OpenNulHandle();
    if (nulOut == INVALID_HANDLE_VALUE || nulErr == INVALID_HANDLE_VALUE) {
        if (nulOut != INVALID_HANDLE_VALUE) CloseHandle(nulOut);
        if (nulErr != INVALID_HANDLE_VALUE) CloseHandle(nulErr);
        CloseHandle(stdinRead); CloseHandle(stdinWrite);
        lastError_ = L"Could not prepare FFmpeg output handles."; return false;
    }
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead; si.hStdOutput = nulOut; si.hStdError = nulErr;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end()); buffer.push_back(L'\0');
    const BOOL ok = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | HIGH_PRIORITY_CLASS,
                                   nullptr, nullptr, &si, &pi);
    CloseHandle(stdinRead); CloseHandle(nulOut); CloseHandle(nulErr);
    if (!ok) {
        const DWORD error = GetLastError(); CloseHandle(stdinWrite);
        lastError_ = L"Windows could not start FFmpeg (error " + std::to_wstring(error) + L").";
        return false;
    }
    CloseHandle(pi.hThread); process_ = pi.hProcess; stdinWrite_ = stdinWrite; return true;
}

bool Recorder::LaunchEncoder(const RECT& captureRect, const std::wstring& outputPath,
                             const RecordingConfig& config) {
    RECT r = captureRect;
    if (r.right <= r.left || r.bottom <= r.top) { lastError_ = L"The selected capture region is invalid."; return false; }
    int width = std::max(2L, r.right - r.left) & ~1;
    int height = std::max(2L, r.bottom - r.top) & ~1;
    int outputWidth = width, outputHeight = height;
    if (config.maxWidth > 0 && config.maxHeight > 0 && (width > config.maxWidth || height > config.maxHeight)) {
        const double scale = std::min(static_cast<double>(config.maxWidth) / width,
                                      static_cast<double>(config.maxHeight) / height);
        outputWidth = std::max(2, static_cast<int>(width * scale)) & ~1;
        outputHeight = std::max(2, static_cast<int>(height * scale)) & ~1;
    }
    std::filesystem::path out(outputPath); std::error_code ec;
    if (!out.parent_path().empty()) std::filesystem::create_directories(out.parent_path(), ec);
    if (ec) { lastError_ = L"Could not create the recordings folder."; return false; }
    std::wstring detail; const std::wstring ffmpeg = FindFfmpeg(detail);
    if (ffmpeg.empty()) { lastError_ = detail; return false; }

    const int fps = std::clamp(config.fps, 15, 120);
    HWND minecraft = FindMinecraftWindow();
    RECT minecraftRect{};
    const bool useWindowCapture = minecraft && GetWindowRect(minecraft, &minecraftRect) && SameRect(minecraftRect, r);
    const std::wstring input = useWindowCapture
        ? BuildWindowInput(minecraft, fps, config.captureCursor)
        : BuildMonitorInput(r, fps, config.captureCursor);

    std::wstringstream cmd;
    cmd << Quote(ffmpeg)
        << L" -hide_banner -loglevel error -y"
        << L" -init_hw_device qsv=hw,child_device_type=d3d11va"
        << L" -filter_hw_device hw"
        << L" -f lavfi -i " << Quote(input);

    if (outputWidth != width || outputHeight != height)
        cmd << L" -vf " << Quote(L"scale_qsv=w=" + std::to_wstring(outputWidth) + L":h=" + std::to_wstring(outputHeight));

    cmd << L" -an -fps_mode cfr -r " << fps
        << L" -c:v h264_qsv -preset veryfast"
        << L" -global_quality " << std::clamp(config.quality, 18, 28)
        << L" -bf 0 -g " << std::clamp(fps * 2, 30, 240)
        << L" -pix_fmt nv12 -movflags +faststart"
        << L" -f mp4 " << Quote(out.wstring());

    encoderName_ = useWindowCapture
        ? L"H.264 Intel Quick Sync + Windows Graphics Capture"
        : L"H.264 Intel Quick Sync + Windows Graphics Capture (monitor)";
    return Launch(cmd.str());
}

bool Recorder::Start(HWND targetWindow, const std::wstring& outputPath, const RecordingConfig& config) {
    if (!IsWindow(targetWindow)) { lastError_ = L"Capture source was not found."; return false; }
    RECT r{}; if (!GetWindowRect(targetWindow, &r)) { lastError_ = L"Could not read capture bounds."; return false; }
    return StartRegion(r, outputPath, config);
}

bool Recorder::StartRegion(const RECT& captureRect, const std::wstring& outputPath, const RecordingConfig& config) {
    lastError_.clear(); encoderName_.clear();
    if (IsRecording()) { lastError_ = L"A recording is already running."; return false; }
    if (!LaunchEncoder(captureRect, outputPath, config)) return false;
    Sleep(500);
    if (IsProcessAlive()) return true;
    CloseProcessHandles();
    if (lastError_.empty()) lastError_ = L"FFmpeg stopped immediately. The recording could not be started.";
    return false;
}

bool Recorder::Screenshot(const RECT& captureRect, const std::wstring& outputPath) {
    if (IsRecording()) { lastError_ = L"Stop recording before taking a screenshot."; return false; }
    RECT r = captureRect;
    const int width = r.right - r.left, height = r.bottom - r.top;
    if (width <= 0 || height <= 0) { lastError_ = L"The screenshot region is invalid."; return false; }
    std::wstring detail; const auto ffmpeg = FindFfmpeg(detail);
    if (ffmpeg.empty()) { lastError_ = detail; return false; }
    std::filesystem::path out(outputPath); std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    HWND minecraft = FindMinecraftWindow();
    RECT mr{};
    const bool useWindowCapture = minecraft && GetWindowRect(minecraft, &mr) && SameRect(mr, r);
    const std::wstring input = useWindowCapture ? BuildWindowInput(minecraft, 1, true) : BuildMonitorInput(r, 1, true);
    std::wstringstream cmd;
    cmd << Quote(ffmpeg) << L" -hide_banner -loglevel error -y"
        << L" -init_hw_device qsv=hw,child_device_type=d3d11va"
        << L" -filter_hw_device hw"
        << L" -f lavfi -i " << Quote(input)
        << L" -frames:v 1 -vf " << Quote(L"hwdownload,format=bgra")
        << L" -pix_fmt bgra -update 1 " << Quote(out.wstring());
    if (!Launch(cmd.str())) return false;
    if (WaitForSingleObject(process_, 10000) != WAIT_OBJECT_0) {
        TerminateProcess(process_, 1); WaitForSingleObject(process_, 1000);
    }
    DWORD code = 1; GetExitCodeProcess(process_, &code);
    CloseProcessHandles();
    if (code != 0) { lastError_ = L"FFmpeg could not create the screenshot."; return false; }
    return true;
}

void Recorder::Stop() {
    if (!process_) return;
    if (stdinWrite_) {
        const char quit[] = "q\n"; DWORD written = 0;
        WriteFile(stdinWrite_, quit, 2, &written, nullptr); FlushFileBuffers(stdinWrite_);
        CloseHandle(stdinWrite_); stdinWrite_ = nullptr;
    }
    if (WaitForSingleObject(process_, 5000) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 1); WaitForSingleObject(process_, 1000);
    }
    CloseHandle(process_); process_ = nullptr;
}
