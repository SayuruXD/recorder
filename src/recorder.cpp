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

std::wstring FindFfmpeg() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exe(modulePath);
    auto local = exe.parent_path() / L"ffmpeg.exe";
    if (std::filesystem::exists(local)) return local.wstring();
    return L"ffmpeg.exe";
}
}

Recorder::~Recorder() { Stop(); }

bool Recorder::Start(HWND minecraftWindow, const std::wstring& outputPath, int fps, int maxWidth) {
    lastError_.clear();
    if (IsRecording()) { lastError_ = L"A recording is already running."; return false; }
    if (!IsWindow(minecraftWindow)) { lastError_ = L"Minecraft window was not found."; return false; }

    RECT r{};
    if (!GetWindowRect(minecraftWindow, &r)) { lastError_ = L"Could not read Minecraft window bounds."; return false; }

    int width = std::max(2L, r.right - r.left);
    int height = std::max(2L, r.bottom - r.top);
    if (maxWidth > 0 && width > maxWidth) {
        height = static_cast<int>((static_cast<double>(height) * maxWidth) / width);
        width = maxWidth;
    }
    width &= ~1;
    height &= ~1;

    std::filesystem::path out(outputPath);
    std::error_code ec;
    if (!out.parent_path().empty()) std::filesystem::create_directories(out.parent_path(), ec);

    std::wstringstream args;
    args << Quote(FindFfmpeg())
         << L" -hide_banner -loglevel error -y"
         << L" -f gdigrab -framerate " << fps
         << L" -offset_x " << r.left << L" -offset_y " << r.top
         << L" -video_size " << width << L"x" << height
         << L" -i desktop"
         << L" -c:v h264_qsv -global_quality 26 -look_ahead 0"
         << L" -pix_fmt yuv420p -movflags +faststart " << Quote(out.wstring());

    std::wstring command = args.str();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE stdinRead = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite_, &sa, 0)) {
        lastError_ = L"Could not create FFmpeg input pipe.";
        stdinWrite_ = nullptr;
        return false;
    }
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite_);
        stdinWrite_ = nullptr;
        lastError_ = L"Could not start FFmpeg. Put ffmpeg.exe beside Recorder.exe.";
        return false;
    }

    CloseHandle(stdinRead);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    return true;
}

void Recorder::Stop() {
    if (!process_) return;

    if (stdinWrite_) {
        const char quit = 'q';
        DWORD written = 0;
        WriteFile(stdinWrite_, &quit, 1, &written, nullptr);
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
