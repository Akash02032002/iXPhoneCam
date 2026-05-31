# PhoneCam Production Hardening & Error Handling Guide

## Executive Summary

This document outlines critical improvements needed for production deployment, including error handling, logging, resilience, and data validation. Current implementation has basic error checking but lacks production-grade robustness.

---

## 1. Error Handling Strategy

### 1.1 Critical Failure Modes

| Component | Failure Mode | Current Handling | Required Action |
|-----------|--------------|-----------------|-----------------|
| **Decoder** | FFmpeg initialization fails | Returns false | Log error context with version info |
| **Decoder** | H.264 stream corruption | Parser error printed | Graceful recovery, keyframe requests |
| **Network** | Connection timeout | 10s timeout | Circuit breaker pattern, exponential backoff |
| **Network** | Malformed packets | Log & skip | Packet validation with checksum |
| **Shared Memory** | FrameBuffer creation fails | Returns false | Fallback to alternative IPC, cleanup |
| **Audio** | Audio subsystem fails | Continue without audio | Monitor and attempt recovery |
| **Service** | Unhandled exception | Crash | Try-catch wrapper, core dump capture |

### 1.2 Logging Infrastructure

#### Three-Tier Logging
```
TIER 1: Console (stdout/stderr)
  - Critical errors only
  - Connection status changes
  - Statistics

TIER 2: File Logging (phonecam-service.log)
  - All events with timestamp
  - Rotation: Daily + 10 files max (100MB)
  - Performance metrics

TIER 3: Event Tracing (Windows ETW / syslog)
  - Structured diagnostics
  - Integration with monitoring tools
```

#### Log Levels
- **ERROR**: Critical failures that require intervention
- **WARN**: Degraded functionality (audio disabled, auto-reconnect)
- **INFO**: Connection status, frame counts, statistics
- **DEBUG**: Packet-level details, memory allocations
- **TRACE**: FFmpeg codec internals (expensive, only in debug builds)

### 1.3 Exception Safety

#### Current Issues
```cpp
// ❌ PROBLEMATIC: Resource leaks on exception
auto* usb = new UsbClient();
if (!usb->connect(...)) {
    printf("ERROR: ...\n");
    delete usb;  // Only freed on error, not on success
}
transport = usb;
```

#### Solution: RAII & Smart Pointers
```cpp
// ✅ CORRECT: Automatic cleanup
std::unique_ptr<TransportClient> transport;
auto usb = std::make_unique<UsbClient>();
if (!usb->connect(...)) {
    log.error("Connection failed", error_context);
    return 1;
}
transport = std::move(usb);  // Ownership transfer
```

---

## 2. Network Resilience

### 2.1 Connection Retry Strategy

```cpp
const int MAX_RETRIES = 10;
const int INITIAL_BACKOFF_MS = 500;
const int MAX_BACKOFF_MS = 30000;
const int JITTER_FACTOR = 0.1;

for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    if (transport->connect(address, port)) {
        log.info("Connected", {"attempt": attempt + 1});
        backoff_ms = INITIAL_BACKOFF_MS;  // Reset on success
        break;
    }
    
    if (attempt < MAX_RETRIES - 1) {
        int delay = calcBackoff(attempt);
        log.warn("Reconnection attempt failed", 
                 {"attempt": attempt + 1, "delay_ms": delay});
        Sleep(delay);
    } else {
        log.error("Max retries exhausted");
        return 1;
    }
}
```

### 2.2 Socket-Level Hardening

- **Keep-alive**: TCP_KEEPALIVE every 5 seconds
- **Send timeout**: 5 seconds (detect stalled sends)
- **Receive timeout**: 15 seconds (detect stalled streams)
- **Buffer limits**: Discard packets >10MB
- **Magic byte verification**: Resync on corruption

### 2.3 Connection State Machine

```
[Disconnected] 
    ↓ connect()
[Connecting] (timeout: 10s)
    ↓ success / failure
[Connected] ↔ [Reading Packets]
    ↓ recv() timeout or error
[Reconnecting] (exponential backoff)
    ↓ connect()
[Connected] or [Disconnected] (max retries)
```

---

## 3. Data Validation & Security

### 3.1 Packet Validation

```cpp
struct ValidationResult {
    bool valid;
    std::string error_message;
    ErrorCode error_code;
};

ValidationResult validatePacket(const PacketHeader* hdr, 
                                const uint8_t* payload,
                                size_t payload_size) {
    // Check magic bytes
    if (hdr->magic1 != MAGIC_1 || hdr->magic2 != MAGIC_2) {
        return {false, "Invalid magic bytes", ERR_MAGIC};
    }
    
    // Check type
    uint8_t type = hdr->type;
    if (type < TYPE_VIDEO || type > TYPE_HEARTBEAT) {
        return {false, "Unknown packet type", ERR_TYPE};
    }
    
    // Check payload size matches header
    if (hdr->payloadLength != payload_size) {
        return {false, "Payload size mismatch", ERR_SIZE};
    }
    
    // Size limit per type
    if (type == TYPE_VIDEO && payload_size > 5*1024*1024) {
        return {false, "Video frame too large", ERR_SIZE};
    }
    
    // Timestamp sanity check
    if (hdr->timestampUs < 0) {
        return {false, "Invalid timestamp", ERR_TIMESTAMP};
    }
    
    return {true, "", ERR_NONE};
}
```

### 3.2 Bounds Checking

- **All array accesses**: Validate index < capacity before access
- **Buffer operations**: Check size before memcpy, memmove
- **String operations**: Use safe variants (strncpy, strncat)
- **FFmpeg callbacks**: Validate frame dimensions before access

### 3.3 Integer Overflow Prevention

```cpp
// ❌ UNSAFE: Can overflow
int frameSize = header.width * header.height * 3;

// ✅ SAFE: Check before multiplication
if (header.width > 0 && header.width <= 7680 &&
    header.height > 0 && header.height <= 4320) {
    // Max 8K: 7680 * 4320 * 3 = ~99MB (safe)
    int frameSize = header.width * header.height * 3;
} else {
    log.error("Invalid resolution", {"w": header.width, "h": header.height});
}
```

---

## 4. Memory Management

### 4.1 Resource Cleanup on Error

```cpp
// ✅ GOOD: Automatic cleanup via RAII
{
    FrameBuffer frameBuffer;
    if (!frameBuffer.createWriter(width, height)) {
        log.error("Frame buffer creation failed");
        return 1;  // Destructor auto-cleans
    }
    // ... use frameBuffer
}  // Automatic cleanup here
```

### 4.2 Memory Leak Detection

- Build with `/RTC1` (runtime checks) in Debug builds
- Use Dr. Memory or Address Sanitizer in CI
- Profile with Windows Task Manager weekly memory growth
- Alert if memory usage exceeds 500MB for >5 minutes

### 4.3 Shared Memory Cleanup

```cpp
// Ensure OS cleanup on service crash
FrameBuffer::~FrameBuffer() {
    if (m_hMapFile) {
        FlushViewOfFile(m_pBuffer, m_totalSize);
        UnmapViewOfFile(m_pBuffer);
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        log.debug("Shared memory cleaned up");
    }
}
```

---

## 5. Performance & Latency Monitoring

### 5.1 Key Metrics

| Metric | Target | Warn Threshold | Alert Threshold |
|--------|--------|-----------------|-----------------|
| **Frame Latency** | <100ms | 200ms | 500ms |
| **CPU Usage** | <15% | 50% | 80% |
| **Memory** | <100MB | 300MB | 500MB |
| **Frame Drops** | 0% | >5% | >20% |
| **Reconnect Time** | <2s | 5s | 10s |

### 5.2 Metrics Collection

```cpp
struct PerformanceMetrics {
    uint64_t frame_count = 0;
    uint64_t dropped_frames = 0;
    uint64_t bytes_received = 0;
    uint64_t reconnect_count = 0;
    std::chrono::milliseconds avg_frame_latency{0};
    std::chrono::milliseconds max_frame_latency{0};
};
```

### 5.3 Telemetry Reporting

- Local file: JSON format, updated every 10 seconds
- Network: Optional POST to telemetry endpoint (anonymized)
- Retention: 30 days rolling window

---

## 6. Thread Safety & Synchronization

### 6.1 Current Issues

```cpp
// ❌ UNSAFE: Race condition
static std::atomic<bool> g_running(true);  // OK - atomic
std::thread statsThread([&]() {
    while (g_running) {  // g_running is atomic ✓
        uint64_t frames = framesDecoded.load();  // ✓
        // But frameBuffer may be deleted by main thread ✗
    }
});
```

### 6.2 Safe Shutdown Protocol

```cpp
// ✅ CORRECT: Proper cleanup sequence
std::atomic<bool> g_shutdown(false);
std::vector<std::thread> threads;

void shutdown() {
    g_shutdown = true;
    
    // Step 1: Signal all threads to stop
    for (auto& t : threads) {
        if (t.joinable()) t.join();  // Wait for completion
    }
    
    // Step 2: Close resources in reverse order
    transport->disconnect();
    decoder.shutdown();
    frameBuffer.close();
    
    log.info("Shutdown complete");
}
```

### 6.3 Synchronization Points

- **Decoder state**: Guard with mutex for reinitialization
- **FrameBuffer writes**: Use WaitForSingleObject for signaling
- **Stats collection**: Use atomic for counters
- **Audio playback queue**: Thread-safe queue (boost::lockfree or custom)

---

## 7. FFmpeg Specific Hardening

### 7.1 Codec Context Error Handling

```cpp
// ✅ IMPROVED: Comprehensive error handling
int ret = avcodec_open2(m_codecCtx, m_codec, nullptr);
if (ret < 0) {
    char errBuf[256] = {0};
    av_strerror(ret, errBuf, sizeof(errBuf) - 1);
    log.error("Codec initialization failed", 
              {"codec": m_codec->name, "error": errBuf, "code": ret});
    
    // Cleanup
    avcodec_free_context(&m_codecCtx);
    return false;
}
```

### 7.2 Stream Corruption Recovery

```cpp
bool Decoder::handleStreamError() {
    log.warn("Decoder recovery triggered", 
             {"decoded_frames": m_frameCount});
    
    // Request I-frame from phone
    transport->sendCommand(CMD_REQUEST_KEYFRAME);
    
    // Flush buffered data
    avcodec_flush_buffers(m_codecCtx);
    
    // Reset state
    m_errorCount++;
    if (m_errorCount > MAX_ERRORS) {
        log.error("Too many errors, requesting reconnect");
        return false;  // Signal reconnect needed
    }
    
    return true;
}
```

### 7.3 Pixel Format Validation

```cpp
// ✅ SAFE: Validate before format conversion
if (m_frame->format != AV_PIX_FMT_YUV420P &&
    m_frame->format != AV_PIX_FMT_NV12) {
    log.error("Unexpected pixel format", 
              {"format": m_frame->format});
    return false;
}
```

---

## 8. Operational Runbook

### 8.1 Service Startup

```powershell
# 1. Pre-flight checks
if (!(Test-Path "C:\ffmpeg\bin\*.dll")) {
    Write-Error "FFmpeg not found at C:\ffmpeg"
    exit 1
}

# 2. Start service with logging
$logFile = "C:\ProgramData\PhoneCam\service-$(Get-Date -f 'yyyy-MM-dd').log"
.\PhoneCamService.exe --usb 2>&1 | Tee-Object -FilePath $logFile

# 3. Monitor output
Get-Content $logFile -Wait -Tail 20
```

### 8.2 Troubleshooting Checklist

| Issue | Diagnosis | Resolution |
|-------|-----------|-----------|
| **Connection fails** | Check device connection | `adb devices`, verify USB debugging enabled |
| **High latency** | Check bitrate | `bitrate 5000000` (5 Mbps) |
| **Decoder errors** | Check H.264 stream | Request keyframe: `keyframe` |
| **Memory leak** | Check for hung threads | Monitor for 10min, check if stable |
| **No audio** | Check audio subsystem | `audio-on` / `audio-off` commands |

### 8.3 Monitoring & Alerting

```plaintext
Alert Conditions:
1. Service crashes → Restart with exponential backoff
2. Memory > 500MB for 5min → Restart service
3. FPS < 10 for 30s → Log warning, attempt keyframe request
4. Reconnect interval > 5s → Log error, check network
5. Decoder errors > 100/min → Restart connection
```

---

## 9. Implementation Checklist

- [ ] Add Logger class with file rotation
- [ ] Implement exponential backoff for reconnection
- [ ] Add packet validation with error codes
- [ ] Replace raw pointers with smart pointers
- [ ] Add metrics collection and reporting
- [ ] Implement thread-safe shutdown
- [ ] Add comprehensive error logging to FFmpeg operations
- [ ] Implement socket option hardening (keep-alive, timeouts)
- [ ] Add stack trace capture on crash
- [ ] Create Windows Event Log integration
- [ ] Add health check endpoint (local port 9090)
- [ ] Document all error codes and recovery procedures

---

## 10. Testing Strategy

### 10.1 Unit Tests
```cpp
TEST(DecoderTest, InvalidStreamErrorRecovery) {
    Decoder decoder;
    decoder.initialize(1280, 720);
    
    // Send corrupted stream
    uint8_t badData[100] = {0xFF, 0xFF};
    EXPECT_FALSE(decoder.decodeFrame(badData, 2, 0));
    
    // Send valid frame after error
    EXPECT_TRUE(decoder.decodeFrame(validKeyFrame, size, 0));
}
```

### 10.2 Integration Tests
- USB connection/disconnection cycles
- Bitrate/resolution changes
- Long-running stability (4+ hours)
- Network outage simulation

### 10.3 Stress Tests
- High-speed resolution changes
- Rapid reconnection attempts
- Malformed packet injection
- Memory pressure scenarios

---

## 11. References

- [RFC 5117: RTP Payload Format for H.264](https://tools.ietf.org/html/rfc5117)
- [FFmpeg Error Handling Guide](https://ffmpeg.org/doxygen/trunk/group__lavc__core.html)
- [Windows Socket Error Codes](https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2)
- [Circuit Breaker Pattern](https://martinfowler.com/bliki/CircuitBreaker.html)

---

**Last Updated:** May 17, 2026  
**Status:** Production Ready Guide (Implementation In Progress)
