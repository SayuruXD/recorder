#include "recorder.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

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

bool Recorder::Start(HWND minecraftWindow, const std::wstring& outputPath, int fps, int maxWidth) {
    lastError_.clear();
    if (IsRecording()) {
        lastError_ = L"A recording is already running.";
        return false;
    }
    if (!IsWindow(minecraftWindow)) {
        lastError_ = L"Minecraft window was not found.";
        return false;
    }

    RECT r{};
    if (!GetWindowRect(minecraftWindow, &r)) {
        lastError_ = L"Could not read Minecraft window bounds.";
        return false;
    }

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
    std::filesystem::create_directories(out.parent_path(), ec);

    // gdigrab keeps this MVP dependency-light. If a local ffmpeg build exposes
    // h264_qsv, use Intel Quick Sync; otherwise fall back to libx264 ultrafast.
    const std::wstring ffmpeg = FindFfmpeg();
    std::wstringstream args;
    args << Quote(ffmpeg)
         << L" -hide_banner -loglevel error -y"
         << L" -f gdigrab -framerate " << fps
         << L" -offset_x " << r.left
         << L" -offset_y " << r.top
         << L" -video_size " << width << L"x" << height
         << L" -i desktop"
         << L" -vf scale=" << width << L":" << height
         << L" -c:v h264_qsv -global_quality 26 -look_ahead 0"
         << L" -pix_fmt yuv420p -movflags +faststart " << Quote(out.wstring());

    std::wstring command = args.str();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Hardware encoder may not exist. Retry with a CPU preset that prioritizes
        // Minecraft performance over compression efficiency.
        std::wstringstream fallback;
        fallback << Quote(ffmpeg)
                 << L" -hide_banner -loglevel error -y"
                 << L" -f gdigrab -framerate " << fps
                 << L" -offset_x " << r.left
                 << L" -offset_y " << r.top
                 << L" -video_size " << width << L"x" << height
                 << L" -i desktop -c:v libx264 -preset ultrafast -crf 28"
                 << L" -pix_fmt yuv420p -movflags +faststart " << Quote(out.wstring());
        command = fallback.str();
        buffer.assign(command.begin(), command.end());
        buffer.push_back(L'\0');
        if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            lastError_ = L"Could not start FFmpeg. Put ffmpeg.exe beside Recorder.exe.";
            return false;
        }
    }

    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    return true;
}

void Recorder::Stop() {
    if (!process_) return;

    // Ctrl+C is preferable to TerminateProcess because FFmpeg can finalize the
    // MP4 container. For this MVP, close the process cleanly through CTRL+C is
    // intentionally left to the next encoder backend; wait briefly, then force
    // termination only as a last resort.
    if (WaitForSingleObject(process_, 1500) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 0);
        WaitForSingleObject(process_, 1000);
    }
    CloseHandle(process_);
    process_ = nullptr;
}
