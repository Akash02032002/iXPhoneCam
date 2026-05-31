#include "Decoder.h"
#include <cstdio>
#include <cstring>

namespace phonecam {

Decoder::Decoder() {}

Decoder::~Decoder() {
    shutdown();
}

bool Decoder::initialize(int width, int height) {
    m_width = width;
    m_height = height;
    m_outWidth = width;
    m_outHeight = height;

    // Find H.264 decoder
    m_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!m_codec) {
        printf("Decoder: H.264 codec not found\n");
        return false;
    }

    // Allocate codec context
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
        printf("Decoder: Failed to allocate codec context\n");
        return false;
    }

    // Configure for decoding
    // Do NOT pre-set width/height/pix_fmt — let the parser detect from SPS
    // NOTE: Removed AV_CODEC_FLAG_LOW_DELAY — it may cause incomplete chroma decoding
    m_codecCtx->thread_count = 1;

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        printf("Decoder: Failed to open codec\n");
        return false;
    }

    // Initialize H.264 parser — critical for splitting Annex B NALUs
    m_parser = av_parser_init(AV_CODEC_ID_H264);
    if (!m_parser) {
        printf("Decoder: Failed to init H.264 parser\n");
        return false;
    }

    // Allocate frames
    m_frame = av_frame_alloc();
    m_rgbFrame = av_frame_alloc();
    m_packet = av_packet_alloc();

    if (!m_frame || !m_rgbFrame || !m_packet) {
        printf("Decoder: Failed to allocate frames\n");
        return false;
    }

    // Allocate RGB output buffer (BGR24 to match DirectShow MEDIASUBTYPE_RGB24)
    int rgbSize = av_image_get_buffer_size(AV_PIX_FMT_BGR24, width, height, 1);
    m_rgbBuffer = static_cast<uint8_t*>(av_malloc(rgbSize));
    av_image_fill_arrays(m_rgbFrame->data, m_rgbFrame->linesize,
                         m_rgbBuffer, AV_PIX_FMT_BGR24, width, height, 1);

    // Scaler will be created on first frame when we know the actual pixel format
    m_swsCtx = nullptr;
    m_lastPixFmt = AV_PIX_FMT_NONE;

    m_initialized = true;
    printf("Decoder: Initialized %dx%d H.264 -> BGR24 (with parser)\n", width, height);
    return true;
}

bool Decoder::decodeFrame(const uint8_t* encodedData, int size, int64_t timestampUs) {
    if (!m_initialized) return false;

    m_frameReady = false;

    // Use the H.264 parser to split the raw Annex B bytestream into
    // proper access units before sending to the decoder.
    const uint8_t* dataPtr = encodedData;
    int dataSize = size;
    bool gotFrame = false;

    while (dataSize > 0) {
        uint8_t* parsedData = nullptr;
        int parsedSize = 0;

        int consumed = av_parser_parse2(
            m_parser, m_codecCtx,
            &parsedData, &parsedSize,
            dataPtr, dataSize,
            timestampUs, timestampUs, 0
        );

        if (consumed < 0) {
            printf("Decoder: Parser error\n");
            break;
        }

        dataPtr += consumed;
        dataSize -= consumed;

        if (parsedSize > 0) {
            if (sendAndDecode(parsedData, parsedSize, timestampUs))
                gotFrame = true;
        }
    }

    // Flush the parser to output any remaining buffered NALU.
    {
        uint8_t* parsedData = nullptr;
        int parsedSize = 0;
        av_parser_parse2(
            m_parser, m_codecCtx,
            &parsedData, &parsedSize,
            nullptr, 0,
            timestampUs, timestampUs, 0
        );
        if (parsedSize > 0) {
            if (sendAndDecode(parsedData, parsedSize, timestampUs))
                gotFrame = true;
        }
    }

    return gotFrame;
}

bool Decoder::sendAndDecode(uint8_t* data, int size, int64_t timestampUs) {
    static int sendCount = 0;
    sendCount++;

    m_packet->data = data;
    m_packet->size = size;
    m_packet->pts = timestampUs;

    int ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        printf("Decoder: Error sending packet: %d\n", ret);
        return false;
    }

    // Drain all available frames from the decoder
    bool gotFrame = false;
    while (true) {
        ret = avcodec_receive_frame(m_codecCtx, m_frame);
        if (ret == 0) {
            gotFrame = convertFrame();
        } else {
            break;
        }
    }
    return gotFrame;
}

bool Decoder::convertFrame() {
    static int frameCount = 0;
    frameCount++;

    AVPixelFormat actualFmt = static_cast<AVPixelFormat>(m_frame->format);

    // Check if stream dimensions or pixel format changed — recreate scaler
    // Note: stream dimensions (m_width/m_height) may differ from output dimensions
    // (m_outWidth/m_outHeight). We always scale to the configured output size.
    if (m_frame->width != m_width || m_frame->height != m_height ||
        !m_swsCtx || actualFmt != m_lastPixFmt) {

        m_width = m_frame->width;
        m_height = m_frame->height;
        m_lastPixFmt = actualFmt;

        printf("Decoder: Stream %dx%d fmt=%d -> output %dx%d\n",
               m_width, m_height, actualFmt, m_outWidth, m_outHeight);

        // Recreate scaler: stream dimensions → output dimensions
        if (m_swsCtx) sws_freeContext(m_swsCtx);
        m_swsCtx = sws_getContext(
            m_width, m_height, actualFmt,
            m_outWidth, m_outHeight, AV_PIX_FMT_BGR24,
            SWS_LANCZOS, nullptr, nullptr, nullptr
        );

        if (!m_swsCtx) {
            printf("Decoder: Failed to create scaler for fmt=%d\n", actualFmt);
            return false;
        }

        // Set full-range output for better color reproduction
        int srcRange = 0; // MPEG/limited range from H.264
        int dstRange = 1; // Full range for RGB output
        const int* srcCoefs = sws_getCoefficients(SWS_CS_ITU709);
        const int* dstCoefs = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(m_swsCtx,
            srcCoefs, srcRange, dstCoefs, dstRange,
            0, 1 << 16, 1 << 16);

        // RGB buffer is always sized for the output dimensions (never changes)
    }

    // Convert YUV → BGR24 (bottom-up for DirectShow biHeight>0)
    // Output is always at m_outWidth x m_outHeight
    int stride = m_outWidth * 3;
    uint8_t* dstSlice[4] = { m_rgbBuffer + (m_outHeight - 1) * stride, nullptr, nullptr, nullptr };
    int dstStride[4] = { -stride, 0, 0, 0 };
    sws_scale(m_swsCtx,
              m_frame->data, m_frame->linesize, 0, m_frame->height,
              dstSlice, dstStride);

    m_frameReady = true;
    return true;
}

int Decoder::getDecodedFrame(uint8_t* outBuffer, int bufferSize) {
    if (!m_frameReady || !m_rgbBuffer) return 0;

    int frameSize = m_outWidth * m_outHeight * 3;
    int copySize = (bufferSize < frameSize) ? bufferSize : frameSize;
    memcpy(outBuffer, m_rgbBuffer, copySize);
    m_frameReady = false;
    return copySize;
}

void Decoder::shutdown() {
    if (m_parser) { av_parser_close(m_parser); m_parser = nullptr; }
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_rgbBuffer) { av_free(m_rgbBuffer); m_rgbBuffer = nullptr; }
    if (m_frame) { av_frame_free(&m_frame); }
    if (m_rgbFrame) { av_frame_free(&m_rgbFrame); }
    if (m_packet) { av_packet_free(&m_packet); }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); }
    m_initialized = false;
    printf("Decoder: Shutdown\n");
}

} // namespace phonecam
