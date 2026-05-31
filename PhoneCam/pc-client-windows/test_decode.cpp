/**
 * Standalone test: decode dump.h264 using FFmpeg APIs to verify decoder behavior.
 * Compile: cl /EHsc /I C:\ffmpeg\include test_decode.cpp /link /LIBPATH:C:\ffmpeg\lib avcodec.lib avformat.lib avutil.lib swscale.lib
 */
#include <cstdio>
#include <cstdint>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

void saveBMP(const char* filename, uint8_t* bgrData, int w, int h) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    int imgSize = w * h * 3;
    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    int fileSize = 54 + imgSize;
    memcpy(&header[2], &fileSize, 4);
    int off = 54; memcpy(&header[10], &off, 4);
    int hs = 40; memcpy(&header[14], &hs, 4);
    memcpy(&header[18], &w, 4);
    memcpy(&header[22], &h, 4);
    short planes = 1; memcpy(&header[26], &planes, 2);
    short bpp = 24; memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &imgSize, 4);
    fwrite(header, 1, 54, f);
    int rowBytes = w * 3;
    for (int row = h - 1; row >= 0; row--)
        fwrite(bgrData + row * rowBytes, 1, rowBytes, f);
    fclose(f);
    printf("Saved %s\n", filename);
}

int main() {
    const char* inputFile = "C:\\Users\\tittu\\Videos\\webcam\\PhoneCam\\dump.h264";

    // Method 1: Use avformat to demux + decode (like ffmpeg CLI)
    printf("=== Method 1: avformat demux ===\n");
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, inputFile, nullptr, nullptr) < 0) {
        printf("Failed to open input\n");
        return 1;
    }
    avformat_find_stream_info(fmtCtx, nullptr);

    int vidIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vidIdx = i;
            break;
        }
    }
    printf("Video stream index: %d\n", vidIdx);

    const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[vidIdx]->codecpar->codec_id);
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, fmtCtx->streams[vidIdx]->codecpar);
    avcodec_open2(codecCtx, codec, nullptr);

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    SwsContext* sws = nullptr;
    uint8_t* rgbBuf = nullptr;
    int frameNum = 0;

    while (av_read_frame(fmtCtx, pkt) >= 0 && frameNum < 5) {
        if (pkt->stream_index != vidIdx) { av_packet_unref(pkt); continue; }

        printf("Pkt: size=%d\n", pkt->size);
        avcodec_send_packet(codecCtx, pkt);

        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            printf("Frame %d: %dx%d fmt=%d linesize=[%d,%d,%d]\n",
                   frameNum, frame->width, frame->height, frame->format,
                   frame->linesize[0], frame->linesize[1], frame->linesize[2]);

            // Check Y plane
            int yNZ = 0, totalY = frame->width * frame->height;
            for (int r = 0; r < frame->height; r++)
                for (int c = 0; c < frame->width; c++)
                    if (frame->data[0][r * frame->linesize[0] + c] != 0) yNZ++;
            printf("  Y non-zero: %d / %d\n", yNZ, totalY);

            if (!sws) {
                sws = sws_getContext(frame->width, frame->height,
                    (AVPixelFormat)frame->format,
                    frame->width, frame->height, AV_PIX_FMT_BGR24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                rgbBuf = (uint8_t*)av_malloc(frame->width * frame->height * 3);
            }

            uint8_t* dst[4] = { rgbBuf, nullptr, nullptr, nullptr };
            int dstStride[4] = { frame->width * 3, 0, 0, 0 };
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);

            int nz = 0;
            for (int i = 0; i < frame->width * frame->height * 3; i++)
                if (rgbBuf[i] != 0) nz++;
            printf("  BGR non-zero: %d / %d\n", nz, frame->width * frame->height * 3);

            char fname[256];
            snprintf(fname, sizeof(fname),
                "C:\\Users\\tittu\\Videos\\webcam\\PhoneCam\\test_m1_frame%d.bmp", frameNum);
            saveBMP(fname, rgbBuf, frame->width, frame->height);
            frameNum++;
        }
        av_packet_unref(pkt);
    }

    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (sws) sws_freeContext(sws);
    if (rgbBuf) av_free(rgbBuf);

    // Method 2: Manual parser + decoder (like our service code)
    printf("\n=== Method 2: manual parser (like our service) ===\n");
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    codecCtx = avcodec_alloc_context3(codec);
    codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecCtx->thread_count = 1;
    avcodec_open2(codecCtx, codec, nullptr);

    AVCodecParserContext* parser = av_parser_init(AV_CODEC_ID_H264);
    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    sws = nullptr;
    rgbBuf = nullptr;

    // Read entire file
    FILE* f = fopen(inputFile, "rb");
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* fileData = new uint8_t[fileSize];
    fread(fileData, 1, fileSize, f);
    fclose(f);
    printf("File size: %ld bytes\n", fileSize);

    const uint8_t* dataPtr = fileData;
    int dataSize = (int)fileSize;
    frameNum = 0;

    while (dataSize > 0 && frameNum < 5) {
        uint8_t* parsedData = nullptr;
        int parsedSize = 0;

        int consumed = av_parser_parse2(
            parser, codecCtx,
            &parsedData, &parsedSize,
            dataPtr, dataSize,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0
        );

        if (consumed < 0) break;
        dataPtr += consumed;
        dataSize -= consumed;

        if (parsedSize > 0) {
            printf("Parsed: size=%d, key=%d\n", parsedSize, parser->key_frame);

            pkt->data = parsedData;
            pkt->size = parsedSize;
            avcodec_send_packet(codecCtx, pkt);

            while (avcodec_receive_frame(codecCtx, frame) == 0) {
                printf("Frame %d: %dx%d fmt=%d linesize=[%d,%d,%d]\n",
                       frameNum, frame->width, frame->height, frame->format,
                       frame->linesize[0], frame->linesize[1], frame->linesize[2]);

                int yNZ = 0;
                for (int r = 0; r < frame->height; r++)
                    for (int c = 0; c < frame->width; c++)
                        if (frame->data[0][r * frame->linesize[0] + c] != 0) yNZ++;
                printf("  Y non-zero: %d / %d\n", yNZ, frame->width * frame->height);

                if (!sws) {
                    sws = sws_getContext(frame->width, frame->height,
                        (AVPixelFormat)frame->format,
                        frame->width, frame->height, AV_PIX_FMT_BGR24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    rgbBuf = (uint8_t*)av_malloc(frame->width * frame->height * 3);
                }

                uint8_t* dst[4] = { rgbBuf, nullptr, nullptr, nullptr };
                int dstStride[4] = { frame->width * 3, 0, 0, 0 };
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);

                int nz = 0;
                for (int i = 0; i < frame->width * frame->height * 3; i++)
                    if (rgbBuf[i] != 0) nz++;
                printf("  BGR non-zero: %d / %d\n", nz, frame->width * frame->height * 3);

                char fname[256];
                snprintf(fname, sizeof(fname),
                    "C:\\Users\\tittu\\Videos\\webcam\\PhoneCam\\test_m2_frame%d.bmp", frameNum);
                saveBMP(fname, rgbBuf, frame->width, frame->height);
                frameNum++;
            }
        }
    }

    // Method 3: NO parser, direct send (like original service code)
    printf("\n=== Method 3: no parser, direct send ===\n");
    av_parser_close(parser);
    avcodec_free_context(&codecCtx);
    if (sws) { sws_freeContext(sws); sws = nullptr; }

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    codecCtx = avcodec_alloc_context3(codec);
    codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecCtx->thread_count = 1;
    avcodec_open2(codecCtx, codec, nullptr);
    rgbBuf = nullptr;

    // Send entire file as one big packet (simulating sending the full stream)
    pkt->data = fileData;
    pkt->size = (int)fileSize;
    int ret = avcodec_send_packet(codecCtx, pkt);
    printf("send_packet ret=%d (fileSize=%ld)\n", ret, fileSize);

    frameNum = 0;
    while (avcodec_receive_frame(codecCtx, frame) == 0 && frameNum < 5) {
        printf("Frame %d: %dx%d fmt=%d linesize=[%d,%d,%d]\n",
               frameNum, frame->width, frame->height, frame->format,
               frame->linesize[0], frame->linesize[1], frame->linesize[2]);

        int yNZ = 0;
        for (int r = 0; r < frame->height; r++)
            for (int c = 0; c < frame->width; c++)
                if (frame->data[0][r * frame->linesize[0] + c] != 0) yNZ++;
        printf("  Y non-zero: %d / %d\n", yNZ, frame->width * frame->height);
        frameNum++;
    }

    delete[] fileData;
    avcodec_free_context(&codecCtx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (rgbBuf) av_free(rgbBuf);

    printf("\nDone.\n");
    return 0;
}
