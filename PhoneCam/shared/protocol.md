# PhoneCam Wire Protocol Specification v1.0

## Overview

PhoneCam uses a custom binary protocol for streaming video/audio from an Android phone to a Windows PC. The protocol operates over TCP (USB/WiFi) or Bluetooth RFCOMM.

## Packet Format

All packets follow the same structure:

```
Offset  Size    Field           Description
------  ----    -----           -----------
0       2       Magic           Always 0xCA 0xFE
2       1       Type            Packet type (see below)
3       1       Flags           Packet flags (see below)
4       4       PayloadLength   Payload size in bytes (big-endian uint32)
8       8       Timestamp       Presentation time in microseconds (big-endian int64)
16      N       Payload         Variable-length payload data
```

**Total header size: 16 bytes**

## Packet Types

| Value | Name      | Direction    | Description |
|-------|-----------|--------------|-------------|
| 0x01  | VIDEO     | Phone → PC   | H.264 encoded video frame (NAL unit) |
| 0x02  | AUDIO     | Phone → PC   | Raw PCM audio data (16-bit, 44100Hz, mono) |
| 0x03  | CONTROL   | PC → Phone   | Camera control command |
| 0x04  | HEARTBEAT | Bidirectional| Keep-alive (no payload) |

## Flags

| Bit  | Name      | Applies to | Description |
|------|-----------|------------|-------------|
| 0x01 | KEYFRAME  | VIDEO      | This is an I-frame (key frame) |
| 0x02 | CONFIG    | VIDEO      | SPS/PPS configuration data |

## Control Commands

Control packets carry a single-byte command in the payload, optionally followed by parameters:

| Command | Value | Parameters | Description |
|---------|-------|------------|-------------|
| SWITCH_CAMERA    | 0x01 | None | Toggle front/back camera |
| TOGGLE_FLASH     | 0x02 | None | Toggle flashlight |
| SET_RESOLUTION   | 0x03 | width(2B) + height(2B) | Change resolution |
| SET_BITRATE      | 0x04 | bitrate(4B) | Change encoding bitrate |
| REQUEST_KEYFRAME | 0x05 | None | Force an I-frame |
| AUTOFOCUS        | 0x06 | None | Trigger auto-focus |
| SET_ZOOM         | 0x07 | zoom ratio x100(2B) | Change zoom ratio |
| ROTATE_CAMERA    | 0x08 | None | Rotate video clockwise by 90 degrees |
| AUDIO_ON         | 0x09 | None | Start phone microphone streaming |
| AUDIO_OFF        | 0x0A | None | Stop phone microphone streaming |
| DISCONNECT       | 0x0F | None | Graceful disconnect |

## Byte Order

All multi-byte fields use **big-endian** (network byte order).

## Connection Flow

1. Phone starts TCP server on port 4747
2. PC connects to phone
3. Phone immediately begins sending VIDEO packets
4. PC can send CONTROL packets at any time
5. Both sides exchange HEARTBEAT every 2 seconds
6. Either side can close the connection
