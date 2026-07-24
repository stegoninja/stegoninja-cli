cmake_minimum_required(VERSION 3.10)
project(SteganoTUI CXX)

# Set C++ version
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include header files from "include" directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Standalone tools — no OpenCV / ncurses needed. Always built.
#   audio          : audio LSB over WAV PCM
#   imgBPCSEmbed   : image BPCS embed (raw BMP)
#   imgBPCSExtract : image BPCS extract (raw BMP)
# ---------------------------------------------------------------------------
add_executable(audio          src/audio.cpp src/vigenere.cpp)
add_executable(imgBPCSEmbed   src/imgBPCSEmbed.cpp)
add_executable(imgBPCSExtract src/imgBPCSExtract.cpp)

set(STANDALONE_TOOLS audio imgBPCSEmbed imgBPCSExtract)

# ---------------------------------------------------------------------------
# OpenCV / ncurses tools — built only when their dependencies are present so
# the standalone tools remain buildable on minimal toolchains.
#   SteganoImgLsb : image LSB TUI  (OpenCV + Curses + Threads)
#   SteganoVid    : video LSB      (OpenCV + Threads)
# ---------------------------------------------------------------------------
find_package(OpenCV QUIET)
find_package(Curses QUIET)
find_package(Threads REQUIRED)

set(OPENCV_TOOLS "")

if(OpenCV_FOUND AND CURSES_FOUND)
    include_directories(${OpenCV_INCLUDE_DIRS} ${CURSES_INCLUDE_DIR})

    add_executable(SteganoImgLsb
        src/main.cpp
        src/stegano.cpp
        src/vigenere.cpp
    )
    target_link_libraries(SteganoImgLsb
        ${OpenCV_LIBS}
        ${CURSES_LIBRARIES}
        Threads::Threads
    )

    add_executable(SteganoVid src/video.cpp src/vigenere.cpp)
    target_link_libraries(SteganoVid
        ${OpenCV_LIBS}
        Threads::Threads
    )

    list(APPEND OPENCV_TOOLS SteganoImgLsb SteganoVid)
else()
    message(WARNING
        "OpenCV and/or Curses not found — skipping SteganoImgLsb and SteganoVid. "
        "Install libopencv-dev and libncurses-dev to build the image LSB TUI and video tool.")
endif()

# Install every built target (see packaging).
install(TARGETS ${STANDALONE_TOOLS} ${OPENCV_TOOLS}
    RUNTIME DESTINATION bin)
