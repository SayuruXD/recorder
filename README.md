# Recorder 🎥

A lightweight Minecraft-focused screen recorder for Windows.

## MVP

- Window capture using Windows Graphics Capture
- Minecraft window detection/selection
- Start/stop recording
- F8 global hotkey
- 720p / 30 FPS low-end preset
- MP4/H.264 output
- Capture and encode off the UI thread

## Architecture

The first implementation uses a native Windows desktop stack. Windows Graphics Capture provides application-window frames, while the encoding layer is kept separate so hardware encoders can be added without changing capture logic.

## Build

Requirements:

- Windows 10 version 1809 or later
- Visual Studio 2022 with Desktop C++ workload
- Windows 10/11 SDK

The MVP project is provided as a CMake-based C++ application.
