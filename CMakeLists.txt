cmake_minimum_required(VERSION 3.10)
project(StegoNinja VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# On macOS, help CMake find Homebrew-provided OpenCV/ncurses (Apple Silicon uses
# /opt/homebrew, Intel uses /usr/local). Harmless on other platforms.
if(APPLE)
    foreach(brew_prefix /opt/homebrew /usr/local)
        if(EXISTS "${brew_prefix}")
            list(APPEND CMAKE_PREFIX_PATH "${brew_prefix}")
        endif()
    endforeach()
    if(EXISTS "/opt/homebrew/opt/ncurses")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/ncurses")
    elseif(EXISTS "/usr/local/opt/ncurses")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/ncurses")
    endif()
endif()

find_package(OpenCV QUIET)
find_package(Curses QUIET)
find_package(Threads REQUIRED)

set(INSTALL_TARGETS "")

# ---------------------------------------------------------------------------
# Shared steganography engine + the two unified front-ends.
#
#   stegoninja_engine : one static lib holding ALL technique/format logic
#                       (image LSB, BPCS, audio LSB, video LSB, Vigenere).
#   stegoninja        : the scriptable CLI (OpenCV; no ncurses/highgui).
#   stegoninja-tui    : the interactive ncurses front-end (OpenCV + Curses).
#
# Both binaries link the same engine, so every technique is reachable from
# either (front-end parity). Image LSB and video need OpenCV, so the engine and
# both binaries require it; the build degrades gracefully when it is absent.
# ---------------------------------------------------------------------------
if(OpenCV_FOUND)
    include_directories(${OpenCV_INCLUDE_DIRS})

    add_library(stegoninja_engine STATIC
        src/engine/imglsb.cpp
        src/engine/bpcs.cpp
        src/engine/audiolsb.cpp
        src/engine/videolsb.cpp
        src/vigenere.cpp
    )
    target_link_libraries(stegoninja_engine ${OpenCV_LIBS} Threads::Threads)

    add_executable(stegoninja src/cli/stegoninja.cpp)
    target_link_libraries(stegoninja stegoninja_engine ${OpenCV_LIBS} Threads::Threads)
    list(APPEND INSTALL_TARGETS stegoninja)

    # Engine-level test harness for the image text-LSB path (no ncurses).
    add_executable(lsb_text_test tests/lsb_text_test.cpp)
    target_link_libraries(lsb_text_test stegoninja_engine ${OpenCV_LIBS} Threads::Threads)

    if(CURSES_FOUND)
        include_directories(${CURSES_INCLUDE_DIR})
        add_executable(stegoninja-tui src/tui/stegoninja_tui.cpp)
        target_link_libraries(stegoninja-tui
            stegoninja_engine ${OpenCV_LIBS} ${CURSES_LIBRARIES} Threads::Threads)
        list(APPEND INSTALL_TARGETS stegoninja-tui)
    else()
        message(WARNING
            "Curses not found — building the stegoninja CLI only. "
            "Install libncurses-dev to also build the interactive stegoninja-tui.")
    endif()
else()
    message(WARNING
        "OpenCV not found — cannot build the stegoninja binaries. "
        "Install libopencv-dev (and libncurses-dev for the TUI) then reconfigure.")
endif()

install(TARGETS ${INSTALL_TARGETS} RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION share/doc/stegoninja OPTIONAL)

# ---------------------------------------------------------------------------
# Packaging: `cpack -B <dir>` builds a distributable tarball of the binaries.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "stegoninja")
set(CPACK_PACKAGE_VENDOR "StegoNinja")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline steganography — unified CLI + TUI")
set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")
include(CPack)
