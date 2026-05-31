cmake_minimum_required(VERSION 3.20)
project(PhoneCamPC VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Dependencies:
#   1. FFmpeg (libavcodec, libavformat, libavutil, libswscale)
#      - Download from: https://github.com/BtbN/FFmpeg-Builds/releases
#      - Set FFMPEG_DIR to the extracted folder
#   2. Windows SDK (for DirectShow base classes)
#      - Included with Visual Studio
# ============================================================

# FFmpeg paths (adjust for your system)
set(FFMPEG_DIR "C:/ffmpeg" CACHE PATH "Path to FFmpeg installation")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_DIR}/lib")

set(DRIVER_SOURCES
    driver/VirtualCamFilter.cpp
    driver/VirtualCamFilter.h
    driver/VirtualCamPin.cpp
    driver/VirtualCamPin.h
    driver/FrameBuffer.cpp
    driver/FrameBuffer.h
    driver/DllMain.cpp
    driver/driver.def
)

set(SERVICE_SOURCES
    service/main.cpp
    service/Logger.cpp
    service/Logger.h
    service/Decoder.cpp
    service/Decoder.h
    service/AudioPlayer.cpp
    service/AudioPlayer.h
    service/ReverseAudioCapture.cpp
    service/ReverseAudioCapture.h
    service/TransportClient.cpp
    service/TransportClient.h
    service/UsbClient.cpp
    service/UsbClient.h
    service/BluetoothClient.cpp
    service/BluetoothClient.h
    service/PacketReader.cpp
    service/PacketReader.h
    service/Protocol.h
    driver/FrameBuffer.cpp
    driver/FrameBuffer.h
)

# ---- Virtual Camera DirectShow Filter (DLL) ----
add_library(PhoneCamDriver SHARED ${DRIVER_SOURCES})

target_include_directories(PhoneCamDriver PRIVATE ${FFMPEG_INCLUDE_DIR})

target_link_libraries(PhoneCamDriver PRIVATE
    strmiids
    ole32
    oleaut32
    uuid
    winmm
)

set_target_properties(PhoneCamDriver PROPERTIES
    OUTPUT_NAME "PhoneCamDriver"
    SUFFIX ".dll"
)

# ---- PhoneCam Service (background decoder + transport) ----
add_executable(PhoneCamService ${SERVICE_SOURCES})

target_include_directories(PhoneCamService PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    driver/
)

target_link_directories(PhoneCamService PRIVATE
    ${FFMPEG_LIB_DIR}
)

target_link_libraries(PhoneCamService PRIVATE
    avcodec
    avformat
    avutil
    swscale
    ws2_32
    Bthprops
    winmm
    ole32
)

# Install targets
install(TARGETS PhoneCamDriver RUNTIME DESTINATION bin)
install(TARGETS PhoneCamService RUNTIME DESTINATION bin)
