# PhoneCam - Android to Windows Virtual Webcam

Transform your Android phone into a high-quality webcam for your Windows PC/laptop. Inspired by DroidCam.

## Features

- **USB Connection** - Lowest latency via ADB port forwarding
- **WiFi Connection** - Wireless streaming on the same network
- **Bluetooth Connection** - No network required (lower quality)
- **Virtual Webcam Driver** - Appears as a regular webcam in Zoom, Teams, OBS, etc.
- **Camera Controls** - Flip camera, flash, auto-focus, zoom from PC
- **Adjustable Quality** - Resolution and bitrate control
- **Audio Streaming** - Use phone microphone as PC mic (optional)

## Architecture

```
ANDROID PHONE                              WINDOWS PC
┌──────────────┐                          ┌──────────────┐
│ CameraX API  │                          │ PhoneCamUI   │
│     ↓        │    USB / WiFi / BT       │ (WPF App)    │
│ MediaCodec   │ ──── TCP stream ────→    │     ↓        │
│ (H.264)      │                          │ PhoneCamSvc  │
│     ↓        │                          │ (Decoder)    │
│ TCP/BT Server│ ←── Control commands ──  │     ↓        │
│              │                          │ FrameBuffer  │
│ Streaming    │                          │ (SharedMem)  │
│ Service      │                          │     ↓        │
└──────────────┘                          │ DirectShow   │
                                          │ VirtualCam   │
                                          │     ↓        │
                                          │ Zoom/Teams/  │
                                          │ OBS/Chrome   │
                                          └──────────────┘
```

## Prerequisites

### Android Phone
- Android 8.0 (API 26) or later
- Camera access
- USB debugging enabled (for USB mode)

### Windows PC
- Windows 10/11 (64-bit)
- Visual Studio 2022 with C++ and .NET 8 workloads
- FFmpeg shared libraries (see build instructions)
- Android SDK Platform Tools (adb) - for USB mode
- CMake 3.20+

## Build Instructions

### 1. Android App

```bash
cd mobile-android
# Open in Android Studio and build, OR:
./gradlew assembleDebug
# Install on phone:
adb install app/build/outputs/apk/debug/app-debug.apk
```

### 2. Windows PC Client

#### Install FFmpeg
1. Download FFmpeg shared build from https://github.com/BtbN/FFmpeg-Builds/releases
2. Extract to `C:\ffmpeg`
3. Ensure `C:\ffmpeg\include` and `C:\ffmpeg\lib` exist

#### Build with CMake
```powershell
cd pc-client-windows
mkdir build ; cd build
cmake .. -DFFMPEG_DIR="C:\ffmpeg"
cmake --build . --config Release
```

#### Build WPF UI
```powershell
cd pc-client-windows\ui\PhoneCamUI
dotnet build -c Release
```

#### Register Virtual Camera
```powershell
# Run as Administrator
regsvr32 build\Release\PhoneCamDriver.dll
```

## Usage

### USB Mode (Recommended)
1. Enable USB Debugging on your Android phone
2. Connect phone to PC via USB cable
3. Install & open PhoneCam app on phone
4. Open PhoneCamUI on PC
5. Select "USB (via ADB)" and click Connect
6. Open Zoom/Teams and select "PhoneCam Virtual Camera"

### WiFi Mode
1. Connect phone and PC to the same WiFi network
2. Open PhoneCam app on phone - note the IP address shown
3. Open PhoneCamUI on PC
4. Select "WiFi", enter the phone's IP address
5. Click Connect

### Bluetooth Mode
1. Pair your phone with PC via Bluetooth settings
2. Open PhoneCam app on phone
3. Open PhoneCamUI on PC
4. Select "Bluetooth", enter the phone's MAC address
5. Click Connect

## Project Structure

```
PhoneCam/
├── mobile-android/          # Android app (Kotlin + Jetpack Compose)
│   ├── app/src/main/java/com/phonecam/
│   │   ├── camera/          # CameraX + MediaCodec encoder
│   │   ├── transport/       # USB/WiFi TCP + Bluetooth RFCOMM servers
│   │   ├── protocol/        # Wire protocol + control handler
│   │   ├── service/         # Foreground streaming service
│   │   └── ui/              # Compose UI
│   └── build.gradle.kts
│
├── pc-client-windows/       # Windows client
│   ├── driver/              # DirectShow virtual camera DLL
│   ├── service/             # Background decoder + transport (C++)
│   ├── ui/PhoneCamUI/       # WPF control panel (C#/.NET 8)
│   └── installer/           # Inno Setup installer script
│
├── shared/                  # Protocol specification
└── docs/                    # Documentation
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| ADB not found | Install Android SDK Platform Tools, add to PATH |
| PhoneCam not in webcam list | Run `regsvr32 PhoneCamDriver.dll` as admin |
| Connection timeout (USB) | Check USB debugging is enabled, run `adb devices` |
| Connection timeout (WiFi) | Ensure same network, check firewall allows port 4747 |
| Low FPS over Bluetooth | Reduce resolution to 640x480, BT bandwidth is limited |
| Black screen in Zoom | Ensure PhoneCamService is running before opening Zoom |

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Android Camera | CameraX (Jetpack), Camera2 API |
| Video Encoding | MediaCodec H.264 (hardware) |
| Android UI | Jetpack Compose + Material 3 |
| Android Transport | TCP Sockets, Bluetooth RFCOMM |
| PC Video Decoding | FFmpeg (libavcodec) |
| Virtual Webcam | DirectShow Source Filter (COM DLL) |
| IPC | Named Shared Memory (Win32) |
| PC UI | WPF (.NET 8) |
| PC Transport | Winsock2 TCP, Bluetooth RFCOMM |
| Installer | Inno Setup |
| Build (C++) | CMake |
| Build (Android) | Gradle + Kotlin |

## License

MIT
