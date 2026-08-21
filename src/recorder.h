#pragma once

#include <windows.h>
#include <string>

struct RecordingConfig {
    int fps = 30;
    int maxWidth = 1280;   // 0 = native/full resolution
    int maxHeight = 720;   // 0 = native/full resolution
    int quality = 26;
    bool captureCursor = true;
    bool lowEndMode = true;
};

class Recorder {
public:
    Recorder() = default;
    ~Recorder();

    bool Start(HWND targetWindow, const std::wstring& outputPath, const RecordingConfig& config);
    bool StartRegion(const RECT& captureRect, const std::wstring& outputPath, const RecordingConfig& config);
    bool Screenshot(const RECT& captureRect, const std::wstring& outputPath);
    void Stop();
    bool IsRecording() const { return process_ != nullptr; }
    const std::wstring& LastError() const { return lastError_; }
    const std::wstring& EncoderName() const { return encoderName_; }

private:
    bool Launch(const std::wstring& commandLine);
    bool LaunchEncoder(const RECT& captureRect, const std::wstring& outputPath, const RecordingConfig& config);
    bool IsProcessAlive() const;
    void CloseProcessHandles();

    HANDLE process_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    std::wstring lastError_;
    std::wstring encoderName_;
};
