# PhoneCam Windows PC Release Package

Last updated: 2026-05-28

## Built Artifacts

Portable package:

`dist\PhoneCam-PC-Portable`

Portable ZIP:

`dist\PhoneCam-PC-Portable.zip`

Self-contained package:

`dist\PhoneCam-PC-SelfContained`

Self-contained ZIP:

`dist\PhoneCam-PC-SelfContained.zip`

Main app executable:

`PhoneCamUI.exe`

## Which Package To Share

Use `PhoneCam-PC-SelfContained.zip` for normal users. It includes the .NET runtime files beside the app, so it is more likely to run on a clean Windows PC.

Use `PhoneCam-PC-Portable.zip` for development/testing if the PC already has .NET 8 Desktop Runtime installed.

## Required Files

- `PhoneCamUI.exe` - Windows control panel
- `PhoneCamUI.dll`, `.deps.json`, `.runtimeconfig.json` - WPF app runtime metadata
- `PhoneCamService.exe` - streaming transport, H.264 decoder, audio bridge
- `PhoneCamDriver.dll` - DirectShow virtual camera driver
- `app-debug.apk` - Android companion app installed by PhoneCamUI
- `avcodec-62.dll`
- `avdevice-62.dll`
- `avfilter-11.dll`
- `avformat-62.dll`
- `avutil-60.dll`
- `swresample-6.dll`
- `swscale-9.dll`
- `Register-PhoneCamDriver.ps1`
- `Unregister-PhoneCamDriver.ps1`
- `README-PC-PACKAGE.txt`

## First-Time Setup On A PC

1. Extract the ZIP.
2. Run PowerShell as Administrator.
3. Go to the extracted folder.
4. Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Register-PhoneCamDriver.ps1
```

5. Start:

```powershell
.\PhoneCamUI.exe
```

6. In Zoom, Teams, OBS, or Chrome, choose:

`PhoneCam Virtual Camera`

## USB Mode Requirement

USB mode needs Android SDK Platform Tools / `adb.exe`.

If ADB is not installed, install Android Platform Tools and make sure `adb.exe` is on PATH, or installed at:

`%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`

## Driver Notes

`PhoneCamDriver.dll` is a DirectShow virtual camera COM DLL. It must be registered once with administrator rights using `regsvr32`.

Register:

```powershell
powershell -ExecutionPolicy Bypass -File .\Register-PhoneCamDriver.ps1
```

Unregister:

```powershell
powershell -ExecutionPolicy Bypass -File .\Unregister-PhoneCamDriver.ps1
```

## Installer EXE

`dist\PhoneCamSetup-PC.exe`

The repository includes the Inno Setup script used to build it:

`pc-client-windows\installer\setup.iss`

The installer creates Start Menu shortcuts and includes a checked installer task to create a `PhoneCam` shortcut on the desktop. To rebuild it:

1. Use Inno Setup Compiler.
2. Make sure the package files exist.
3. Compile `setup.iss` with Inno Setup Compiler.

## Verification Performed

- Rebuilt native Windows targets with CMake:
  - `PhoneCamService.exe`
  - `PhoneCamDriver.dll`
- Published WPF UI:
  - framework-dependent package
  - self-contained package
- Added latest Android `app-debug.apk`
- Created ZIP packages
