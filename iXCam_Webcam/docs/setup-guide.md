# PhoneCam Setup Guide

## Quick Start

### Step 1: Build & Install Android App

1. Open `mobile-android/` in Android Studio
2. Connect your Android phone via USB
3. Enable USB Debugging: Settings → Developer Options → USB Debugging
4. Click Run (▶) in Android Studio
5. Grant Camera and Audio permissions when prompted

### Step 2: Build Windows Components

#### Prerequisites
- Visual Studio 2022 (Community or higher)
  - Desktop development with C++ workload
  - .NET desktop development workload
- CMake 3.20+
- FFmpeg 6.x shared libraries

#### FFmpeg Setup
```powershell
# Download FFmpeg shared build
Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip" -OutFile ffmpeg.zip
Expand-Archive ffmpeg.zip -DestinationPath C:\
Rename-Item "C:\ffmpeg-master-latest-win64-gpl-shared" "C:\ffmpeg"
```

#### Build Service (C++)
```powershell
cd PhoneCam\pc-client-windows
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -DFFMPEG_DIR="C:\ffmpeg"
cmake --build . --config Release
```

#### Build UI (C#)
```powershell
cd PhoneCam\pc-client-windows\ui\PhoneCamUI
dotnet publish -c Release -r win-x64 --self-contained
```

### Step 3: Register Virtual Camera Driver
```powershell
# Run PowerShell as Administrator
regsvr32 "PhoneCam\pc-client-windows\build\Release\PhoneCamDriver.dll"
```

### Step 4: Connect

#### USB Connection
```powershell
# Verify device is connected
adb devices

# The app handles port forwarding automatically, or manually:
adb forward tcp:4747 tcp:4747
```

#### WiFi Connection
1. Both devices on the same WiFi network
2. Note the IP shown in the PhoneCam Android app
3. Enter it in PhoneCamUI

## Developer Notes

### Debug the Service Manually
```powershell
cd build\Release
.\PhoneCamService.exe --usb
# or
.\PhoneCamService.exe --wifi 192.168.1.100
# or
.\PhoneCamService.exe --bluetooth AA:BB:CC:DD:EE:FF
```

### Verify Virtual Camera
Open Device Manager → Imaging Devices → should show "PhoneCam Virtual Camera"

Or use a test in PowerShell:
```powershell
# List DirectShow video devices
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Caption -like "*camera*" -or $_.Caption -like "*PhoneCam*" }
```

### Uninstall Virtual Camera
```powershell
regsvr32 /u "PhoneCam\pc-client-windows\build\Release\PhoneCamDriver.dll"
```
