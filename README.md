# iXPhoneCam

iXPhoneCam turns an Android phone into a Windows webcam and microphone source. The Android app captures camera and audio data, streams it to a Windows PC over USB, WiFi, or Bluetooth, and the Windows side exposes the video as a normal virtual camera for apps such as Zoom, Microsoft Teams, OBS, browsers, and other video tools.

The repository currently contains two closely related project folders:

- `PhoneCam/` - the active, production-hardened version with additional docs, setup scripts, installer notes, and audio work.
- `iXCam_Webcam/` - the earlier/parallel project tree with the same core Android-to-Windows webcam architecture.

For new development, start with `PhoneCam/`.

## What It Does

- Uses the Android phone camera as a Windows virtual webcam.
- Streams H.264 video from Android to Windows.
- Streams phone microphone audio to the PC.
- Supports reverse audio experiments, where PC audio can be sent back to the phone.
- Supports USB, WiFi, and Bluetooth connection modes.
- Provides a Windows desktop control panel.
- Registers a DirectShow virtual camera so normal Windows apps can select it.
- Allows remote camera controls from the PC side.
- Includes production hardening for logging, error handling, validation, reconnect behavior, and diagnostics.

## Main Features

| Feature | Description |
|---|---|
| USB mode | Uses ADB port forwarding for low-latency local streaming over a USB cable. |
| WiFi mode | Connects over the local network by using the phone IP address and TCP port `4747`. |
| Bluetooth mode | Uses Bluetooth RFCOMM for cases where USB/WiFi are not available. |
| Virtual webcam | Windows DirectShow source filter appears as `PhoneCam Virtual Camera`. |
| Android preview | Android app uses CameraX/Compose for capture, preview, and controls. |
| H.264 video | Android hardware encoding through MediaCodec, decoded on Windows with FFmpeg. |
| Audio streaming | Android AudioRecord captures microphone PCM audio for PC playback. |
| PC control UI | WPF/.NET control app starts the service and sends camera/control commands. |
| Camera controls | Flip camera, flash, focus, keyframe request, resolution, bitrate, FPS, and audio toggles. |
| Resilience | Structured logging, error codes, reconnection, validation, and graceful degradation. |

## Architecture

```text
Android Phone
  CameraX + AudioRecord
        |
        v
  MediaCodec H.264 Encoder
        |
        v
  PhoneCam Binary Protocol
        |
        v
  USB ADB / WiFi TCP / Bluetooth RFCOMM
        |
        v
Windows PC
  PhoneCamService.exe
        |
        v
  FFmpeg Decoder + waveOut Audio Playback
        |
        v
  Win32 Shared Memory FrameBuffer
        |
        v
  DirectShow Virtual Camera Driver
        |
        v
  Zoom / Teams / OBS / Browser Apps
```

## Tech Stack

### Android App

| Technology | Purpose |
|---|---|
| Kotlin | Android app logic, services, protocol, transport, and UI state. |
| Android SDK | Permissions, lifecycle, networking, Bluetooth, audio, and services. |
| Jetpack Compose | Android UI. |
| Material 3 | Android UI components and styling. |
| CameraX / Camera2 | Camera capture, preview, front/back camera support, frame delivery. |
| MediaCodec | Hardware H.264 video encoding. |
| AudioRecord | Phone microphone capture. |
| Foreground Service | Keeps streaming active with a persistent notification. |
| TCP ServerSocket | USB/WiFi streaming server on port `4747`. |
| Bluetooth RFCOMM | Bluetooth streaming transport. |
| Gradle Kotlin DSL | Android build configuration. |

### Windows PC

| Technology | Purpose |
|---|---|
| C++17 | Native Windows service, decoding pipeline, transport, driver support. |
| C# | Windows desktop UI logic. |
| WPF | PC control panel UI. |
| .NET 8 | Runtime and build target for the WPF app. |
| Win32 API | Shared memory, handles, sockets, audio APIs, process control. |
| Winsock2 | TCP connections for USB-forwarded and WiFi modes. |
| ADB | USB bridge through `adb forward tcp:4747 tcp:4747`. |
| Windows Bluetooth APIs | Bluetooth device and RFCOMM support. |
| FFmpeg | H.264 video decoding and pixel conversion. |
| DirectShow / COM | Virtual webcam driver registration and camera output. |
| waveOut API | PC-side audio playback. |
| WASAPI loopback | Reverse-audio capture work. |
| CMake + MSVC | Windows C++ build. |
| Inno Setup | Windows installer packaging. |
| PowerShell | Setup and USB helper scripts. |

### Shared Protocol

| Item | Description |
|---|---|
| Custom binary protocol | Carries video, audio, heartbeat, control, and reverse-audio packets. |
| Magic bytes | `0xCAFE` validates packet headers. |
| Big-endian fields | Keeps packet encoding consistent across Android and Windows. |
| Timestamp fields | Preserve media timing. |
| Control packets | Carry camera and streaming commands from PC to Android. |

See [`PhoneCam/shared/protocol.md`](PhoneCam/shared/protocol.md) for protocol details.

## Repository Layout

```text
.
|-- PhoneCam/
|   |-- mobile-android/              Android app
|   |   |-- app/src/main/java/com/phonecam/
|   |   |   |-- camera/              Camera, video encoder, audio capture
|   |   |   |-- protocol/            Packet format and control handling
|   |   |   |-- service/             Foreground streaming service
|   |   |   |-- transport/           USB/WiFi TCP and Bluetooth transports
|   |   |   `-- ui/                  Jetpack Compose UI
|   |-- pc-client-windows/
|   |   |-- driver/                 DirectShow virtual camera DLL
|   |   |-- service/                C++ receiver, decoder, audio, transport
|   |   |-- ui/PhoneCamUI/          WPF desktop control app
|   |   `-- installer/              Inno Setup installer script
|   |-- shared/                     Protocol specification
|   |-- tools/                      PowerShell helper scripts
|   `-- docs/                       Setup, hardening, troubleshooting docs
|
|-- iXCam_Webcam/                   Earlier/parallel project tree
|-- meet-screen.png                 Screenshot/reference image
`-- README.md                       This file
```

Generated outputs such as `build/`, `.gradle/`, `bin/`, `obj/`, release ZIP/APK/AAB files, logs, and `graphify-out/` are intentionally ignored.

## Requirements

### Android

- Android phone running Android 8.0/API 26 or newer.
- Camera and microphone permissions.
- USB debugging enabled for USB mode.
- Android Studio or Android SDK/Gradle tooling.
- Android SDK Platform Tools for `adb`.

### Windows

- Windows 10 or Windows 11, 64-bit.
- Visual Studio 2022 with:
  - Desktop development with C++ workload.
  - .NET desktop development workload.
- .NET 8 SDK/runtime.
- CMake 3.20 or newer.
- FFmpeg shared libraries.
- Android SDK Platform Tools for USB mode.
- Administrator access for registering/unregistering the DirectShow driver.

## Build Instructions

### 1. Build the Android App

```powershell
cd PhoneCam\mobile-android
.\gradlew.bat assembleDebug
```

Install the debug APK:

```powershell
adb install app\build\outputs\apk\debug\app-debug.apk
```

You can also open `PhoneCam/mobile-android/` in Android Studio and run the app directly.

### 2. Install FFmpeg on Windows

Download a Windows shared FFmpeg build and place it at `C:\ffmpeg` so these folders exist:

```text
C:\ffmpeg\include
C:\ffmpeg\lib
C:\ffmpeg\bin
```

One common source is the BtbN FFmpeg builds:

```powershell
Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip" -OutFile ffmpeg.zip
Expand-Archive ffmpeg.zip -DestinationPath C:\
```

Rename the extracted folder to `C:\ffmpeg` if needed.

### 3. Build the Windows C++ Service and Driver

```powershell
cd PhoneCam\pc-client-windows
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -DFFMPEG_DIR="C:\ffmpeg"
cmake --build . --config Release
```

Expected outputs include:

- `PhoneCamService.exe`
- `PhoneCamDriver.dll`

### 4. Build the Windows WPF UI

```powershell
cd PhoneCam\pc-client-windows\ui\PhoneCamUI
dotnet build -c Release
```

For a self-contained release build:

```powershell
dotnet publish -c Release -r win-x64 --self-contained
```

### 5. Register the Virtual Camera

Run PowerShell or Command Prompt as Administrator:

```powershell
regsvr32 "PhoneCam\pc-client-windows\build\Release\PhoneCamDriver.dll"
```

To unregister:

```powershell
regsvr32 /u "PhoneCam\pc-client-windows\build\Release\PhoneCamDriver.dll"
```

## How to Use

### USB Mode

USB is the recommended mode for lowest latency.

1. Enable Developer Options and USB Debugging on the Android phone.
2. Connect the phone to the Windows PC with a USB cable.
3. Verify the phone is visible:

   ```powershell
   adb devices
   ```

4. Open the PhoneCam Android app and grant permissions.
5. Start the Windows PhoneCam UI.
6. Select USB mode and connect.
7. If needed, manually forward the port:

   ```powershell
   adb forward tcp:4747 tcp:4747
   ```

8. Open Zoom, Teams, OBS, or another app and select `PhoneCam Virtual Camera`.

### WiFi Mode

1. Connect the phone and PC to the same WiFi network.
2. Open the Android app and note the displayed phone IP address.
3. Open the Windows PhoneCam UI.
4. Select WiFi mode.
5. Enter the phone IP address.
6. Connect.
7. Select `PhoneCam Virtual Camera` in your video app.

Default port: `4747`.

### Bluetooth Mode

1. Pair the Android phone with the Windows PC in Bluetooth settings.
2. Open the Android PhoneCam app.
3. Open the Windows PhoneCam UI.
4. Select Bluetooth mode.
5. Choose or enter the paired phone Bluetooth address.
6. Connect.

Bluetooth has lower bandwidth than USB/WiFi, so use a lower resolution or FPS if streaming is unstable.

## Manual Service Commands

The Windows service can also be started manually for debugging:

```powershell
cd PhoneCam\pc-client-windows\build\Release
.\PhoneCamService.exe --usb
.\PhoneCamService.exe --wifi 192.168.1.100
.\PhoneCamService.exe --bluetooth AA:BB:CC:DD:EE:FF
```

Common runtime commands sent from the PC UI/service command channel include:

```text
flip
rotate
flash
focus-on
focus-off
keyframe
resolution 1280 720
bitrate 4000000
fps 30
audio-on
audio-off
```

## Production Hardening

The `PhoneCam/` tree includes production-oriented improvements:

- Structured logs with levels and rotation.
- Error code framework with recovery hints.
- RAII and smart pointer usage in native code.
- Packet and frame validation.
- Socket timeouts.
- Reconnect behavior with exponential backoff.
- Graceful degradation when optional audio or device features fail.
- Operator-focused troubleshooting docs.

Important docs:

- [`PhoneCam/docs/PROJECT_INDEX.md`](PhoneCam/docs/PROJECT_INDEX.md)
- [`PhoneCam/docs/PRODUCTION_SETUP.md`](PhoneCam/docs/PRODUCTION_SETUP.md)
- [`PhoneCam/docs/PRODUCTION_HARDENING.md`](PhoneCam/docs/PRODUCTION_HARDENING.md)
- [`PhoneCam/docs/ERROR_REFERENCE.md`](PhoneCam/docs/ERROR_REFERENCE.md)
- [`PhoneCam/docs/QUICK_REFERENCE.md`](PhoneCam/docs/QUICK_REFERENCE.md)
- [`PhoneCam/docs/AUDIO_STREAMING_LESSONS.md`](PhoneCam/docs/AUDIO_STREAMING_LESSONS.md)

## Troubleshooting

| Problem | What to Check |
|---|---|
| `adb` is not found | Install Android SDK Platform Tools and add them to `PATH`. |
| Phone is not listed by `adb devices` | Enable USB debugging, accept the phone authorization prompt, reconnect cable. |
| USB connection fails | Run `adb forward tcp:4747 tcp:4747` and verify the Android app is running. |
| WiFi connection fails | Confirm both devices are on the same network and Windows firewall allows port `4747`. |
| Bluetooth is slow or unstable | Lower resolution/FPS; Bluetooth bandwidth is limited. |
| Virtual camera is missing | Register `PhoneCamDriver.dll` with Administrator privileges using `regsvr32`. |
| Black screen in meeting app | Start PhoneCam service/UI before opening the meeting app, then reselect the camera. |
| Audio is missing | Grant microphone permission on Android and enable audio streaming in the UI. |
| FFmpeg build errors | Confirm `C:\ffmpeg\include`, `C:\ffmpeg\lib`, and FFmpeg DLLs are available. |

## Development Notes

- Keep source changes in `PhoneCam/` unless you intentionally need to update the older `iXCam_Webcam/` tree too.
- Do not commit generated folders such as `build/`, `.gradle/`, `bin/`, `obj/`, logs, release ZIPs, APKs, AABs, or Graphify output.
- The root `.gitignore` is configured to keep large/generated artifacts out of GitHub.
- If you produce release packages, distribute them through GitHub Releases instead of committing binaries to the repository.

## Documentation Map

| File | Purpose |
|---|---|
| [`PhoneCam/README.md`](PhoneCam/README.md) | Project-specific overview and quick build/use instructions. |
| [`PhoneCam/TECH_STACK_README.md`](PhoneCam/TECH_STACK_README.md) | Detailed technology stack and runtime behavior. |
| [`PhoneCam/shared/protocol.md`](PhoneCam/shared/protocol.md) | Wire protocol specification. |
| [`PhoneCam/docs/setup-guide.md`](PhoneCam/docs/setup-guide.md) | Step-by-step setup guide. |
| [`PhoneCam/docs/PRODUCTION_SETUP.md`](PhoneCam/docs/PRODUCTION_SETUP.md) | Deployment and production setup guide. |
| [`PhoneCam/docs/ERROR_REFERENCE.md`](PhoneCam/docs/ERROR_REFERENCE.md) | Error code and recovery reference. |
| [`PhoneCam/docs/IMPLEMENTATION_CHANGES.md`](PhoneCam/docs/IMPLEMENTATION_CHANGES.md) | Summary of implementation changes. |
| [`PhoneCam/docs/BEFORE_AFTER_COMPARISON.md`](PhoneCam/docs/BEFORE_AFTER_COMPARISON.md) | Architecture and behavior comparison. |

## License

This project is marked as MIT in the existing project documentation.

