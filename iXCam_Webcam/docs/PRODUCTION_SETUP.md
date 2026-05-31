# PhoneCam Production Configuration & Deployment Guide

## Prerequisites Checklist

### System Requirements
- [ ] Windows 10/11 (64-bit) with latest Windows Update
- [ ] Visual C++ Redistributable 2022 installed
- [ ] 2 GB available disk space for logs
- [ ] Network access to phone (USB/WiFi/Bluetooth as needed)

### Software Dependencies
- [ ] FFmpeg 6.x shared libraries at `C:\ffmpeg`
  - `C:\ffmpeg\bin\*.dll` (runtime libraries)
  - `C:\ffmpeg\include\` (headers)
  - `C:\ffmpeg\lib\*.lib` (static libraries for linking)
- [ ] Android SDK Platform Tools (for USB mode): `adb` accessible in PATH
- [ ] DirectShow development headers (Windows SDK)

### Permissions
- [ ] PhoneCamService runs as current user or service account
- [ ] `%ProgramData%\PhoneCam` directory readable/writable
- [ ] `HKLM\SYSTEM\CurrentControlSet\Services\PhoneCam` registry key (if deployed as service)

---

## Installation Steps

### Step 1: Prepare Environment

```powershell
# Create log directory
$logDir = "$env:ProgramData\PhoneCam"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# Verify FFmpeg
if (!(Test-Path "C:\ffmpeg\bin\avcodec-61.dll")) {
    Write-Error "FFmpeg not found at C:\ffmpeg\bin"
    exit 1
}

# Add to PATH if needed
$env:Path += ";C:\ffmpeg\bin"
```

### Step 2: Register Virtual Camera Driver

```powershell
# Run as Administrator
$driverDll = "C:\PhoneCam\build\Release\PhoneCamDriver.dll"

# Register
regsvr32 /s $driverDll
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to register driver"
    exit 1
}

# Verify registration
Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.Caption -like "*PhoneCam*" -or $_.Caption -like "*Virtual Camera*"
} | Select-Object Caption, Status
```

### Step 3: Test Service Startup

```powershell
# Test USB mode
C:\PhoneCam\build\Release\PhoneCamService.exe --usb

# Monitor output for errors
# Expected: "Connected to phone!"
# Check logs at: $env:ProgramData\PhoneCam\phonecam-service-YYYY-MM-DD.log
```

---

## Configuration

### Command-Line Options

```bash
PhoneCamService.exe --usb
  # USB/ADB connection (requires: adb devices shows 1+ device)

PhoneCamService.exe --wifi 192.168.1.100
  # WiFi connection (requires: WiFi on phone and PC on same network)

PhoneCamService.exe --bluetooth AA:BB:CC:DD:EE:FF
  # Bluetooth connection (requires: Bluetooth pairing completed)

PhoneCamService.exe --usb --width 1920 --height 1080
  # Custom resolution (defaults: 1280x720)

PhoneCamService.exe --usb --port 5555
  # Custom port (default: 4747, only for WiFi/Bluetooth)

PhoneCamService.exe --usb --no-audio
  # Disable audio playback (default: enabled)
```

### Resolution Presets

| Name | Resolution | Bitrate | CPU | Latency |
|------|-----------|---------|-----|---------|
| **SD** | 640x480 | 2Mbps | Low | 50ms |
| **HD** (default) | 1280x720 | 4Mbps | Medium | 100ms |
| **Full HD** | 1920x1080 | 6Mbps | Medium | 150ms |
| **2K** | 2560x1440 | 10Mbps | High | 200ms |
| **4K** | 3840x2160 | 15Mbps | High | 250ms |

### Windows Service Installation

```powershell
# Create service (run as Administrator)
$serviceName = "PhoneCamService"
$binaryPath = "C:\PhoneCam\build\Release\PhoneCamService.exe"
$displayName = "PhoneCam Virtual Webcam Service"

# Create service
New-Service -Name $serviceName `
    -BinaryPathName "$binaryPath --usb" `
    -DisplayName $displayName `
    -Description "Stream Android phone as virtual webcam" `
    -StartupType Automatic `
    -ErrorAction Stop

# Start service
Start-Service -Name $serviceName

# View status
Get-Service -Name $serviceName

# Stop service
Stop-Service -Name $serviceName

# Remove service
Remove-Service -Name $serviceName -Force
```

---

## Monitoring & Troubleshooting

### Log File Location

```
%ProgramData%\PhoneCam\phonecam-service-YYYY-MM-DD.log
%ProgramData%\PhoneCam\phonecam-service-YYYY-MM-DD.log.1
%ProgramData%\PhoneCam\phonecam-service-YYYY-MM-DD.log.2
... (up to 10 files per day, 100MB max each)
```

### Log Levels

- **[ERROR]** - Critical failures requiring intervention
- **[WARN]** - Degraded functionality (missing device, audio init failed)
- **[INFO]** - Connection status, frame counts, statistics
- **[DEBUG]** - Detailed diagnostics (enable in debug builds)

### Common Issues & Solutions

#### USB Mode Issues

**Error: "No Android device detected via ADB"**
```powershell
# Solution 1: Verify ADB installation
adb version  # Should show version 30.0+

# Solution 2: Check USB connection
adb devices  # Should list device

# Solution 3: Enable USB Debugging on phone
# Settings → Developer Options → USB Debugging → Enable

# Solution 4: Try reconnecting USB cable
```

**Error: "Failed to connect via USB. Is the PhoneCam app running?"**
```powershell
# Solution 1: Verify app is running on phone
# Open PhoneCam app → Should show server status

# Solution 2: Check port forwarding
adb forward --list  # Should show tcp:4747 tcp:4747

# Solution 3: Manual port forward
adb forward tcp:4747 tcp:4747

# Solution 4: Check firewall
Get-NetFirewallProfile | Select-Object Name, Enabled
```

#### WiFi Connection Issues

**Error: "Connection timeout"**
```powershell
# Solution 1: Verify WiFi connectivity
ping 192.168.1.100  # Replace with phone IP

# Solution 2: Check firewall on phone
# Settings → Security → Firewall → Add PhoneCam to whitelist

# Solution 3: Reduce bitrate
# Command: bitrate 2000000  (2 Mbps instead of default ~4 Mbps)

# Solution 4: Check router WiFi band
# Ensure phone and PC on same WiFi band (2.4GHz or 5GHz stable)
```

#### Decoder Issues

**Error: "H.264 codec not found"**
```powershell
# Solution: Verify FFmpeg installation
$ffmpeg = Get-Command ffmpeg.exe
if (!$ffmpeg) {
    Write-Error "FFmpeg not in PATH"
}

# Check libraries
dir C:\ffmpeg\bin\avcodec*.dll
# Should see: avcodec-61.dll, avutil-59.dll, etc.
```

**Error: "Invalid H.264 stream (corrupted data?)"**
```powershell
# Solution: Request new keyframe from phone
# Type in service console: keyframe

# Or change settings to force I-frame
# Command: resolution 1280 720
```

#### Frame Buffer Issues

**Error: "Failed to create shared memory frame buffer"**
```powershell
# Solution 1: Clear stale handles
# Restart PC

# Solution 2: Check permissions
# Ensure user has write access to temp directory
icacls $env:TEMP

# Solution 3: Check available memory
Get-ComputerInfo | Select-Object CsPhyicallyInstalledSystemMemory

# Solution 4: Unregister/re-register driver
regsvr32 /u C:\PhoneCam\build\Release\PhoneCamDriver.dll
regsvr32    C:\PhoneCam\build\Release\PhoneCamDriver.dll
```

### Performance Diagnostics

```powershell
# Monitor service resource usage
$process = Get-Process | Where-Object {$_.ProcessName -like "*PhoneCam*"}
while ($true) {
    $mem = [math]::Round($process.WorkingSet64 / 1MB, 2)
    $cpu = [math]::Round(($process | Measure-Object -Property CPU -Sum).Sum, 2)
    Write-Host "Memory: ${mem}MB | CPU: ${cpu}%"
    Start-Sleep -Seconds 1
}

# Check last 20 log entries
Get-Content "$env:ProgramData\PhoneCam\phonecam-service-*.log" `
    -Tail 20 | Select-Object -Last 20
```

### Verification Checklist

- [ ] Service starts without errors
- [ ] Log file is created with correct timestamp
- [ ] First log entry shows "Configuration" with mode, address, port
- [ ] "Connected to phone!" appears within 5 seconds
- [ ] "Stats" log appears every 2 seconds
- [ ] Stats show FPS > 0, kbps > 0
- [ ] No "ERROR" entries in logs
- [ ] Camera appears in Zoom/Teams device list
- [ ] Video feeds with minimal latency

---

## Performance Tuning

### For High-Latency Networks (WiFi)

```powershell
# Reduce resolution
C:\PhoneCam\build\Release\PhoneCamService.exe --wifi 192.168.1.100 --width 640 --height 480

# In console after starting:
bitrate 1000000  # 1 Mbps

# Check for improvement:
# Stats: FPS should increase, frame drops should decrease
```

### For Low-Bandwidth Scenarios

```powershell
# Decrease frame rate on phone (via control command)
# Command: fps 15  (reduces from 30 to 15 fps)

# Monitor stats
# Stats: kbps should drop from ~4000 to ~2000
```

### For Low-Resource Systems

```powershell
# Disable audio
C:\PhoneCam\build\Release\PhoneCamService.exe --usb --no-audio

# Reduce resolution
# --width 800 --height 600

# Monitor memory
# Should remain < 200MB
```

---

## Uninstallation

```powershell
# 1. Stop service if running
Stop-Service -Name PhoneCamService -ErrorAction SilentlyContinue

# 2. Unregister virtual camera driver
regsvr32 /u "C:\PhoneCam\build\Release\PhoneCamDriver.dll"

# 3. Remove service
Remove-Service -Name PhoneCamService -Force -ErrorAction SilentlyContinue

# 4. Delete files (optional)
Remove-Item -Recurse -Force "C:\PhoneCam"
Remove-Item -Recurse -Force "$env:ProgramData\PhoneCam"
```

---

## References

- [PRODUCTION_HARDENING.md](PRODUCTION_HARDENING.md) - Hardening strategies
- [IMPLEMENTATION_CHANGES.md](IMPLEMENTATION_CHANGES.md) - Recent improvements
- [README.md](../README.md) - Project overview

---

**Last Updated:** May 17, 2026  
**Maintained By:** PhoneCam Development Team
