#pragma once
#ifndef PHONECAM_DECODER_H
#define PHONECAM_DECODER_H

#include <cstdint>
#include <functional>

// Forward declarations for FFmpeg (C linkage)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace phonecam {

/**
 * H.264 video decoder using FFmpeg.
 * Decodes H.264 NAL units from the phone into RGB24 frames
 * that are written to the shared FrameBuffer.
 */
class Decoder {
public:
    Decoder();
    ~Decoder();

    /**
     * Initialize the decoder for H.264.
     * @param width Expected width (used for scaler setup)
     * @param height Expected height
     * @return true on success
     */
    bool initialize(int width, int height);

    /**
     * Decode an H.264 encoded frame.
     * @param encodedData Raw H.264 NAL unit data
     * @param size Size of encoded data
     * @param timestampUs Presentation timestamp
     * @return true if a decoded frame is available (call getDecodedFrame)
     */
    bool decodeFrame(const uint8_t* encodedData, int size, int64_t timestampUs);

    /**
     * Get the last decoded frame as RGB24.
     * @param outBuffer Output buffer (must be width * height * 3 bytes)
     * @param bufferSize Size of output buffer
     * @return Number of bytes written, or 0 if no frame available
     */
    int getDecodedFrame(uint8_t* outBuffer, int bufferSize);

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

    void shutdown();

private:
    bool convertFrame();
    bool sendAndDecode(uint8_t* data, int size, int64_t timestampUs);

    const AVCodec* m_codec = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVCodecParserContext* m_parser = nullptr;
    AVFrame* m_frame = nullptr;     // decoded YUV frame
    AVFrame* m_rgbFrame = nullptr;  // converted RGB frame
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsCtx = nullptr;
    uint8_t* m_rgbBuffer = nullptr;
    int m_width = 0;        // actual stream dimensions (from decoder)
    int m_height = 0;
    int m_outWidth = 0;     // configured output dimensions (constant)
    int m_outHeight = 0;
    AVPixelFormat m_lastPixFmt = AV_PIX_FMT_NONE;
    bool m_initialized = false;
    bool m_frameReady = false;
};

} // namespace phonecam

#endif // PHONECAM_DECODER_H
