# PhoneCam Error Reference & Recovery Guide

## Overview

This guide provides detailed information about error codes, causes, and recovery procedures for PhoneCam deployments.

---

## Error Code Categories

### 1000-1099: Connection Errors

| Code | Name | Cause | Recovery |
|------|------|-------|----------|
| **1001** | `ERR_CONNECTION_TIMEOUT` | Socket recv timeout (10+ sec with no data) | Increase timeout or check network latency |
| **1002** | `ERR_CONNECTION_REFUSED` | Phone app not running or firewall blocking | Restart PhoneCam app on phone; check firewall |
| **1003** | `ERR_CONNECTION_RESET` | Unexpected connection close from peer | Phone crashed or network unstable; reconnect |
| **1004** | `ERR_SOCKET_CREATION_FAILED` | Cannot create socket (OS resource exhaustion) | Restart service; check Windows socket limits |
| **1005** | `ERR_ADB_NOT_FOUND` | adb.exe not in PATH | Install Android SDK Platform Tools |
| **1006** | `ERR_ADB_NO_DEVICE` | No Android device connected via ADB | Connect phone via USB; enable USB Debugging |
| **1007** | `ERR_BLUETOOTH_PAIRING_FAILED` | Bluetooth device not paired or found | Pair device in Windows Bluetooth settings |

**Recovery Strategy:**
```
1. Log error code from service output
2. Check network connectivity (ping phone IP)
3. Verify app is running on phone
4. Force service restart
5. If persistent, reboot both devices
```

### 2000-2099: Packet/Protocol Errors

| Code | Name | Cause | Recovery |
|------|------|-------|----------|
| **2001** | `ERR_PACKET_MAGIC_INVALID` | Received 0x?? 0x?? instead of 0xCA 0xFE | Stream corruption; request keyframe |
| **2002** | `ERR_PACKET_TYPE_INVALID` | Unknown packet type in header | Incompatible protocol version; update app |
| **2003** | `ERR_PACKET_SIZE_INVALID` | Payload size > 10MB or negative | Malformed packet; typically auto-recovers |
| **2004** | `ERR_PACKET_CHECKSUM` | CRC/checksum validation failed | Stream corruption; request keyframe |
| **2005** | `ERR_PACKET_TIMEOUT` | No data received for 10+ seconds | Network stalled; will trigger auto-reconnect |

**Recovery Strategy:**
```
1. Service logs packet errors but usually recovers
2. If error rate > 100 errors/min:
   - Send: keyframe (command)
   - Or: resolution 1280 720 (forces new keyframe)
3. If still failing, reconnect
```

**Example Log:**
```
[ERROR] [PacketReader] Invalid magic bytes (stream corruption?)
[WARN] Decoder error #1 (requesting keyframe)
[INFO] Command sent: request keyframe
```

### 3000-3099: Decoder Errors

| Code | Name | Cause | Recovery |
|------|------|-------|----------|
| **3001** | `ERR_DECODER_INIT_FAILED` | FFmpeg codec context allocation failed | Check FFmpeg version (6.0+ required) |
| **3002** | `ERR_DECODER_CODEC_NOT_FOUND` | H.264 codec not available in FFmpeg build | Rebuild with libx264 support |
| **3003** | `ERR_DECODER_INVALID_STREAM` | H.264 parser error or corrupted NAL units | Resync stream by requesting keyframe |
| **3004** | `ERR_DECODER_FRAME_DROP` | Frame discarded due to parsing error | Minor issue; service auto-recovers |
| **3005** | `ERR_DECODER_PIXEL_FORMAT` | Unexpected pixel format (not YUV420P/NV12) | Phone sending incompatible format |

**Recovery Strategy:**

**For ERR_DECODER_INIT_FAILED:**
```powershell
# Verify FFmpeg installation
ffmpeg -codecs | findstr h264
# Output should show: D.V.LS h264

# Check library versions
dir C:\ffmpeg\bin\avcodec*.dll
# Expected: avcodec-61.dll (or similar)
```

**For ERR_DECODER_CODEC_NOT_FOUND:**
```powershell
# Rebuild FFmpeg or replace with release build that includes H.264
# Verify: ffmpeg -decoders | findstr h264
```

**For ERR_DECODER_INVALID_STREAM:**
```
1. Command in service: keyframe
2. If errors continue: resolution 1920 1080 (forces reconfiguration)
3. If still failing: Stop and restart service
```

**Example Log Anti-pattern (Bad):**
```
[ERROR] [Decoder] Error sending packet: -12
[ERROR] [Decoder] Error sending packet: -12
... repeated 500+ times ...
```

**Expected Log (Good):**
```
[WARN] Decoder error #1 (requesting keyframe)
[INFO] Stats | fps=24 kbps=4000 frames=1200 decoder_errors=1
[INFO] Stats | fps=24 kbps=4000 frames=1244 decoder_errors=1  # Stable
```

### 4000-4099: Shared Memory Errors

| Code | Name | Cause | Recovery |
|------|------|-------|----------|
| **4001** | `ERR_FRAMEBUFFER_CREATE_FAILED` | Cannot create Windows shared memory section | Shared memory resource exhaustion |
| **4002** | `ERR_FRAMEBUFFER_MAP_FAILED` | Cannot map virtual address to shared memory | OS virtual memory issue |
| **4003** | `ERR_FRAMEBUFFER_PERMISSION` | User lacks permissions for temp/shared mem | Run as admin or adjust permissions |

**Recovery Strategy:**

```powershell
# 1. Check available virtual memory
Get-WmiObject Win32_LogicalMemoryConfiguration | 
    Select-Object Name, TotalVirtualMemory, AvailableVirtualMemory | 
    Format-Table

# 2. Check temp directory permissions
icacls $env:TEMP

# 3. Restart service
Restart-Service -Name PhoneCamService

# 4. Last resort: Restart PC
Restart-Computer

# 5. Verify DirectShow filter is installed
regquery HKCR .{{E3F2C5A0-1234-4B8E-9A0F-ABCDEF123456}}
```

### 5000-5099: Audio Errors

| Code | Name | Cause | Recovery |
|------|------|-------|----------|
| **5001** | `ERR_AUDIO_INIT_FAILED` | Cannot initialize audio subsystem (WASAPI) | Audio device disconnected or driver issue |
| **5002** | `ERR_AUDIO_DEVICE_NOT_FOUND` | No default audio output device | Connect speakers/headphones; set default device |

**Recovery Strategy:**

```
1. Service automatically continues WITHOUT audio
2. To re-enable audio:
   - Command: audio-on (in service console)
   - Check: Control Panel → Sound → Default Playback Device
3. For persistent issues:
   - Update audio driver
   - Disable audio: --no-audio flag
```

**Example Log:**
```
[WARN] Failed to initialize audio (AudioPlayer)
[INFO] Continuing without audio
```

### 6000-6099: System Errors

| Code | Name | Cause | Recovery |
|------|------|-------|----------|
| **6001** | `ERR_OUT_OF_MEMORY` | Heap allocation failed (< 50MB free) | Reduce resolution or restart service |
| **6002** | `ERR_THREAD_CREATION_FAILED` | Cannot spawn stats/stdin thread | OS thread limit reached |
| **6003** | `ERR_INVALID_ARGUMENT` | Invalid CLI argument or frame size overflow | Check command line syntax |

**Recovery Strategy:**

```powershell
# For OUT_OF_MEMORY:
Get-Process | Where-Object {$_.Name -eq "PhoneCamService"} | Select-Object WorkingSet
# If > 500MB, restart service

# For low memory:
Get-ComputerInfo CsPhysicallyInstalledSystemMemory
# If < 2GB total, upgrade RAM or reduce resolution

# For THREAD_CREATION_FAILED:
# Restart-Computer (OS likely under resource pressure)
```

---

## Troubleshooting Decision Tree

```
START
  ↓
Is PhoneCamService running?
  ├─ NO → Start service: net start PhoneCamService
  │        or: C:\PhoneCam\build\Release\PhoneCamService.exe --usb
  │
  └─ YES → Check log file: %ProgramData%\PhoneCam\phonecam-service-*.log
           ↓
           Any ERROR or FATAL entries?
           ├─ NO ERROR → Service healthy ✓
           │   ├─ Can you see camera in Zoom?
           │   │  ├─ YES → Working perfectly ✓
           │   │  └─ NO → Increase log level, debug DirectShow
           │   │
           │   └─ Check fps in logs
           │       ├─ fps > 20 → Performance OK
           │       ├─ fps = 0 → Check network connectivity
           │       └─ fps < 10 → Reduce resolution or bitrate
           │
           └─ ERROR FOUND → Check error code
               ├─ 10xx (Connection)
               │   ├─ Is phone connected?
               │   │  ├─ USB: adb devices
               │   │  ├─ WiFi: ping <phone-ip>
               │   │  └─ BT: Settings → Bluetooth
               │   └─ Is PhoneCam app running on phone?
               │
               ├─ 20xx (Packet)
               │   ├─ Check network stability
               │   ├─ Try: bitrate 2000000 (lower bitrate)
               │   └─ Reconnect WiFi if using wireless
               │
               ├─ 30xx (Decoder)
               │   ├─ Verify FFmpeg: ffmpeg -codecs | findstr h264
               │   ├─ Try: keyframe (command in console)
               │   └─ Restart service if persistent
               │
               ├─ 40xx (Shared Memory)
               │   ├─ Restart: Restart-Service PhoneCamService
               │   └─ If fails: Restart-Computer
               │
               ├─ 50xx (Audio)
               │   └─ Disable audio: --no-audio flag
               │
               └─ 60xx (System)
                   ├─ OUT_OF_MEMORY: Reduce resolution
                   ├─ THREAD_FAILED: Restart PC
                   └─ INVALID_ARG: Check command line syntax
```

---

## Log Analysis Examples

### Healthy Service Log

```
[INFO] PhoneCam Service starting
[INFO] Version: 1.0.0
[INFO] Configuration | mode=usb address=127.0.0.1 port=4747 resolution=1280x720 audio=enabled
[INFO] Shared memory frame buffer created | size=2764800
[INFO] H.264 decoder initialized
[INFO] Setting up USB/ADB connection...
[INFO] Connected to phone!
[INFO] Audio playback initialized
[INFO] Receiving video stream... (Ctrl+C to stop)

[INFO] Stats | fps=30 kbps=4100 frames=60 audio_packets=120 decoder_errors=0
[INFO] Stats | fps=30 kbps=4090 frames=120 audio_packets=240 decoder_errors=0
[INFO] Stats | fps=30 kbps=4110 frames=180 audio_packets=360 decoder_errors=0
```

### Service Log With Recoverable Errors

```
[INFO] Connected to phone!
[WARN] Decoder error #1 (requesting keyframe)      ← Recoverable
[INFO] Stats | fps=25 kbps=3900 frames=50 decoder_errors=1
[INFO] Stats | fps=30 kbps=4100 frames=100 decoder_errors=1  ← Back to normal
```

### Service Log With Connection Issues

```
[ERROR] Connection refused (is app running on phone?)
[WARN] Reconnecting | attempt=1 delay_ms=500
[WARN] Reconnection attempt failed, will retry
[WARN] Reconnecting | attempt=2 delay_ms=1000
[WARN] Reconnection attempt failed, will retry
[WARN] Reconnecting | attempt=3 delay_ms=2000
[INFO] Reconnected successfully
```

### Service Log With Critical Errors (Non-Recoverable)

```
[ERROR] Shared memory frame buffer creation failed
[ERROR] Recovery: Check Windows permissions, try restarting service, or reinstall DirectShow driver
```

---

## Remote Diagnostics

### Collect Debug Information

```powershell
# Create diagnostic bundle
$diagDir = "C:\PhoneCam-Diagnostics"
New-Item -ItemType Directory -Force -Path $diagDir | Out-Null

# 1. Collect logs (last 100 lines)
Get-Content "$env:ProgramData\PhoneCam\phonecam-service-*.log" `
    -Tail 100 | Out-File "$diagDir\service-logs.txt"

# 2. System info
Get-ComputerInfo | Out-File "$diagDir\system-info.txt"

# 3. Network config
ipconfig /all | Out-File "$diagDir\network.txt"

# 4. Audio devices
Get-CimInstance Win32_SoundDevice | Out-File "$diagDir\audio-devices.txt"

# 5. Camera devices
Get-CimInstance Win32_PnPEntity | Where-Object {$_.Caption -like "*camera*"} `
    | Out-File "$diagDir\camera-devices.txt"

# 6. FFmpeg version
ffmpeg -version | Out-File "$diagDir\ffmpeg-version.txt" 2>&1

# 7. ADB status
adb devices | Out-File "$diagDir\adb-devices.txt" 2>&1

# 8. DirectShow filter check
regquery HKCR | findstr PhoneCam | Out-File "$diagDir\directshow-registration.txt" 2>&1

# Zip for sharing
Compress-Archive -Path $diagDir -DestinationPath "$diagDir.zip"

Write-Host "Diagnostic bundle: $diagDir.zip"
```

### Key Logs to Provide When Reporting Issues

1. **Last 50 lines of service log** - Shows recent errors
2. **System information** - Windows version, FFmpeg version
3. **Command used to start service** - Mode, resolution, audio setting
4. **Error code** - From log file (e.g., 1002, 3003, etc.)
5. **Application version** - PhoneCam app version on phone

---

## Performance Benchmarks

### Expected Latency by Connection

| Mode | Latency | Jitter | Reliability |
|------|---------|--------|-------------|
| **USB (ADB)** | 80-100ms | ±5ms | 99.9% |
| **WiFi (5GHz)** | 100-150ms | ±20ms | 98% |
| **WiFi (2.4GHz)** | 150-250ms | ±50ms | 95% |
| **Bluetooth** | 300-500ms | ±100ms | 85% |

### Expected Resource Usage

| Metric | Value | Alert Threshold |
|--------|-------|-----------------|
| CPU Usage | 10-20% | >80% |
| Memory Usage | 80-120MB | >300MB |
| Frame Drops | 0% | >5% |
| Dropped Packets | 0% | >10% |

---

## Prevention Checklist

- [ ] Update Windows and drivers monthly
- [ ] Update PhoneCam app when new version released
- [ ] Monitor logs weekly for ERROR entries
- [ ] Test disaster recovery (pull USB, WiFi down, etc.)
- [ ] Keep FFmpeg updated to latest stable release
- [ ] Document any environment-specific configurations

---

## References

- [PRODUCTION_HARDENING.md](PRODUCTION_HARDENING.md) - Hardening strategies
- [IMPLEMENTATION_CHANGES.md](IMPLEMENTATION_CHANGES.md) - Code changes
- [PRODUCTION_SETUP.md](PRODUCTION_SETUP.md) - Deployment steps
- Error codes in [service/Error.h](../pc-client-windows/service/Error.h)

---

**Last Updated:** May 17, 2026  
**Revision:** 1.0  
**Status:** Production Ready
