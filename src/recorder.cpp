#include "recorder.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {

std::wstring Quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        if (c == L'\"') out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

std::wstring FindFfmpeg(std::wstring& detail) {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        detail = L"Could not locate Recorder.exe.";
        return {};
    }

    const std::filesystem::path exe(modulePath);
    const auto local = exe.parent_path() / L"ffmpeg.exe";
    if (std::filesystem::exists(local)) {
        detail = L"Using bundled FFmpeg.";
        return local.wstring();
    }

    wchar_t resolved[MAX_PATH]{};
    const DWORD n = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, resolved, nullptr);
    if (n > 0 && n < MAX_PATH) {
        detail = L"Using FFmpeg from PATH.";
        return std::wstring(resolved);
    }

    detail = L"FFmpeg was not found beside Recorder.exe. Keep ffmpeg.exe in the same folder.";
    return {};
}

std::wstring BuildCaptureInput(const RECT& r, int fps, bool cursor) {
    const int width = std::max(2L, r.right - r.left) & ~1;
    const int height = std::max(2L, r.bottom - r.top) & ~1;
    std::wstringstream s;
    s << L"ddagrab=framerate=" << std::clamp(fps, 15, 60)
      << L":draw_mouse=" << (cursor ? 1 : 0)
      << L":video_size=" << width << L"x" << height
      << L":offset_x=" << r.left << L":offset_y=" << r.top;
    return s.str();
}

HANDLE OpenNulHandle() {
    return CreateFileW(L"NUL", GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

} // namespace

Recorder::~Recorder() { Stop(); }

bool Recorder::IsProcessAlive() const {
    if (!process_) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(process_, &code)) return false;
    return code == STILL_ACTIVE;
}

void Recorder::CloseProcessHandles() {
    if (stdinWrite_) {
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
}

bool Recorder::Launch(const std::wstring& commandLine) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0)) {
        lastError_ = L"Could not create the FFmpeg control pipe.";
        return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulOut = OpenNulHandle();
    HANDLE nulErr = OpenNulHandle();
    if (nulOut == INVALID_HANDLE_VALUE || nulErr == INVALID_HANDLE_VALUE) {
        if (nulOut != INVALID_HANDLE_VALUE) CloseHandle(nulOut);
        if (nulErr != INVALID_HANDLE_VALUE) CloseHandle(nulErr);
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        lastError_ = L"Could not prepare FFmpeg output handles.";
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = nulOut;
    si.hStdError = nulErr;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    const BOOL ok = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                                   nullptr, nullptr, &si, &pi);

    CloseHandle(stdinRead);
    CloseHandle(nulOut);
    CloseHandle(nulErr);

    if (!ok) {
        const DWORD error = GetLastError();
        CloseHandle(stdinWrite);
        lastError_ = L"Windows could not start FFmpeg (error " + std::to_wstring(error) + L").";
        return false;
    }

    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    stdinWrite_ = stdinWrite;
    return true;
}

bool Recorder::LaunchEncoder(HWND targetWindow, const std::wstring& outputPath,
                             const RecordingConfig& config, bool qsv) {
    if (IsIconic(targetWindow)) {
        lastError_ = L"Minecraft is minimized. Restore the game before recording.";
        return false;
    }

    RECT r{};
    if (!GetWindowRect(targetWindow, &r) || r.right <= r.left || r.bottom <= r.top) {
        lastError_ = L"Could not read the Minecraft window bounds.";
        return false;
    }

    int width = std::max(2L, r.right - r.left) & ~1;
    int height = std::max(2L, r.bottom - r.top) & ~1;

    // Keep the capture dimensions even. H.264 encoders and MP4 playback are much
    // more reliable when the input is explicitly converted to a standard 8-bit
    // YUV420 format.
    if (config.maxWidth > 0 && config.maxHeight > 0 &&
        width > config.maxWidth && height > config.maxHeight) {
        const double scale = std::min(static_cast<double>(config.maxWidth) / width,
                                      static_cast<double>(config.maxHeight) / height);
        const int targetW = std::max(2, static_cast<int>(width * scale)) & ~1;
        const int targetH = std::max(2, static_cast<int>(height * scale)) & ~1;
        r.left += (width - targetW) / 2;
        r.top += (height - targetH) / 2;
        r.right = r.left + targetW;
        r.bottom = r.top + targetH;
        width = targetW;
        height = targetH;
    }

    std::filesystem::path out(outputPath);
    std::error_code ec;
    if (!out.parent_path().empty()) std::filesystem::create_directories(out.parent_path(), ec);
    if (ec) {
        lastError_ = L"Could not create the recordings folder.";
        return false;
    }

    std::wstring ffmpegDetail;
    const std::wstring ffmpeg = FindFfmpeg(ffmpegDetail);
    if (ffmpeg.empty()) {
        lastError_ = ffmpegDetail;
        return false;
    }

    const std::wstring input = BuildCaptureInput(r, config.fps, config.captureCursor);

    std::wstringstream cmd;
    cmd << Quote(ffmpeg)
        << L" -hide_banner -loglevel error -nostdin -y"
        << L" -f lavfi -i " << Quote(input)
        // ddagrab supplies D3D11 frames. hwdownload makes the fallback path
        // universally decodable by software H.264 and prevents malformed MP4s.
        << L" -vf " << Quote(L"hwdownload,format=bgra,scale=" +
                               std::to_wstring(width) + L":" + std::to_wstring(height) +
                               L":flags=fast_bilinear,format=yuv420p")
        << L" -an -r " << std::clamp(config.fps, 15, 60);

    // The software path is intentionally the reliability baseline. QSV is used
    // only when explicitly requested by a future hardware-verified pipeline.
    // This avoids producing files that some Intel generations cannot finalize.
    (void)qsv;
    cmd << L" -c:v libx264 -preset ultrafast -tune zerolatency"
        << L" -crf " << std::clamp(config.quality + 2, 20, 32)
        << L" -threads 2 -bf 0 -g " << std::clamp(config.fps * 2, 30, 120)
        << L" -pix_fmt yuv420p"
        << L" -movflags +faststart"
        << L" -f mp4 " << Quote(out.wstring());

    encoderName_ = L"H.264 low-end compatibility mode";
    return Launch(cmd.str());
}

bool Recorder::Start(HWND targetWindow, const std::wstring& outputPath,
                     const RecordingConfig& config) {
    lastError_.clear();
    encoderName_.clear();

    if (IsRecording()) {
        lastError_ = L"A recording is already running.";
        return false;
    }
    if (!IsWindow(targetWindow)) {
        lastError_ = L"Minecraft window was not found.";
        return false;
    }

    // One deterministic pipeline is preferable to producing a corrupt file and
    // guessing which hardware encoder failed. It can be optimized later after
    // the actual target machines have been validated.
    if (LaunchEncoder(targetWindow, outputPath, config, false)) {
        Sleep(500);
        if (IsProcessAlive()) return true;
        CloseProcessHandles();
    }

    if (lastError_.empty()) {
        lastError_ = L"FFmpeg stopped immediately. The recording could not be started.";
    }
    return false;
}

void Recorder::Stop() {
    if (!process_) return;

    // FFmpeg's -nostdin prevents normal stdin interaction, so send a graceful
    // process signal by closing the input stream first and then wait briefly.
    if (stdinWrite_) {
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
    }

    if (WaitForSingleObject(process_, 5000) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 1000);
    }

    CloseHandle(process_);
    process_ = nullptr;
}
