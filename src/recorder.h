#pragma once

#include <windows.h>
#include <string>

struct RecordingConfig {
    int fps = 30;
    int maxWidth = 1280;
    int maxHeight = 720;
    int quality = 26;
    bool captureCursor = true;
    bool lowEndMode = true;
};

class Recorder {
public:
    Recorder() = default;
    ~Recorder();

    bool Start(HWND targetWindow, const std::wstring& outputPath, const RecordingConfig& config);
    void Stop();
    bool IsRecording() const { return process_ != nullptr; }
    const std::wstring& LastError() const { return lastError_; }
    const std::wstring& EncoderName() const { return encoderName_; }

private:
    bool Launch(const std::wstring& commandLine);
    bool LaunchEncoder(HWND targetWindow, const std::wstring& outputPath, const RecordingConfig& config, bool qsv);
    bool IsProcessAlive() const;
    void CloseProcessHandles();
    std::wstring FindFfmpeg(std::wstring& detail) const;

    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    std::wstring lastError_;
    std::wstring encoderName_;
};
