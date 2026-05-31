# Audio Streaming Lessons

This note records the working solution for the PhoneCam mobile-mic-to-PC-speaker
stream after repeated failures with the newer audio path.

## Failure Symptoms

- Phone mic audio appeared to arrive briefly, then stopped.
- `audio_packets` froze after a very small number such as `7` or `13`.
- Video frames could also stall at the same time.
- Direct PC speaker tests still worked, proving Realtek playback itself was not
  broken.
- VB-Audio / CABLE routing added confusion and should not be used for the
  speaker-monitor path.

## Root Cause Pattern

The fragile path combined too many moving parts:

- Android sent tiny fixed 20 ms audio packets.
- PC routed audio through multi-output playback, sometimes including VB-Audio.
- PC used an extra audio worker queue between `PacketReader` and `AudioPlayer`.
- PC `waveOut` cleanup relied on callback-driven buffer completion.
- UI mute/unmute controls could stop either phone capture or PC playback.

That combination could make logs look alive while playback stalled.

## Working Reference

The previous working project `iXCam_Webcam` used a simpler path:

```text
Android built-in mic -> AudioRecord MIC -> TCP -> single PC AudioPlayer -> waveOut -> Realtek speakers
```

The important behaviors to preserve are:

- Android `AudioCapture` uses `MediaRecorder.AudioSource.MIC`.
- Android reads using the native `AudioRecord.getMinBufferSize(...) * 2`
  buffer size instead of tiny 20 ms chunks.
- PC uses one `AudioPlayer`.
- PC sends audio directly to `waveOut`.
- PC output is forced to `Speakers (Realtek(R) Audio)` when testing speaker
  playback.
- VB-Audio / CABLE is not part of the Realtek speaker-monitor solution.
- Mute/unmute audio UI and `audio-off` behavior should not stop the stream.

## Current Fix

PhoneCam now follows the iXCam-style path with one extra stability improvement:
`AudioPlayer` opens Realtek through `waveOut` with `CALLBACK_NULL` and reclaims
completed buffers by polling `WHDR_DONE` before submitting new buffers. This
avoids callback/lock interactions that can stall the packet reader.

Expected healthy logs:

```text
AudioPlayer: Routing phone mic audio to output device 'Speakers (Realtek(R) Audio)'
AudioPlayer: Initialized (44100 Hz, 16-bit, Mono)
Stats | audio_packets=19 ...
Stats | audio_packets=44 ...
Stats | audio_packets=68 ...
Stats | audio_packets=94 ...
```

If this problem returns, first compare against `iXCam_Webcam` and keep the path
simple before adding bridges, queues, gain stages, or UI audio toggles.
