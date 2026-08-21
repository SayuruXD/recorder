#pragma once

#include <string>
#include <windows.h>

class Recorder {
public:
    bool Start(HWND minecraftWindow, const std::wstring& outputPath, int fps = 30, int maxWidth = 1280);
    void Stop();
    bool IsRecording() const { return process_ != nullptr; }
    std::wstring LastError() const { return lastError_; }

private:
    HANDLE process_ = nullptr;
    std::wstring lastError_;
};
