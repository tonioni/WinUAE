cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_APPLE)
    set(WINUAE_UNIX_DEFAULT_MACOS_DEPLOYMENT_TARGET "13.0")
    set(WINUAE_UNIX_MACOS_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../winuae-macos-deps" CACHE PATH "Private macOS dependency prefix built by tools/macos-build-deps.sh")
    set(WINUAE_QEMU_UAE_DEPS_PREFIX "${WINUAE_UNIX_MACOS_DEPS_PREFIX}" CACHE PATH "Private macOS GLib prefix for the QEMU-UAE plugin")
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${WINUAE_UNIX_DEFAULT_MACOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS version for WinUAE Unix builds")
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${WINUAE_UNIX_DEFAULT_MACOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS version for WinUAE Unix builds" FORCE)
    endif()
endif()

project(winuae_unix_port LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

set(WINUAE_UNIX_TEST_TARGETS)
function(winuae_unix_register_test target)
    list(APPEND WINUAE_UNIX_TEST_TARGETS "${target}")
    set(WINUAE_UNIX_TEST_TARGETS "${WINUAE_UNIX_TEST_TARGETS}" PARENT_SCOPE)
    if(BUILD_TESTING)
        add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
        set_tests_properties(${target} PROPERTIES LABELS "unix")
    endif()
endfunction()

function(winuae_unix_imported_location target out_var)
    foreach(prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_MINSIZEREL IMPORTED_LOCATION_DEBUG IMPORTED_LOCATION)
        get_target_property(location "${target}" "${prop}")
        if(location AND NOT location MATCHES "-NOTFOUND$" AND EXISTS "${location}")
            set("${out_var}" "${location}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set("${out_var}" "" PARENT_SCOPE)
endfunction()

function(winuae_unix_macos_find_qt_frameworks out_var)
    set(prefix_candidates ${CMAKE_PREFIX_PATH})
    if(WINUAE_UNIX_MACOS_DEPS_PREFIX)
        list(APPEND prefix_candidates "${WINUAE_UNIX_MACOS_DEPS_PREFIX}")
    endif()
    list(REMOVE_DUPLICATES prefix_candidates)

    foreach(prefix IN LISTS prefix_candidates)
        if(EXISTS "${prefix}/lib/QtWidgets.framework/QtWidgets"
                AND EXISTS "${prefix}/lib/QtGui.framework/QtGui"
                AND EXISTS "${prefix}/lib/QtCore.framework/QtCore")
            if(NOT TARGET winuae_unix_qt_frameworks)
                add_library(winuae_unix_qt_frameworks INTERFACE)
                target_include_directories(winuae_unix_qt_frameworks INTERFACE
                    "${prefix}/lib/QtWidgets.framework/Headers"
                    "${prefix}/lib/QtGui.framework/Headers"
                    "${prefix}/lib/QtCore.framework/Headers"
                )
                target_compile_definitions(winuae_unix_qt_frameworks INTERFACE
                    QT_WIDGETS_LIB
                    QT_GUI_LIB
                    QT_CORE_LIB
                )
                target_compile_options(winuae_unix_qt_frameworks INTERFACE
                    "-F${prefix}/lib"
                )
                target_link_options(winuae_unix_qt_frameworks INTERFACE
                    "-F${prefix}/lib"
                )
                target_link_libraries(winuae_unix_qt_frameworks INTERFACE
                    "${prefix}/lib/QtWidgets.framework"
                    "${prefix}/lib/QtGui.framework"
                    "${prefix}/lib/QtCore.framework"
                )
            endif()
            if(EXISTS "${prefix}/bin/macdeployqt")
                set(MACDEPLOYQT_EXECUTABLE "${prefix}/bin/macdeployqt" CACHE FILEPATH "macdeployqt executable" FORCE)
            endif()
            set("${out_var}" winuae_unix_qt_frameworks PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set("${out_var}" "" PARENT_SCOPE)
endfunction()

function(winuae_unix_find_lzma_sdk out_var)
    # A system lzma-sdk source package (e.g. openSUSE lzma-sdk-devel) is
    # preferred over fetching the vendored archive; it must provide the SDK
    # C sources, not just headers.
    set(WINUAE_LZMA_SDK_CANDIDATES
        "${WINUAE_LZMA_SDK_DIR}/C"
        "${WINUAE_LZMA_SDK_DIR}"
    )
    foreach(prefix IN LISTS WINUAE_LZMA_SDK_SYSTEM_PATHS)
        list(APPEND WINUAE_LZMA_SDK_CANDIDATES "${prefix}/C" "${prefix}")
    endforeach()
    foreach(candidate IN LISTS WINUAE_LZMA_SDK_CANDIDATES)
        if(EXISTS "${candidate}/7z/7zVersion.h" AND EXISTS "${candidate}/7z/7zCrc.c")
            set(WINUAE_LZMA_SDK_SOURCE_DIR "${candidate}")
            break()
        endif()
    endforeach()

    if(NOT WINUAE_LZMA_SDK_SOURCE_DIR)
        if(NOT WINUAE_LZMA_SDK_FETCH)
            message(FATAL_ERROR
                "7-Zip/LZMA SDK ${WINUAE_LZMA_SDK_VERSION} was not found in "
                "${WINUAE_LZMA_SDK_DIR}. Enable WINUAE_LZMA_SDK_FETCH or "
                "pre-seed WINUAE_LZMA_SDK_DIR with the SDK archive contents."
            )
        endif()

        message(STATUS
            "Fetching 7-Zip/LZMA SDK ${WINUAE_LZMA_SDK_VERSION} into "
            "${WINUAE_LZMA_SDK_DIR}"
        )
        execute_process(
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/fetch-lzma-sdk.sh"
                    "${WINUAE_LZMA_SDK_DIR}"
                    "${WINUAE_LZMA_SDK_URL}"
                    "${WINUAE_LZMA_SDK_VERSION}"
                    "${WINUAE_LZMA_SDK_SHA256}"
            RESULT_VARIABLE WINUAE_LZMA_SDK_FETCH_RESULT
            OUTPUT_VARIABLE WINUAE_LZMA_SDK_FETCH_OUTPUT
            ERROR_VARIABLE WINUAE_LZMA_SDK_FETCH_ERROR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        if(NOT WINUAE_LZMA_SDK_FETCH_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Failed to fetch 7-Zip/LZMA SDK "
                "${WINUAE_LZMA_SDK_VERSION}: "
                "${WINUAE_LZMA_SDK_FETCH_ERROR}\n"
                "${WINUAE_LZMA_SDK_FETCH_OUTPUT}"
            )
        endif()

        foreach(candidate IN LISTS WINUAE_LZMA_SDK_CANDIDATES)
            if(EXISTS "${candidate}/7z/7zVersion.h")
                set(WINUAE_LZMA_SDK_SOURCE_DIR "${candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(NOT WINUAE_LZMA_SDK_SOURCE_DIR)
        message(FATAL_ERROR
            "7-Zip/LZMA SDK ${WINUAE_LZMA_SDK_VERSION} was fetched, but "
            "no 7z/7zVersion.h was found in ${WINUAE_LZMA_SDK_DIR}."
        )
    endif()

    file(STRINGS "${WINUAE_LZMA_SDK_SOURCE_DIR}/7z/7zVersion.h"
        WINUAE_LZMA_SDK_VERSION_LINES
        REGEX "^#define MY_VERSION \"${WINUAE_LZMA_SDK_VERSION}\""
    )
    if(NOT WINUAE_LZMA_SDK_VERSION_LINES)
        message(FATAL_ERROR
            "Expected 7-Zip/LZMA SDK ${WINUAE_LZMA_SDK_VERSION}, but "
            "${WINUAE_LZMA_SDK_SOURCE_DIR}/7z/7zVersion.h did not match."
        )
    endif()

    set("${out_var}" "${WINUAE_LZMA_SDK_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

option(WINUAE_UNIX_BUILD_EXECUTABLE "Link the initial Unix command-line executable" ON)
option(WINUAE_UNIX_WITH_SDL3 "Build the Unix SDL3 video/audio backend" ON)
option(WINUAE_UNIX_WITH_SLIRP "Build built-in SLIRP user-mode networking and A2065 Ethernet support" ON)
option(WINUAE_UNIX_WITH_SANA2 "Build uaenet.device SANA-II networking on top of the Unix Ethernet backend" ON)
option(WINUAE_UNIX_WITH_UAENET_PCAP "Build native libpcap and TAP/TUN packet networking for A2065 and uaenet.device" ON)
option(WINUAE_UNIX_WITH_PROWIZARD "Build Pro-Wizard module ripper support" ON)
option(WINUAE_UNIX_WITH_QT_UI "Build the optional Unix Qt configuration UI" ON)
option(WINUAE_UNIX_WITH_INTEGRATED_QT_UI "Allow winuae_unix to host the Unix Qt frontend" ON)
option(WINUAE_UNIX_WITH_LIBPNG "Build optional PNG screenshot support when libpng is available" ON)
option(WINUAE_UNIX_WITH_ARCHIVES "Build bundled ZIP/LHA/LZX/DMS/WRP archive support" ON)
option(WINUAE_UNIX_WITH_CHD "Build CHD hardfile and CD image support" ON)
option(WINUAE_UNIX_WITH_CHD_FLAC "Build CHD FLAC codecs when a compatible libFLAC is available" ON)
option(WINUAE_UNIX_WITH_LIBMPEG2 "Build CD32 FMV video decode support when libmpeg2 is available" ON)
option(WINUAE_UNIX_WITH_BSDSOCKET "Build bsdsocket.library on top of POSIX sockets" ON)
option(WINUAE_UNIX_WITH_UAESCSI "Build uaescsi.device using the shared CD/tape SCSI emulation layer" ON)
option(WINUAE_UNIX_WITH_UAESERIAL "Build uaeserial.device on top of the Unix serial backend" ON)
option(WINUAE_UNIX_WITH_SAMPLER "Build SDL3 recording support for the parallel-port sampler" ON)
option(WINUAE_UNIX_WITH_NATIVE_HARDDRIVES "Build Unix raw hard-drive enumeration and read-only open support" ON)
option(WINUAE_UNIX_WITH_NATIVE_CD "Build Unix native CD/DVD drive access" ON)
option(WINUAE_UNIX_WITH_NATIVE_SCSI "Build native SCSI/tape passthrough (macOS SCSITaskLib, Linux SG_IO)" ON)
option(WINUAE_UNIX_WITH_MIDI "Build native Unix MIDI support" ON)
option(WINUAE_UNIX_WITH_MIDIEMU "Build Munt MT-32/CM-32L MIDI emulation when mt32emu is available" ON)
option(WINUAE_UNIX_WITH_CPUBOARD "Build shared non-PPC CPU accelerator board support" ON)
option(WINUAE_UNIX_WITH_PPC_QEMU "Build Unix PPC accelerator support through the qemu-uae plugin ABI" ON)
option(WINUAE_UNIX_BUILD_QEMU_UAE_PLUGIN "Build the sibling qemu-uae plugin for Unix packaging" ON)
option(WINUAE_UNIX_WITH_DSP "Build shared DSP3210 accelerator support" ON)
option(WINUAE_UNIX_WITH_AVIOUTPUT "Build Unix screenshot and video/audio capture output support" ON)
option(WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE "Build OpenGL-backed Unix shader/filter presentation support" ON)
option(WINUAE_LZMA_SDK_FETCH "Fetch the 7-Zip/LZMA SDK into WINUAE_LZMA_SDK_DIR when missing" ON)

set(WINUAE_LZMA_SDK_VERSION "16.04" CACHE STRING "7-Zip/LZMA SDK version used by archive and CHD support")
set(WINUAE_LZMA_SDK_URL "https://www.7-zip.org/a/lzma1604.7z"
    CACHE STRING "URL for the 7-Zip/LZMA SDK archive")
set(WINUAE_LZMA_SDK_SHA256 "edf8690e9d5ae0dbe80751e3df247bb4e1b497b100c4af5b95fc50a5c2e5b04f"
    CACHE STRING "Expected SHA-256 for the 7-Zip/LZMA SDK archive")
set(WINUAE_LZMA_SDK_DIR "${CMAKE_BINARY_DIR}/_deps/lzma-sdk/${WINUAE_LZMA_SDK_VERSION}"
    CACHE PATH "Local cache directory for the 7-Zip/LZMA SDK")
set(WINUAE_LZMA_SDK_SYSTEM_PATHS
    "/usr/share/lzma-sdk;/usr/include/lzma-sdk;/usr/src/lzma-sdk;/usr/include/lzma"
    CACHE STRING "System prefixes probed for an installed 7-Zip/LZMA SDK source tree")

set(WINUAE_QEMU_UAE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qemu-uae-v11.0"
    CACHE PATH "QEMU-UAE plugin source directory")
set(WINUAE_QEMU_UAE_PLUGIN_FILE "" CACHE FILEPATH "Prebuilt qemu-uae.so plugin to install or bundle")
set(WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE "${CMAKE_INSTALL_LIBDIR}/winuae/plugins"
    CACHE STRING "Relative Unix install directory for runtime plugins")

if(IS_ABSOLUTE "${WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE}")
    message(FATAL_ERROR "WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE must be relative")
endif()

set(WINUAE_UNIX_TARGET_ARCHES "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(WINUAE_UNIX_TARGET_ARCHES ${CMAKE_OSX_ARCHITECTURES})
endif()

set(WINUAE_UNIX_JIT_TARGET_SUPPORTED OFF)
foreach(WINUAE_UNIX_TARGET_ARCH IN LISTS WINUAE_UNIX_TARGET_ARCHES)
    if(WINUAE_UNIX_TARGET_ARCH MATCHES "^(aarch64|arm64|x86_64|amd64|AMD64)$")
        set(WINUAE_UNIX_JIT_TARGET_SUPPORTED ON)
    endif()
endforeach()

set(WINUAE_UNIX_WITH_JIT_DEFAULT ${WINUAE_UNIX_JIT_TARGET_SUPPORTED})
option(WINUAE_UNIX_WITH_JIT "Build JIT support for Unix targets where a backend is available" ${WINUAE_UNIX_WITH_JIT_DEFAULT})

set(WINUAE_UNIX_VERSION_MAJOR 0)
set(WINUAE_UNIX_VERSION_MINOR 0)
set(WINUAE_UNIX_VERSION_REVISION 0)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/options.h" WINUAE_UNIX_VERSION_LINES REGEX "^#define UAE(MAJOR|MINOR|SUBREV) [0-9]+")
foreach(WINUAE_UNIX_VERSION_LINE IN LISTS WINUAE_UNIX_VERSION_LINES)
    if(WINUAE_UNIX_VERSION_LINE MATCHES "^#define UAEMAJOR +([0-9]+)")
        set(WINUAE_UNIX_VERSION_MAJOR "${CMAKE_MATCH_1}")
    elseif(WINUAE_UNIX_VERSION_LINE MATCHES "^#define UAEMINOR +([0-9]+)")
        set(WINUAE_UNIX_VERSION_MINOR "${CMAKE_MATCH_1}")
    elseif(WINUAE_UNIX_VERSION_LINE MATCHES "^#define UAESUBREV +([0-9]+)")
        set(WINUAE_UNIX_VERSION_REVISION "${CMAKE_MATCH_1}")
    endif()
endforeach()
set(WINUAE_UNIX_VERSION "${WINUAE_UNIX_VERSION_MAJOR}.${WINUAE_UNIX_VERSION_MINOR}.${WINUAE_UNIX_VERSION_REVISION}")

find_package(ZLIB REQUIRED)
if(WINUAE_UNIX_WITH_LIBPNG)
    if(APPLE AND CMAKE_PREFIX_PATH)
        unset(PNG_LIBRARY CACHE)
        unset(PNG_LIBRARY_RELEASE CACHE)
        unset(PNG_LIBRARY_DEBUG CACHE)
        unset(PNG_PNG_INCLUDE_DIR CACHE)
    endif()
    find_package(PNG QUIET)
endif()
find_package(PkgConfig QUIET)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_library(winuae_core OBJECT)

target_compile_definitions(winuae_core PRIVATE
    UAE_TARGET_UNIX=1
    UAE_NOGUI=1
    WINUAE_UNIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    WINUAE_UNIX_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/winuae"
    WINUAE_UNIX_INSTALL_DATADIR_RELATIVE="${CMAKE_INSTALL_DATADIR}/winuae"
    WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE="${WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE}"
)

target_include_directories(winuae_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/od-unix"
    "${CMAKE_CURRENT_SOURCE_DIR}/od-unix/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers/dms"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers/lha"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers/lzx"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers/mp2"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers/wrp"
    "${CMAKE_CURRENT_SOURCE_DIR}/archivers/zip"
)

if(WINUAE_UNIX_WITH_ARCHIVES OR WINUAE_UNIX_WITH_CHD)
    winuae_unix_find_lzma_sdk(WINUAE_LZMA_SDK_SOURCE_DIR)
    set(WINUAE_LZMA_SDK_7Z_DIR "${WINUAE_LZMA_SDK_SOURCE_DIR}/7z")
    target_include_directories(winuae_core PRIVATE "${WINUAE_LZMA_SDK_SOURCE_DIR}")
    target_compile_definitions(winuae_core PRIVATE
        _7ZIP_ST=1
        UAE_7Z_SDK_1604=1
    )
endif()

target_compile_options(winuae_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-narrowing>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-register>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-format-security>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-multichar>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-value>
)

set(WINUAE_UNIX_EXTRA_SOURCES)
set(WINUAE_UNIX_EXTRA_LIBRARIES)
set(WINUAE_UNIX_EXTRA_LIBRARY_DIRS)
set(WINUAE_UNIX_ENABLE_SANA2 OFF)
set(WINUAE_UNIX_ENABLE_UAENET_PCAP OFF)
set(WINUAE_UNIX_ENABLE_BSDSOCKET OFF)
set(WINUAE_UNIX_ENABLE_UAESCSI OFF)
set(WINUAE_UNIX_ENABLE_UAESERIAL OFF)
set(WINUAE_UNIX_ENABLE_SAMPLER OFF)
set(WINUAE_UNIX_ENABLE_NATIVE_HARDDRIVES OFF)
set(WINUAE_UNIX_ENABLE_NATIVE_CD OFF)
set(WINUAE_UNIX_ENABLE_NATIVE_SCSI OFF)
set(WINUAE_UNIX_ENABLE_MIDI OFF)
set(WINUAE_UNIX_ENABLE_MIDIEMU OFF)
set(WINUAE_UNIX_ENABLE_CPUBOARD OFF)
set(WINUAE_UNIX_ENABLE_PPC OFF)
set(WINUAE_UNIX_ENABLE_DSP OFF)
set(WINUAE_UNIX_ENABLE_SNDBOARD OFF)
set(WINUAE_UNIX_ENABLE_PCI OFF)
set(WINUAE_UNIX_ENABLE_GFXBOARD OFF)
set(WINUAE_UNIX_ENABLE_X86 OFF)
set(WINUAE_UNIX_ENABLE_DRACO OFF)
set(WINUAE_UNIX_ENABLE_PCEM OFF)
set(WINUAE_UNIX_ENABLE_JIT OFF)
set(WINUAE_UNIX_ENABLE_AVIOUTPUT OFF)
set(WINUAE_UNIX_ENABLE_SHADER_PIPELINE OFF)
set(WINUAE_UNIX_MIDI_LIBRARIES)
set(WINUAE_UNIX_MIDI_INCLUDE_DIRS)
set(WINUAE_UNIX_MIDI_LIBRARY_DIRS)
set(WINUAE_UNIX_MIDI_COMPILE_OPTIONS)
set(WINUAE_UNIX_MIDI_QT_DEFINITIONS)

set(WINUAE_UNIX_PCEM_COMMON_SOURCES
    od-unix/pcem_wincompat.cpp
    pcem/pcemglue.cpp
    pcem/timer.cpp
)

set(WINUAE_UNIX_PCEM_VIDEO_SOURCES
    pcem/vid_bt482_ramdac.cpp
    pcem/vid_cl5429.cpp
    pcem/vid_et4000.cpp
    pcem/vid_et4000w32.cpp
    pcem/vid_inmos.cpp
    pcem/vid_mga.cpp
    pcem/vid_ncr.cpp
    pcem/vid_permedia2.cpp
    pcem/vid_s3.cpp
    pcem/vid_s3_virge.cpp
    pcem/vid_sc1502x_ramdac.cpp
    pcem/vid_sdac_ramdac.cpp
    pcem/vid_svga.cpp
    pcem/vid_svga_render.cpp
    pcem/vid_tvp3026_ramdac.cpp
    pcem/vid_voodoo.cpp
    pcem/vid_voodoo_banshee.cpp
    pcem/vid_voodoo_banshee_blitter.cpp
    pcem/vid_voodoo_blitter.cpp
    pcem/vid_voodoo_display.cpp
    pcem/vid_voodoo_fb.cpp
    pcem/vid_voodoo_fifo.cpp
    pcem/vid_voodoo_reg.cpp
    pcem/vid_voodoo_render.cpp
    pcem/vid_voodoo_setup.cpp
    pcem/vid_voodoo_texture.cpp
    qemuvga/cirrus_vga.cpp
    qemuvga/es1370.cpp
    qemuvga/ne2000.cpp
    qemuvga/qemu.cpp
    qemuvga/qemuuaeglue.cpp
    qemuvga/vga.cpp
)

set(WINUAE_UNIX_PCEM_X86_SOURCES
    pcem/386.cpp
    pcem/386_common.cpp
    pcem/386_dynarec.cpp
    pcem/808x.cpp
    pcem/cpu.cpp
    pcem/dma.cpp
    pcem/dosbox/dbopl.cpp
    pcem/dosbox/nukedopl.cpp
    pcem/keyboard.cpp
    pcem/keyboard_at.cpp
    pcem/mem.cpp
    pcem/mouse_ps2.cpp
    pcem/mouse_serial.cpp
    pcem/nvr.cpp
    pcem/pcemrtc.cpp
    pcem/pic.cpp
    pcem/pit.cpp
    pcem/serial.cpp
    pcem/sound_cms.cpp
    pcem/sound_dbopl.cpp
    pcem/sound_mpu401_uart.cpp
    pcem/sound_opl.cpp
    pcem/sound_sb.cpp
    pcem/sound_sb_dsp.cpp
    pcem/sound_speaker.cpp
    pcem/x86seg.cpp
    pcem/x87.cpp
    pcem/x87_timings.cpp
)

set(WINUAE_UNIX_PCEM_DRACO_SOURCES
    pcem/keyboard.cpp
    pcem/keyboard_at.cpp
    pcem/keyboard_at_draco.cpp
    pcem/mouse_serial.cpp
    pcem/serial.cpp
)

set(WINUAE_UNIX_USE_LIBPNG OFF)
if(WINUAE_UNIX_WITH_LIBPNG AND PNG_FOUND)
    set(WINUAE_UNIX_USE_LIBPNG ON)
    if(APPLE)
        set(WINUAE_UNIX_PNG_LIBRARY "")
        get_target_property(WINUAE_UNIX_PNG_LIBRARY_RELEASE PNG::PNG IMPORTED_LOCATION_RELEASE)
        get_target_property(WINUAE_UNIX_PNG_LIBRARY_DEFAULT PNG::PNG IMPORTED_LOCATION)
        if(WINUAE_UNIX_PNG_LIBRARY_RELEASE AND EXISTS "${WINUAE_UNIX_PNG_LIBRARY_RELEASE}")
            set(WINUAE_UNIX_PNG_LIBRARY "${WINUAE_UNIX_PNG_LIBRARY_RELEASE}")
        elseif(WINUAE_UNIX_PNG_LIBRARY_DEFAULT AND EXISTS "${WINUAE_UNIX_PNG_LIBRARY_DEFAULT}")
            set(WINUAE_UNIX_PNG_LIBRARY "${WINUAE_UNIX_PNG_LIBRARY_DEFAULT}")
        elseif(PNG_LIBRARY_RELEASE AND EXISTS "${PNG_LIBRARY_RELEASE}")
            set(WINUAE_UNIX_PNG_LIBRARY "${PNG_LIBRARY_RELEASE}")
        endif()
        if(WINUAE_UNIX_PNG_LIBRARY)
            execute_process(
                COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-check-deployment-target.sh" "${WINUAE_UNIX_PNG_LIBRARY}" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                RESULT_VARIABLE WINUAE_UNIX_PNG_DEPLOYMENT_RESULT
                OUTPUT_VARIABLE WINUAE_UNIX_PNG_DEPLOYMENT_OUTPUT
                ERROR_VARIABLE WINUAE_UNIX_PNG_DEPLOYMENT_ERROR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE
            )
            if(NOT WINUAE_UNIX_PNG_DEPLOYMENT_RESULT EQUAL 0)
                message(STATUS "libpng is newer than CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}; PNG screenshots disabled. ${WINUAE_UNIX_PNG_DEPLOYMENT_ERROR}")
                set(WINUAE_UNIX_USE_LIBPNG OFF)
            endif()
        else()
            message(WARNING "libpng was found but its imported library path was not available; PNG screenshots disabled")
            set(WINUAE_UNIX_USE_LIBPNG OFF)
        endif()
    endif()
endif()

if(WINUAE_UNIX_USE_LIBPNG)
    target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_LIBPNG=1)
    target_link_libraries(winuae_core PRIVATE PNG::PNG)
    list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES PNG::PNG)
endif()

if(APPLE)
    target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_IMAGEIO=1)
    list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
endif()

if(WINUAE_UNIX_WITH_ARCHIVES)
    target_compile_definitions(winuae_core PRIVATE
        WINUAE_UNIX_WITH_ARCHIVES=1
        A_ZIP=1
        A_7Z=1
        A_LHA=1
        A_LZX=1
        A_DMS=1
        A_WRP=1
    )
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        zfile_archive.cpp
        archivers/zip/ioapi.cpp
        archivers/zip/unzip.cpp
        archivers/zip/zip.cpp
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zAlloc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zArcIn.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zBuf.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zBuf2.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zCrc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zCrcOpt.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zDec.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zFile.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/7zStream.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Aes.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/AesOpt.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Alloc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Bcj2.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Bra.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Bra86.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/BraIA64.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/CpuArch.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Delta.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/LzFind.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Lzma2Dec.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Lzma2Enc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Lzma86Dec.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Lzma86Enc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/LzmaDec.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/LzmaEnc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/LzmaLib.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Ppmd7.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Ppmd7Dec.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Ppmd7Enc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Sha256.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Sort.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/Xz.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/XzCrc64.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/XzCrc64Opt.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/XzDec.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/XzEnc.c"
        "${WINUAE_LZMA_SDK_7Z_DIR}/XzIn.c"
        archivers/lha/crcio.cpp
        archivers/lha/dhuf.cpp
        archivers/lha/header.cpp
        archivers/lha/huf.cpp
        archivers/lha/larc.cpp
        archivers/lha/lhamaketbl.cpp
        archivers/lha/lharc.cpp
        archivers/lha/shuf.cpp
        archivers/lha/slide.cpp
        archivers/lha/uae_lha.cpp
        archivers/lha/util.cpp
        archivers/lzx/unlzx.cpp
        archivers/dms/crc_csum.cpp
        archivers/dms/getbits.cpp
        archivers/dms/maketbl.cpp
        archivers/dms/pfile.cpp
        archivers/dms/tables.cpp
        archivers/dms/u_deep.cpp
        archivers/dms/u_heavy.cpp
        archivers/dms/u_init.cpp
        archivers/dms/u_medium.cpp
        archivers/dms/u_quick.cpp
        archivers/dms/u_rle.cpp
        archivers/wrp/warp.cpp
    )
endif()

if(WINUAE_UNIX_WITH_CHD)
    target_compile_definitions(winuae_core PRIVATE
        WINUAE_UNIX_WITH_CHD=1
        WITH_CHD=1
    )
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        archivers/chd/astring.cpp
        archivers/chd/chd.cpp
        archivers/chd/chdcdrom.cpp
        archivers/chd/chdcodec.cpp
        archivers/chd/chdglue.cpp
        archivers/chd/corealloc.cpp
        archivers/chd/harddisk.cpp
        archivers/chd/hashing.cpp
        archivers/chd/huffman.cpp
        archivers/chd/md5.cpp
        archivers/chd/sha1.cpp
        od-unix/chd_unix_time.cpp
        od-unix/chd_unix_work.cpp
    )
    if(NOT WINUAE_UNIX_WITH_ARCHIVES)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES
            "${WINUAE_LZMA_SDK_7Z_DIR}/LzmaDec.c"
            "${WINUAE_LZMA_SDK_7Z_DIR}/LzmaEnc.c"
            "${WINUAE_LZMA_SDK_7Z_DIR}/LzFind.c"
        )
    endif()

    set(WINUAE_UNIX_CHD_FLAC_OK OFF)
    if(WINUAE_UNIX_WITH_CHD_FLAC AND PkgConfig_FOUND)
        pkg_check_modules(FLAC QUIET flac)
        if(FLAC_FOUND)
            set(WINUAE_UNIX_CHD_FLAC_OK ON)
            if(APPLE)
                unset(WINUAE_UNIX_FLAC_LIBRARY CACHE)
                find_library(WINUAE_UNIX_FLAC_LIBRARY NAMES FLAC PATHS ${FLAC_LIBRARY_DIRS} NO_DEFAULT_PATH)
                if(WINUAE_UNIX_FLAC_LIBRARY)
                    execute_process(
                        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-check-deployment-target.sh" "${WINUAE_UNIX_FLAC_LIBRARY}" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                        RESULT_VARIABLE WINUAE_UNIX_FLAC_DEPLOYMENT_RESULT
                        OUTPUT_VARIABLE WINUAE_UNIX_FLAC_DEPLOYMENT_OUTPUT
                        ERROR_VARIABLE WINUAE_UNIX_FLAC_DEPLOYMENT_ERROR
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_STRIP_TRAILING_WHITESPACE
                    )
                    if(NOT WINUAE_UNIX_FLAC_DEPLOYMENT_RESULT EQUAL 0)
                        message(STATUS "libFLAC is newer than CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}; CHD FLAC codecs disabled. ${WINUAE_UNIX_FLAC_DEPLOYMENT_ERROR}")
                        set(WINUAE_UNIX_CHD_FLAC_OK OFF)
                    endif()
                else()
                    message(STATUS "libFLAC was found by pkg-config but not as a library path; CHD FLAC codecs disabled")
                    set(WINUAE_UNIX_CHD_FLAC_OK OFF)
                endif()
            endif()
        endif()
    endif()

    if(WINUAE_UNIX_CHD_FLAC_OK)
        target_compile_definitions(winuae_core PRIVATE WITH_CHD_FLAC=1)
        target_include_directories(winuae_core PRIVATE ${FLAC_INCLUDE_DIRS})
        target_compile_options(winuae_core PRIVATE ${FLAC_CFLAGS_OTHER})
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES archivers/chd/flac.cpp)
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${FLAC_LIBRARIES})
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARY_DIRS ${FLAC_LIBRARY_DIRS})
    else()
        target_compile_definitions(winuae_core PRIVATE WITH_CHD_FLAC=0)
    endif()
endif()

set(WINUAE_UNIX_LIBMPEG2_OK OFF)
set(WINUAE_UNIX_LIBMPEG2_FOUND OFF)
if(WINUAE_UNIX_WITH_LIBMPEG2 AND PkgConfig_FOUND)
    pkg_check_modules(LIBMPEG2 QUIET libmpeg2 libmpeg2convert)
    if(LIBMPEG2_FOUND)
        set(WINUAE_UNIX_LIBMPEG2_FOUND ON)
        set(WINUAE_UNIX_LIBMPEG2_OK ON)
        if(APPLE)
            unset(WINUAE_UNIX_LIBMPEG2_LIBRARY CACHE)
            unset(WINUAE_UNIX_LIBMPEG2CONVERT_LIBRARY CACHE)
            find_library(WINUAE_UNIX_LIBMPEG2_LIBRARY
                NAMES mpeg2
                PATHS ${LIBMPEG2_LIBRARY_DIRS}
                NO_DEFAULT_PATH
            )
            find_library(WINUAE_UNIX_LIBMPEG2CONVERT_LIBRARY
                NAMES mpeg2convert
                PATHS ${LIBMPEG2_LIBRARY_DIRS}
                NO_DEFAULT_PATH
            )
            if(WINUAE_UNIX_LIBMPEG2_LIBRARY AND WINUAE_UNIX_LIBMPEG2CONVERT_LIBRARY)
                foreach(WINUAE_UNIX_LIBMPEG2_CHECK_LIBRARY IN ITEMS WINUAE_UNIX_LIBMPEG2_LIBRARY WINUAE_UNIX_LIBMPEG2CONVERT_LIBRARY)
                    execute_process(
                        COMMAND
                            "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-check-deployment-target.sh"
                            "${${WINUAE_UNIX_LIBMPEG2_CHECK_LIBRARY}}"
                            "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                        RESULT_VARIABLE WINUAE_UNIX_LIBMPEG2_DEPLOYMENT_RESULT
                        OUTPUT_VARIABLE WINUAE_UNIX_LIBMPEG2_DEPLOYMENT_OUTPUT
                        ERROR_VARIABLE WINUAE_UNIX_LIBMPEG2_DEPLOYMENT_ERROR
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_STRIP_TRAILING_WHITESPACE
                    )
                    if(NOT WINUAE_UNIX_LIBMPEG2_DEPLOYMENT_RESULT EQUAL 0)
                        message(STATUS
                            "libmpeg2 is newer than "
                            "CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}; "
                            "CD32 FMV video decode disabled. "
                            "${WINUAE_UNIX_LIBMPEG2_DEPLOYMENT_ERROR}"
                        )
                        set(WINUAE_UNIX_LIBMPEG2_OK OFF)
                    endif()
                endforeach()
            else()
                message(STATUS
                    "libmpeg2 was found by pkg-config but not as library "
                    "paths; CD32 FMV video decode disabled"
                )
                set(WINUAE_UNIX_LIBMPEG2_OK OFF)
            endif()
        endif()
    endif()
endif()

if(WINUAE_UNIX_LIBMPEG2_OK)
    target_compile_definitions(winuae_core PRIVATE WITH_LIBMPEG2=1)
    target_include_directories(winuae_core PRIVATE ${LIBMPEG2_INCLUDE_DIRS})
    target_compile_options(winuae_core PRIVATE ${LIBMPEG2_CFLAGS_OTHER})
    if(APPLE)
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES
            "${WINUAE_UNIX_LIBMPEG2_LIBRARY}"
            "${WINUAE_UNIX_LIBMPEG2CONVERT_LIBRARY}"
        )
    else()
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${LIBMPEG2_LIBRARIES})
    endif()
    list(APPEND WINUAE_UNIX_EXTRA_LIBRARY_DIRS ${LIBMPEG2_LIBRARY_DIRS})
elseif(WINUAE_UNIX_WITH_LIBMPEG2 AND NOT WINUAE_UNIX_LIBMPEG2_FOUND)
    message(STATUS "libmpeg2 development package not found; CD32 FMV video decode disabled")
endif()

set(WINUAE_UNIX_MT32EMU_OK OFF)
set(WINUAE_UNIX_MT32EMU_FOUND OFF)
if(WINUAE_UNIX_WITH_MIDIEMU AND PkgConfig_FOUND)
    pkg_check_modules(MT32EMU QUIET mt32emu)
    if(MT32EMU_FOUND)
        set(WINUAE_UNIX_MT32EMU_FOUND ON)
        set(WINUAE_UNIX_MT32EMU_OK ON)
        if(APPLE)
            unset(WINUAE_UNIX_MT32EMU_LIBRARY CACHE)
            find_library(WINUAE_UNIX_MT32EMU_LIBRARY
                NAMES mt32emu
                PATHS ${MT32EMU_LIBRARY_DIRS}
                NO_DEFAULT_PATH
            )
            if(WINUAE_UNIX_MT32EMU_LIBRARY)
                execute_process(
                    COMMAND
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-check-deployment-target.sh"
                        "${WINUAE_UNIX_MT32EMU_LIBRARY}"
                        "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                    RESULT_VARIABLE WINUAE_UNIX_MT32EMU_DEPLOYMENT_RESULT
                    OUTPUT_VARIABLE WINUAE_UNIX_MT32EMU_DEPLOYMENT_OUTPUT
                    ERROR_VARIABLE WINUAE_UNIX_MT32EMU_DEPLOYMENT_ERROR
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE
                )
                if(NOT WINUAE_UNIX_MT32EMU_DEPLOYMENT_RESULT EQUAL 0)
                    message(STATUS
                        "libmt32emu is newer than "
                        "CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}; "
                        "Munt MIDI emulation disabled. "
                        "${WINUAE_UNIX_MT32EMU_DEPLOYMENT_ERROR}"
                    )
                    set(WINUAE_UNIX_MT32EMU_OK OFF)
                endif()
            else()
                message(STATUS
                    "mt32emu was found by pkg-config but not as a library "
                    "path; Munt MIDI emulation disabled"
                )
                set(WINUAE_UNIX_MT32EMU_OK OFF)
            endif()
        endif()
    endif()
endif()

if(WINUAE_UNIX_WITH_NATIVE_HARDDRIVES)
    set(WINUAE_UNIX_ENABLE_NATIVE_HARDDRIVES ON)
    target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_NATIVE_HARDDRIVES=1)
endif()

if(WINUAE_UNIX_WITH_MIDI)
    if(APPLE)
        set(WINUAE_UNIX_ENABLE_MIDI ON)
        target_compile_definitions(winuae_core PRIVATE WITH_MIDI=1 WINUAE_UNIX_WITH_COREMIDI=1)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/midi.cpp)
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES "-framework CoreMIDI" "-framework CoreFoundation")
        list(APPEND WINUAE_UNIX_MIDI_LIBRARIES "-framework CoreMIDI" "-framework CoreFoundation")
        list(APPEND WINUAE_UNIX_MIDI_QT_DEFINITIONS UAE_UNIX_WITH_COREMIDI=1)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(ALSA QUIET alsa)
        if(ALSA_FOUND)
            set(WINUAE_UNIX_ENABLE_MIDI ON)
            target_compile_definitions(winuae_core PRIVATE WITH_MIDI=1 WINUAE_UNIX_WITH_ALSA_MIDI=1)
            target_include_directories(winuae_core PRIVATE ${ALSA_INCLUDE_DIRS})
            target_compile_options(winuae_core PRIVATE ${ALSA_CFLAGS_OTHER})
            list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/midi.cpp)
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${ALSA_LIBRARIES})
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARY_DIRS ${ALSA_LIBRARY_DIRS})
            list(APPEND WINUAE_UNIX_MIDI_LIBRARIES ${ALSA_LIBRARIES})
            list(APPEND WINUAE_UNIX_MIDI_INCLUDE_DIRS ${ALSA_INCLUDE_DIRS})
            list(APPEND WINUAE_UNIX_MIDI_LIBRARY_DIRS ${ALSA_LIBRARY_DIRS})
            list(APPEND WINUAE_UNIX_MIDI_COMPILE_OPTIONS ${ALSA_CFLAGS_OTHER})
            list(APPEND WINUAE_UNIX_MIDI_QT_DEFINITIONS UAE_UNIX_WITH_ALSA_MIDI=1)
        else()
            message(STATUS "ALSA development package not found; native Unix MIDI disabled")
        endif()
    else()
        message(STATUS "pkg-config not available; native Unix MIDI disabled")
    endif()
endif()

if(WINUAE_UNIX_WITH_MIDIEMU AND WINUAE_UNIX_ENABLE_MIDI)
    if(WINUAE_UNIX_MT32EMU_OK)
        set(WINUAE_UNIX_ENABLE_MIDIEMU ON)
        target_compile_definitions(winuae_core PRIVATE WITH_MIDIEMU=1)
        target_include_directories(winuae_core PRIVATE ${MT32EMU_INCLUDE_DIRS})
        target_compile_options(winuae_core PRIVATE ${MT32EMU_CFLAGS_OTHER})
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES midiemu.cpp)
        if(APPLE)
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES "${WINUAE_UNIX_MT32EMU_LIBRARY}")
        else()
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${MT32EMU_LIBRARIES})
        endif()
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARY_DIRS ${MT32EMU_LIBRARY_DIRS})
    elseif(NOT WINUAE_UNIX_MT32EMU_FOUND)
        message(STATUS "mt32emu development package not found; Munt MIDI emulation disabled")
    endif()
elseif(WINUAE_UNIX_WITH_MIDIEMU AND NOT WINUAE_UNIX_ENABLE_MIDI)
    message(STATUS "Native Unix MIDI disabled; Munt MIDI emulation disabled")
endif()

if(WINUAE_UNIX_WITH_CPUBOARD)
    set(WINUAE_UNIX_ENABLE_CPUBOARD ON)
    target_compile_definitions(winuae_core PRIVATE WITH_CPUBOARD=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES cpuboard.cpp)
endif()

if(WINUAE_UNIX_WITH_PPC_QEMU)
    set(WINUAE_UNIX_ENABLE_PPC ON)
    target_compile_definitions(winuae_core PRIVATE WITH_PPC=1 WITH_QEMU_CPU=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        ppc/ppc.cpp
        ppc/ppcd.cpp
        qemuvga/qemu.cpp
    )
endif()

if(WINUAE_UNIX_WITH_DSP)
    set(WINUAE_UNIX_ENABLE_DSP ON)
    target_compile_definitions(winuae_core PRIVATE WITH_DSP=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        dsp3210/DSP3210_emulation.cpp
        dsp3210/dsp_glue.cpp
    )
endif()

set(WINUAE_UNIX_ENABLE_SNDBOARD ON)
set(WINUAE_UNIX_ENABLE_PCI ON)
target_compile_definitions(winuae_core PRIVATE
    WITH_SNDBOARD=1
    WITH_TOCCATA=1
    WITH_PCI=1
)
list(APPEND WINUAE_UNIX_EXTRA_SOURCES
    sndboard.cpp
    od-unix/sndboard_host.cpp
    pci.cpp
    qemuvga/es1370.cpp
    qemuvga/ne2000.cpp
    qemuvga/qemuuaeglue.cpp
)

set(WINUAE_UNIX_ENABLE_GFXBOARD ON)
target_compile_definitions(winuae_core PRIVATE GFXBOARD=1)
set(WINUAE_UNIX_ENABLE_PCEM ON)
list(APPEND WINUAE_UNIX_EXTRA_SOURCES
    gfxboard.cpp
    pci.cpp
    mame/a2410.cpp
    mame/tm34010/tms34010.cpp
    ${WINUAE_UNIX_PCEM_COMMON_SOURCES}
    ${WINUAE_UNIX_PCEM_VIDEO_SOURCES}
)

set(WINUAE_UNIX_ENABLE_X86 ON)
set(WINUAE_UNIX_ENABLE_PCEM ON)
target_compile_definitions(winuae_core PRIVATE WITH_X86=1)
list(APPEND WINUAE_UNIX_EXTRA_SOURCES
    x86.cpp
    ${WINUAE_UNIX_PCEM_COMMON_SOURCES}
    ${WINUAE_UNIX_PCEM_X86_SOURCES}
)

set(WINUAE_UNIX_ENABLE_DRACO ON)
set(WINUAE_UNIX_ENABLE_PCEM ON)
target_compile_definitions(winuae_core PRIVATE WITH_DRACO=1)
list(APPEND WINUAE_UNIX_EXTRA_SOURCES
    draco.cpp
    ${WINUAE_UNIX_PCEM_COMMON_SOURCES}
    ${WINUAE_UNIX_PCEM_DRACO_SOURCES}
)

if(WINUAE_UNIX_ENABLE_PCEM)
    target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_PCEM=1)
    target_compile_options(winuae_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-include>
        $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/od-unix/pcem_wincompat.h>
    )
endif()

if(WINUAE_UNIX_ENABLE_PPC OR WINUAE_UNIX_ENABLE_GFXBOARD OR WINUAE_UNIX_ENABLE_X86 OR WINUAE_UNIX_ENABLE_DRACO)
    target_compile_definitions(winuae_core PRIVATE UAE=)
endif()

if(WINUAE_UNIX_WITH_AVIOUTPUT)
    set(WINUAE_UNIX_ENABLE_AVIOUTPUT ON)
    target_compile_definitions(winuae_core PRIVATE AVIOUTPUT=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/avioutput.cpp)
endif()

if(WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE AND WINUAE_UNIX_WITH_SDL3)
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        set(WINUAE_UNIX_ENABLE_SHADER_PIPELINE ON)
        target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE=1)
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES OpenGL::GL)
    else()
        message(STATUS "OpenGL development package not found; Unix shader pipeline disabled")
    endif()
endif()

if(WINUAE_UNIX_WITH_JIT)
    if(WINUAE_UNIX_JIT_TARGET_SUPPORTED)
        set(WINUAE_UNIX_ENABLE_JIT ON)
        target_compile_definitions(winuae_core PRIVATE
            JIT=1
            USE_JIT_FPU=1
            NOFLAGS_SUPPORT_GENCOMP=1
            SAHF_SETO_PROFITABLE=1
        )
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES
            jit/compemu.cpp
            jit/compemu_fpp.cpp
            jit/compemu_support.cpp
            jit/compstbl.cpp
        )
    else()
        message(STATUS "JIT is currently enabled only for Unix ARM64 and x86_64 builds; disabling JIT for ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

if(WINUAE_UNIX_WITH_SLIRP)
    target_compile_definitions(winuae_core PRIVATE
        A2065=1
        WITH_SLIRP=1
        WITH_BUILTIN_SLIRP=1
    )
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        a2065.cpp
        slirp_uae.cpp
        slirp/bootp.cpp
        slirp/cksum.cpp
        slirp/if.cpp
        slirp/ip_icmp.cpp
        slirp/ip_input.cpp
        slirp/ip_output.cpp
        slirp/mbuf.cpp
        slirp/misc.cpp
        slirp/sbuf.cpp
        slirp/slirp.cpp
        slirp/slirpdebug.cpp
        slirp/socket.cpp
        slirp/tcp_input.cpp
        slirp/tcp_output.cpp
        slirp/tcp_subr.cpp
        slirp/tcp_timer.cpp
        slirp/tftp.cpp
        slirp/udp.cpp
    )
endif()

if(WINUAE_UNIX_WITH_SANA2)
    if(WINUAE_UNIX_WITH_SLIRP)
        set(WINUAE_UNIX_ENABLE_SANA2 ON)
        target_compile_definitions(winuae_core PRIVATE SANA2=1)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES sana2.cpp)
    else()
        message(STATUS "uaenet.device requires the Unix Ethernet backend; disabling WINUAE_UNIX_WITH_SANA2")
    endif()
endif()

if(WINUAE_UNIX_WITH_UAENET_PCAP)
    set(WINUAE_UNIX_PCAP_FOUND OFF)
    if(PkgConfig_FOUND)
        pkg_check_modules(PCAP QUIET libpcap)
        if(PCAP_FOUND)
            set(WINUAE_UNIX_PCAP_FOUND ON)
            target_include_directories(winuae_core PRIVATE ${PCAP_INCLUDE_DIRS})
            target_compile_options(winuae_core PRIVATE ${PCAP_CFLAGS_OTHER})
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${PCAP_LIBRARIES})
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARY_DIRS ${PCAP_LIBRARY_DIRS})
        endif()
    endif()
    if(NOT WINUAE_UNIX_PCAP_FOUND)
        find_path(WINUAE_UNIX_PCAP_INCLUDE_DIR NAMES pcap/pcap.h pcap.h)
        find_library(WINUAE_UNIX_PCAP_LIBRARY NAMES pcap)
        if(WINUAE_UNIX_PCAP_INCLUDE_DIR AND WINUAE_UNIX_PCAP_LIBRARY)
            set(WINUAE_UNIX_PCAP_FOUND ON)
            target_include_directories(winuae_core PRIVATE "${WINUAE_UNIX_PCAP_INCLUDE_DIR}")
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES "${WINUAE_UNIX_PCAP_LIBRARY}")
        endif()
    endif()
    if(WINUAE_UNIX_PCAP_FOUND)
        set(WINUAE_UNIX_ENABLE_UAENET_PCAP ON)
        target_compile_definitions(winuae_core PRIVATE WITH_UAENET_PCAP=1)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/uaenet_unix.cpp)
    else()
        message(STATUS "libpcap was not found; disabling native Unix packet networking")
    endif()
endif()

if(WINUAE_UNIX_WITH_BSDSOCKET)
    set(WINUAE_UNIX_ENABLE_BSDSOCKET ON)
    target_compile_definitions(winuae_core PRIVATE BSDSOCKET=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/bsdsock_unix.cpp)
endif()

if(WINUAE_UNIX_WITH_UAESCSI)
    set(WINUAE_UNIX_ENABLE_UAESCSI ON)
    target_compile_definitions(winuae_core PRIVATE SCSIEMU=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES scsiemul.cpp)
endif()

if(WINUAE_UNIX_WITH_NATIVE_CD)
    if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(WINUAE_UNIX_ENABLE_NATIVE_CD ON)
        target_compile_definitions(winuae_core PRIVATE WITH_SCSI_IOCTL=1)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES
            od-unix/blkdev_unix_ioctl.cpp
            od-unix/cda_play.cpp
        )
        if(APPLE)
            list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES
                "-framework CoreFoundation"
                "-framework DiskArbitration"
                "-framework IOKit"
            )
        endif()
    else()
        message(STATUS "Native Unix CD/DVD access is implemented for macOS and Linux only")
    endif()
endif()

if(WINUAE_UNIX_WITH_NATIVE_SCSI)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(WINUAE_UNIX_ENABLE_NATIVE_SCSI ON)
        target_compile_definitions(winuae_core PRIVATE WITH_SCSI_SPTI=1)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/blkdev_unix_sg.cpp)
    elseif(APPLE)
        set(WINUAE_UNIX_ENABLE_NATIVE_SCSI ON)
        target_compile_definitions(winuae_core PRIVATE WITH_SCSI_SPTI=1)
        list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/blkdev_unix_scsi_macos.cpp)
        list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES
            "-framework CoreFoundation"
            "-framework IOKit"
        )
    else()
        message(STATUS "Native Unix SCSI/tape passthrough is implemented for macOS SCSITaskLib and Linux SG_IO only")
    endif()
endif()

if(WINUAE_UNIX_WITH_UAESERIAL)
    set(WINUAE_UNIX_ENABLE_UAESERIAL ON)
    target_compile_definitions(winuae_core PRIVATE UAESERIAL=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        uaeserial.cpp
        od-unix/uaeserial_host.cpp
    )
endif()

target_compile_definitions(winuae_core PRIVATE NCR=1 NCR9X=1)
set_source_files_properties(ncr_scsi.cpp ncr9x_scsi.cpp PROPERTIES COMPILE_DEFINITIONS WITH_QEMU_CPU=1)
list(APPEND WINUAE_UNIX_EXTRA_SOURCES
    ncr_scsi.cpp
    ncr9x_scsi.cpp
    qemuvga/esp.cpp
    qemuvga/lsi53c710.cpp
    qemuvga/lsi53c895a.cpp
    qemuvga/qemuuaeglue.cpp
)

if(WINUAE_UNIX_WITH_PROWIZARD)
    target_compile_definitions(winuae_core PRIVATE PROWIZARD=1)
    target_include_directories(winuae_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/prowizard/include"
    )
    set(WINUAE_UNIX_PROWIZARD_C_SOURCES
        prowizard/misc/misc.c
        prowizard/prowiz.c
        prowizard/misc/testbag.c
        prowizard/rippers/AC1D_packer.c
        prowizard/rippers/AMF.c
        prowizard/rippers/AMOS-MusicBank.c
        prowizard/rippers/AMOS-PowerPackerBank.c
        prowizard/rippers/BP-Soundmonitor.c
        prowizard/rippers/BSI-FutureComposer.c
        prowizard/rippers/Bytekiller.c
        prowizard/rippers/ChipTracker.c
        prowizard/rippers/Crunchmania-Address.c
        prowizard/rippers/Crunchmania-Simple.c
        prowizard/rippers/datacrunchers.c
        prowizard/rippers/DefjamCruncher32-pro.c
        prowizard/rippers/DigiBooster17.c
        prowizard/rippers/DigitalIllusion.c
        prowizard/rippers/DoubleAction.c
        prowizard/rippers/DM1.c
        prowizard/rippers/Dragpack100.c
        prowizard/rippers/Dragpack252.c
        prowizard/rippers/EurekaPacker.c
        prowizard/rippers/FastTracker2.c
        prowizard/rippers/FC-M-Packer.c
        prowizard/rippers/FuchsTracker.c
        prowizard/rippers/FutureComposer13.c
        prowizard/rippers/FutureComposer14.c
        prowizard/rippers/FuzzacPacker.c
        prowizard/rippers/GMC.c
        prowizard/rippers/GNUPacker12.c
        prowizard/rippers/GnuPlayer.c
        prowizard/rippers/GPMO.c
        prowizard/rippers/HCD.c
        prowizard/rippers/HeatseekerMC10.c
        prowizard/rippers/HighPresCr.c
        prowizard/rippers/HornetPacker.c
        prowizard/rippers/HQC2.c
        prowizard/rippers/IFF.c
        prowizard/rippers/IT.c
        prowizard/rippers/JamCracker.c
        prowizard/rippers/KefrensSoundMachine.c
        prowizard/rippers/MasterCruncher30-Address.c
        prowizard/rippers/MaxPacker12.c
        prowizard/rippers/MED-Octamed.c
        prowizard/rippers/MegaCruncher-Object.c
        prowizard/rippers/MegaCruncher.c
        prowizard/rippers/MOD-compatible.c
        prowizard/rippers/ModuleProtector.c
        prowizard/rippers/Mugician.c
        prowizard/rippers/Newtron.c
        prowizard/rippers/NewtronOld.c
        prowizard/rippers/NoiseFromHeaven.c
        prowizard/rippers/NoisePacker1.c
        prowizard/rippers/NoisePacker2.c
        prowizard/rippers/NoiseRunner.c
        prowizard/rippers/NovoTrade.c
        prowizard/rippers/NP3.c
        prowizard/rippers/PhaPacker.c
        prowizard/rippers/Perfsong.c
        prowizard/rippers/PolkaPacker.c
        prowizard/rippers/PowerMusic.c
        prowizard/rippers/PowerPacker23.c
        prowizard/rippers/PowerPacker30.c
        prowizard/rippers/PowerPacker40-library.c
        prowizard/rippers/PowerPacker40.c
        prowizard/rippers/Promizer01.c
        prowizard/rippers/Promizer10c.c
        prowizard/rippers/Promizer18a.c
        prowizard/rippers/Promizer20.c
        prowizard/rippers/Promizer40.c
        prowizard/rippers/ProPacker10.c
        prowizard/rippers/ProPacker21.c
        prowizard/rippers/ProPacker30.c
        prowizard/rippers/Prorunner10.c
        prowizard/rippers/Prorunner20.c
        prowizard/rippers/QuadraComposer.c
        prowizard/rippers/RelokIt10.c
        prowizard/rippers/S3M.c
        prowizard/rippers/SA.c
        prowizard/rippers/SGT-Packer.c
        prowizard/rippers/skizzo.c
        prowizard/rippers/SlamPacker.c
        prowizard/rippers/SkytPacker.c
        prowizard/rippers/SoundFX.c
        prowizard/rippers/SoundTracker.c
        prowizard/rippers/SoundTracker26.c
        prowizard/rippers/SpikeCruncher.c
        prowizard/rippers/StartrekkerPack.c
        prowizard/rippers/StimPacker.c
        prowizard/rippers/StoneArtsPlayer.c
        prowizard/rippers/StoneCracker270.c
        prowizard/rippers/StoneCracker299.c
        prowizard/rippers/StoneCracker299b.c
        prowizard/rippers/StoneCracker299d.c
        prowizard/rippers/StoneCracker300.c
        prowizard/rippers/StoneCracker310.c
        prowizard/rippers/SuperCruncher27.c
        prowizard/rippers/SyncroPacker46.c
        prowizard/rippers/THX.c
        prowizard/rippers/TetraPack102.c
        prowizard/rippers/TetraPack21.c
        prowizard/rippers/TetraPack22.c
        prowizard/rippers/TheDarkDemon.c
        prowizard/rippers/ThePlayer22a.c
        prowizard/rippers/ThePlayer30a.c
        prowizard/rippers/ThePlayer40.c
        prowizard/rippers/ThePlayer41a.c
        prowizard/rippers/ThePlayer50a.c
        prowizard/rippers/ThePlayer60a.c
        prowizard/rippers/ThePlayer61a.c
        prowizard/rippers/TimeCruncher17.c
        prowizard/rippers/TitanicsPlayer.c
        prowizard/rippers/TMK.c
        prowizard/rippers/TNMCruncher.c
        prowizard/rippers/TrackerPacker1.c
        prowizard/rippers/TrackerPacker2.c
        prowizard/rippers/TrackerPacker3.c
        prowizard/rippers/TryItCruncher101.c
        prowizard/rippers/TurboSqueezer61.c
        prowizard/rippers/UnicTracker.c
        prowizard/rippers/UnicTracker2.c
        prowizard/rippers/WantonPacker.c
        prowizard/rippers/XannPlayer.c
        prowizard/rippers/bnr.c
        prowizard/rippers/mosh.c
        prowizard/rippers/okta.c
        prowizard/rippers/pmd3.c
        prowizard/rippers/sidmon1.c
        prowizard/rippers/sidmon2.c
        prowizard/rippers/struggle.c
        prowizard/rippers/ZenPacker.c
    )
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES
        moduleripper.cpp
        ${WINUAE_UNIX_PROWIZARD_C_SOURCES}
    )
    set_source_files_properties(${WINUAE_UNIX_PROWIZARD_C_SOURCES}
        PROPERTIES LANGUAGE CXX
    )
endif()

if(WINUAE_UNIX_WITH_SDL3)
    find_package(SDL3 CONFIG QUIET)
    if(TARGET SDL3::SDL3)
        set(WINUAE_UNIX_SDL3_TARGET SDL3::SDL3)
    elseif(TARGET SDL3::SDL3-shared)
        set(WINUAE_UNIX_SDL3_TARGET SDL3::SDL3-shared)
    elseif(TARGET SDL3-static)
        set(WINUAE_UNIX_SDL3_TARGET SDL3-static)
    elseif(TARGET SDL3)
        set(WINUAE_UNIX_SDL3_TARGET SDL3)
    endif()
    if(WINUAE_UNIX_SDL3_TARGET AND APPLE)
        winuae_unix_imported_location("${WINUAE_UNIX_SDL3_TARGET}" WINUAE_UNIX_SDL3_IMPORTED_LIBRARY)
        if(WINUAE_UNIX_SDL3_IMPORTED_LIBRARY)
            execute_process(
                COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-check-deployment-target.sh" "${WINUAE_UNIX_SDL3_IMPORTED_LIBRARY}" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                RESULT_VARIABLE WINUAE_UNIX_SDL3_DEPLOYMENT_RESULT
                OUTPUT_VARIABLE WINUAE_UNIX_SDL3_DEPLOYMENT_OUTPUT
                ERROR_VARIABLE WINUAE_UNIX_SDL3_DEPLOYMENT_ERROR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE
            )
            if(NOT WINUAE_UNIX_SDL3_DEPLOYMENT_RESULT EQUAL 0)
                message(STATUS "SDL3 is newer than CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}; ignoring package target. ${WINUAE_UNIX_SDL3_DEPLOYMENT_ERROR}")
                unset(WINUAE_UNIX_SDL3_TARGET)
                set(SDL3_FOUND OFF)
            endif()
        endif()
    endif()
    if(NOT WINUAE_UNIX_SDL3_TARGET)
        find_path(WINUAE_UNIX_SDL3_INCLUDE_DIR NAMES SDL3/SDL.h)
        find_library(WINUAE_UNIX_SDL3_FALLBACK_LIBRARY NAMES SDL3 SDL3.0)
        if(WINUAE_UNIX_SDL3_INCLUDE_DIR AND WINUAE_UNIX_SDL3_FALLBACK_LIBRARY)
            set(WINUAE_UNIX_SDL3_FALLBACK_OK ON)
            if(APPLE)
                execute_process(
                    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-check-deployment-target.sh" "${WINUAE_UNIX_SDL3_FALLBACK_LIBRARY}" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                    RESULT_VARIABLE WINUAE_UNIX_SDL3_FALLBACK_DEPLOYMENT_RESULT
                    OUTPUT_VARIABLE WINUAE_UNIX_SDL3_FALLBACK_DEPLOYMENT_OUTPUT
                    ERROR_VARIABLE WINUAE_UNIX_SDL3_FALLBACK_DEPLOYMENT_ERROR
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE
                )
                if(NOT WINUAE_UNIX_SDL3_FALLBACK_DEPLOYMENT_RESULT EQUAL 0)
                    message(STATUS "SDL3 library is newer than CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}; SDL3 disabled. ${WINUAE_UNIX_SDL3_FALLBACK_DEPLOYMENT_ERROR}")
                    set(WINUAE_UNIX_SDL3_FALLBACK_OK OFF)
                endif()
            endif()
            if(WINUAE_UNIX_SDL3_FALLBACK_OK)
                add_library(WINUAE::SDL3 UNKNOWN IMPORTED)
                set_target_properties(WINUAE::SDL3 PROPERTIES
                    IMPORTED_LOCATION "${WINUAE_UNIX_SDL3_FALLBACK_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${WINUAE_UNIX_SDL3_INCLUDE_DIR}"
                )
                set(WINUAE_UNIX_SDL3_TARGET WINUAE::SDL3)
                message(STATUS "Using SDL3 library: ${WINUAE_UNIX_SDL3_FALLBACK_LIBRARY}")
            endif()
        endif()
    endif()
    if(NOT WINUAE_UNIX_SDL3_TARGET AND PkgConfig_FOUND AND NOT APPLE)
        pkg_check_modules(SDL3 QUIET sdl3)
    endif()
endif()

if(WINUAE_UNIX_SDL3_TARGET)
    target_compile_definitions(winuae_core PRIVATE UAE_UNIX_WITH_SDL3=1)
    target_link_libraries(winuae_core PRIVATE ${WINUAE_UNIX_SDL3_TARGET})
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/video_sdl.cpp)
    list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${WINUAE_UNIX_SDL3_TARGET})
elseif(SDL3_FOUND)
    target_compile_definitions(winuae_core PRIVATE UAE_UNIX_WITH_SDL3=1)
    target_include_directories(winuae_core PRIVATE ${SDL3_INCLUDE_DIRS})
    target_compile_options(winuae_core PRIVATE ${SDL3_CFLAGS_OTHER})
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/video_sdl.cpp)
    list(APPEND WINUAE_UNIX_EXTRA_LIBRARIES ${SDL3_LIBRARIES})
    list(APPEND WINUAE_UNIX_EXTRA_LIBRARY_DIRS ${SDL3_LIBRARY_DIRS})
else()
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/video_null.cpp)
endif()

if(WINUAE_UNIX_WITH_SAMPLER AND (WINUAE_UNIX_SDL3_TARGET OR SDL3_FOUND))
    set(WINUAE_UNIX_ENABLE_SAMPLER ON)
    target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_SAMPLER=1)
    list(APPEND WINUAE_UNIX_EXTRA_SOURCES od-unix/sampler_sdl.cpp)
endif()

set(WINUAE_CORE_SOURCES
    a2091.cpp
    akiko.cpp
    amax.cpp
    ar.cpp
    arcadia.cpp
    aros.rom.cpp
    audio.cpp
    autoconf.cpp
    blitfunc.cpp
    blitops.cpp
    blittable.cpp
    blitter.cpp
    blkdev.cpp
    blkdev_cdimage.cpp
    bsdsocket.cpp
    calc.cpp
    cd32_fmv.cpp
    cd32_fmv_genlock.cpp
    cdrom.cpp
    cdtv.cpp
    cdtvcr.cpp
    cfgfile.cpp
    cia.cpp
    consolehook.cpp
    cpudefs.cpp
    cpustbl.cpp
    cpuemu_0.cpp
    cpuemu_11.cpp
    cpuemu_13.cpp
    cpuemu_20.cpp
    cpuemu_21.cpp
    cpuemu_22.cpp
    cpuemu_23.cpp
    cpuemu_24.cpp
    cpuemu_31.cpp
    cpuemu_32.cpp
    cpuemu_33.cpp
    cpuemu_34.cpp
    cpuemu_35.cpp
    cpuemu_40.cpp
    cpuemu_50.cpp
    cpummu.cpp
    cpummu30.cpp
    crc32.cpp
    custom.cpp
    debug.cpp
    debugmem.cpp
    def_icons.cpp
    devices.cpp
    disasm.cpp
    disk.cpp
    diskutil.cpp
    dlopen.cpp
    dongle.cpp
    drawing.cpp
    driveclick.cpp
    enforcer.cpp
    ethernet.cpp
    events.cpp
    expansion.cpp
    fdi2raw.cpp
    filesys.cpp
    flashrom.cpp
    fpp.cpp
    fpp_native.cpp
    fpp_softfloat.cpp
    framebufferboards.cpp
    fsdb.cpp
    fsdb_unix.cpp
    gayle.cpp
    gfxlib.cpp
    gfxutil.cpp
    hardfile.cpp
    hrtmon.rom.cpp
    ide.cpp
    idecontrollers.cpp
    identify.cpp
    ini.cpp
    inputdevice.cpp
    inputrecord.cpp
    isofs.cpp
    kbmcu/8048/co8048.cpp
    kbmcu/keyboard_mcu_6500_1.cpp
    kbmcu/keyboard_mcu_6805.cpp
    kbmcu/keyboard_mcu_d8039hlc.cpp
    kbmcu/m6805/m68emu.cpp
    kbmcu/m6805/m68_ops.cpp
    keybuf.cpp
    logging.cpp
    main.cpp
    memory.cpp
    missing.cpp
    mos6502.cpp
    native2amiga.cpp
    newcpu.cpp
    newcpu_common.cpp
    readcpu.cpp
    rommgr.cpp
    rtc.cpp
    savestate.cpp
    scp.cpp
    scsi.cpp
    scsitape.cpp
    statusline.cpp
    test_card.cpp
    tinyxml2.cpp
    traps.cpp
    uaeexe.cpp
    uaelib.cpp
    uaeresource.cpp
    vm.cpp
    writelog.cpp
    zfile.cpp
    archivers/mp2/kjmp2.cpp
    softfloat/softfloat.cpp
    softfloat/softfloat_decimal.cpp
    softfloat/softfloat_fpsp.cpp
    od-unix/charset.cpp
    od-unix/clipboard.cpp
    od-unix/config.cpp
    od-unix/driveclick.cpp
    od-unix/dlopen.cpp
    od-unix/filesys_host.cpp
    od-unix/graphics.cpp
    od-unix/gui.cpp
    od-unix/hardfile_host.cpp
    od-unix/input.cpp
    od-unix/logging.cpp
    od-unix/mman.cpp
    od-unix/path_expand.cpp
    od-unix/parallel.cpp
    od-unix/registry.cpp
    od-unix/romscan.cpp
    od-unix/rtg.cpp
    od-unix/screenshot.cpp
    od-unix/serial.cpp
    od-unix/sound.cpp
    od-unix/stubs.cpp
    od-unix/support.cpp
    od-unix/threading.cpp
    od-unix/time.cpp
)

list(REMOVE_DUPLICATES WINUAE_UNIX_EXTRA_SOURCES)
list(APPEND WINUAE_CORE_SOURCES ${WINUAE_UNIX_EXTRA_SOURCES})
target_sources(winuae_core PRIVATE ${WINUAE_CORE_SOURCES})

if(WINUAE_UNIX_BUILD_EXECUTABLE)
    add_executable(winuae_unix $<TARGET_OBJECTS:winuae_core>)
    # Keep the developer-facing target name stable; the produced binary is
    # named winuae to match the installed name, the .desktop files, the
    # WinUAE.app bundle, and winuae.exe on Windows.
    set_target_properties(winuae_unix PROPERTIES OUTPUT_NAME winuae)
    if(WINUAE_UNIX_EXTRA_LIBRARY_DIRS)
        target_link_directories(winuae_unix PRIVATE ${WINUAE_UNIX_EXTRA_LIBRARY_DIRS})
    endif()
    target_link_libraries(winuae_unix PRIVATE pthread ${CMAKE_DL_LIBS} ZLIB::ZLIB ${WINUAE_UNIX_EXTRA_LIBRARIES})

    function(add_winuae_unix_smoke_target target script)
        add_custom_target(${target}
            COMMAND "${CMAKE_COMMAND}" -E env
                "WINUAE_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/${script}"
            DEPENDS winuae_unix
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Running ${script}"
            VERBATIM
        )
    endfunction()

    add_winuae_unix_smoke_target(winuae_unix_smoke_a1200 unix-smoke-a1200.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_a2065 unix-smoke-a2065.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_sana2 unix-smoke-sana2.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_driveclick unix-smoke-driveclick.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_sound_boards unix-smoke-sound-boards.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_rtg_z3 unix-smoke-rtg-z3.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_uaegfx unix-smoke-uaegfx.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest unix-smoke-p96-guest.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_800x600 unix-smoke-p96-guest-800x600.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_16bit_modes unix-smoke-p96-guest-16bit-modes.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_15bit_open unix-smoke-p96-guest-15bit-open.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_16bit_open unix-smoke-p96-guest-16bit-open.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_24bit_open unix-smoke-p96-guest-24bit-open.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_32bit_open unix-smoke-p96-guest-32bit-open.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_draw_8bit unix-smoke-p96-guest-draw-8bit.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_draw_16bit unix-smoke-p96-guest-draw-16bit.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_p96_guest_z3 unix-smoke-p96-guest-z3.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_a4091_hdf unix-smoke-a4091-hdf.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_alfapower_hdf unix-smoke-alfapower-hdf.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_ripple_hdf unix-smoke-ripple-hdf.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_serial_tcp unix-smoke-serial-tcp.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_path_config unix-smoke-path-config.sh)
    add_winuae_unix_smoke_target(winuae_unix_smoke_config_roundtrip unix-smoke-config-roundtrip.sh)
    if(WINUAE_UNIX_ENABLE_JIT)
        add_winuae_unix_smoke_target(winuae_unix_smoke_jit unix-smoke-jit.sh)
    endif()
    add_custom_target(winuae_unix_smoke_basic
        DEPENDS winuae_unix_smoke_a1200 winuae_unix_smoke_path_config
            winuae_unix_smoke_config_roundtrip
    )
endif()

add_executable(winuae_unix_path_expand_test EXCLUDE_FROM_ALL
    od-unix/path_expand.cpp
    od-unix/path_expand_test.cpp
)
target_include_directories(winuae_unix_path_expand_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/od-unix"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
winuae_unix_register_test(winuae_unix_path_expand_test)

add_executable(winuae_unix_registry_test EXCLUDE_FROM_ALL
    od-unix/registry.cpp
    od-unix/registry_test.cpp
    ini.cpp
    missing.cpp
)
target_include_directories(winuae_unix_registry_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/od-unix"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
winuae_unix_register_test(winuae_unix_registry_test)

add_executable(winuae_unix_threading_test EXCLUDE_FROM_ALL
    od-unix/threading.cpp
    od-unix/threading_test.cpp
)
target_include_directories(winuae_unix_threading_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/od-unix"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(winuae_unix_threading_test PRIVATE pthread)
winuae_unix_register_test(winuae_unix_threading_test)

if(WINUAE_UNIX_WITH_INTEGRATED_QT_UI AND NOT WINUAE_UNIX_WITH_QT_UI)
    message(STATUS
        "WINUAE_UNIX_WITH_INTEGRATED_QT_UI requires "
        "WINUAE_UNIX_WITH_QT_UI; disabling integrated Qt UI"
    )
    set(WINUAE_UNIX_WITH_INTEGRATED_QT_UI OFF)
endif()

if(WINUAE_UNIX_WITH_QT_UI)
    if(APPLE)
        winuae_unix_macos_find_qt_frameworks(WINUAE_UNIX_QT_WIDGETS_TARGET)
    endif()
    if(NOT WINUAE_UNIX_QT_WIDGETS_TARGET)
        find_package(Qt6 COMPONENTS Widgets QUIET)
        if(Qt6_FOUND)
            set(WINUAE_UNIX_QT_WIDGETS_TARGET Qt6::Widgets)
        endif()
    endif()
    if(NOT WINUAE_UNIX_QT_WIDGETS_TARGET)
        find_package(Qt5 COMPONENTS Widgets QUIET)
        if(Qt5_FOUND)
            set(WINUAE_UNIX_QT_WIDGETS_TARGET Qt5::Widgets)
        endif()
    endif()

    if(WINUAE_UNIX_QT_WIDGETS_TARGET)
        add_library(winuae_unix_qt_frontend STATIC
            od-unix/qt/config.cpp
            od-unix/qt/launcher.cpp
            od-unix/qt/launcher_bridge.cpp
            od-unix/qt/mount_config.cpp
            od-unix/qt/path_utils.cpp
            od-unix/qt/prefs_adapter.cpp
        )
        target_include_directories(winuae_unix_qt_frontend PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/od-unix"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        target_compile_definitions(winuae_unix_qt_frontend PRIVATE
            WINUAE_UNIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            WINUAE_UNIX_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/winuae"
            WINUAE_UNIX_INSTALL_DATADIR_RELATIVE="${CMAKE_INSTALL_DATADIR}/winuae"
            WINUAE_UNIX_VERSION_MAJOR=${WINUAE_UNIX_VERSION_MAJOR}
            WINUAE_UNIX_VERSION_MINOR=${WINUAE_UNIX_VERSION_MINOR}
            WINUAE_UNIX_VERSION_REVISION=${WINUAE_UNIX_VERSION_REVISION}
            UAE_UNIX_WITH_SANA2=$<BOOL:${WINUAE_UNIX_ENABLE_SANA2}>
            UAE_UNIX_WITH_UAENET_PCAP=$<BOOL:${WINUAE_UNIX_ENABLE_UAENET_PCAP}>
            UAE_UNIX_WITH_BSDSOCKET=$<BOOL:${WINUAE_UNIX_ENABLE_BSDSOCKET}>
            UAE_UNIX_WITH_UAESCSI=$<BOOL:${WINUAE_UNIX_ENABLE_UAESCSI}>
            UAE_UNIX_WITH_SAMPLER=$<BOOL:${WINUAE_UNIX_ENABLE_SAMPLER}>
            UAE_UNIX_WITH_NATIVE_HARDDRIVES=$<BOOL:${WINUAE_UNIX_ENABLE_NATIVE_HARDDRIVES}>
            UAE_UNIX_WITH_NATIVE_SCSI=$<BOOL:${WINUAE_UNIX_ENABLE_NATIVE_SCSI}>
            UAE_UNIX_WITH_MIDI=$<BOOL:${WINUAE_UNIX_ENABLE_MIDI}>
            UAE_UNIX_WITH_DSP=$<BOOL:${WINUAE_UNIX_ENABLE_DSP}>
            UAE_UNIX_WITH_SNDBOARD=$<BOOL:${WINUAE_UNIX_ENABLE_SNDBOARD}>
            UAE_UNIX_WITH_GFXBOARD=$<BOOL:${WINUAE_UNIX_ENABLE_GFXBOARD}>
            UAE_UNIX_WITH_X86=$<BOOL:${WINUAE_UNIX_ENABLE_X86}>
            UAE_UNIX_WITH_DRACO=$<BOOL:${WINUAE_UNIX_ENABLE_DRACO}>
            UAE_UNIX_WITH_JIT=$<BOOL:${WINUAE_UNIX_ENABLE_JIT}>
            UAE_UNIX_WITH_AVIOUTPUT=$<BOOL:${WINUAE_UNIX_ENABLE_AVIOUTPUT}>
            UAE_UNIX_WITH_SHADER_PIPELINE=$<BOOL:${WINUAE_UNIX_ENABLE_SHADER_PIPELINE}>
            ${WINUAE_UNIX_MIDI_QT_DEFINITIONS}
        )
        if(WINUAE_UNIX_MIDI_INCLUDE_DIRS)
            target_include_directories(winuae_unix_qt_frontend PRIVATE ${WINUAE_UNIX_MIDI_INCLUDE_DIRS})
        endif()
        if(WINUAE_UNIX_MIDI_COMPILE_OPTIONS)
            target_compile_options(winuae_unix_qt_frontend PRIVATE ${WINUAE_UNIX_MIDI_COMPILE_OPTIONS})
        endif()
        target_link_libraries(winuae_unix_qt_frontend PUBLIC ${WINUAE_UNIX_QT_WIDGETS_TARGET})
        if(WINUAE_UNIX_MIDI_LIBRARY_DIRS)
            target_link_directories(winuae_unix_qt_frontend PUBLIC ${WINUAE_UNIX_MIDI_LIBRARY_DIRS})
        endif()
        if(WINUAE_UNIX_MIDI_LIBRARIES)
            target_link_libraries(winuae_unix_qt_frontend PUBLIC ${WINUAE_UNIX_MIDI_LIBRARIES})
        endif()
        if(WINUAE_UNIX_SDL3_TARGET)
            target_compile_definitions(winuae_unix_qt_frontend PRIVATE UAE_UNIX_WITH_SDL3=1)
            target_link_libraries(winuae_unix_qt_frontend PUBLIC ${WINUAE_UNIX_SDL3_TARGET})
        elseif(SDL3_FOUND)
            target_compile_definitions(winuae_unix_qt_frontend PRIVATE UAE_UNIX_WITH_SDL3=1)
            target_include_directories(winuae_unix_qt_frontend PRIVATE ${SDL3_INCLUDE_DIRS})
            target_compile_options(winuae_unix_qt_frontend PRIVATE ${SDL3_CFLAGS_OTHER})
            target_link_libraries(winuae_unix_qt_frontend PUBLIC ${SDL3_LIBRARIES})
            if(SDL3_LIBRARY_DIRS)
                target_link_directories(winuae_unix_qt_frontend PUBLIC ${SDL3_LIBRARY_DIRS})
            endif()
        endif()

        add_executable(winuae_unix_qt_config_test EXCLUDE_FROM_ALL
            od-unix/qt/config.cpp
            od-unix/qt/config_roundtrip_test.cpp
        )
        target_link_libraries(winuae_unix_qt_config_test PRIVATE ${WINUAE_UNIX_QT_WIDGETS_TARGET})
        winuae_unix_register_test(winuae_unix_qt_config_test)

        add_executable(winuae_unix_qt_mount_config_test EXCLUDE_FROM_ALL
            od-unix/qt/mount_config.cpp
            od-unix/qt/mount_config_test.cpp
        )
        target_link_libraries(winuae_unix_qt_mount_config_test PRIVATE ${WINUAE_UNIX_QT_WIDGETS_TARGET})
        winuae_unix_register_test(winuae_unix_qt_mount_config_test)

        add_executable(winuae_unix_qt_prefs_adapter_test EXCLUDE_FROM_ALL
            od-unix/qt/config.cpp
            od-unix/qt/prefs_adapter.cpp
            od-unix/qt/prefs_adapter_test.cpp
        )
        target_include_directories(winuae_unix_qt_prefs_adapter_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/od-unix"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        target_link_libraries(winuae_unix_qt_prefs_adapter_test PRIVATE ${WINUAE_UNIX_QT_WIDGETS_TARGET})
        winuae_unix_register_test(winuae_unix_qt_prefs_adapter_test)

        add_executable(winuae_unix_qt_path_utils_test EXCLUDE_FROM_ALL
            od-unix/qt/path_utils.cpp
            od-unix/qt/path_utils_test.cpp
        )
        target_link_libraries(winuae_unix_qt_path_utils_test PRIVATE ${WINUAE_UNIX_QT_WIDGETS_TARGET})
        winuae_unix_register_test(winuae_unix_qt_path_utils_test)

        if(WINUAE_UNIX_WITH_INTEGRATED_QT_UI)
            target_compile_definitions(winuae_core PRIVATE WINUAE_UNIX_WITH_INTEGRATED_QT_UI=1)
            if(TARGET winuae_unix)
                target_link_libraries(winuae_unix PRIVATE winuae_unix_qt_frontend)
            endif()
        endif()
    else()
        message(STATUS "Qt Widgets not found; skipping Unix Qt targets")
    endif()
endif()

if(WINUAE_UNIX_TEST_TARGETS)
    add_custom_target(winuae_unix_tests
        DEPENDS ${WINUAE_UNIX_TEST_TARGETS}
    )
    if(BUILD_TESTING)
        add_custom_target(winuae_unix_check
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            DEPENDS winuae_unix_tests
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Running Unix host tests"
            VERBATIM
        )
    endif()
endif()

add_custom_target(winuae_unix_linux_package_metadata_check
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/unix-check-linux-package-metadata.sh" "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking Linux install and package metadata"
    VERBATIM
)

if(APPLE)
    add_custom_target(winuae_unix_macos_deps
        COMMAND "${CMAKE_COMMAND}" -E env
            "WINUAE_MACOS_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-build-deps.sh" "${WINUAE_UNIX_MACOS_DEPS_PREFIX}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building private macOS dependencies for deployment target ${CMAKE_OSX_DEPLOYMENT_TARGET}"
        VERBATIM
    )
    add_custom_target(winuae_unix_macos_qemu_deps
        COMMAND "${CMAKE_COMMAND}" -E env
            "WINUAE_MACOS_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-build-qemu-deps.sh" "${WINUAE_QEMU_UAE_DEPS_PREFIX}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building private macOS QEMU-UAE dependencies for deployment target ${CMAKE_OSX_DEPLOYMENT_TARGET}"
        VERBATIM
    )
endif()

if(WINUAE_QEMU_UAE_PLUGIN_FILE)
    get_filename_component(WINUAE_QEMU_UAE_PLUGIN_PATH
        "${WINUAE_QEMU_UAE_PLUGIN_FILE}" ABSOLUTE
        BASE_DIR "${CMAKE_BINARY_DIR}")
else()
    set(WINUAE_QEMU_UAE_PLUGIN_PATH "${CMAKE_BINARY_DIR}/qemu-uae.so")
endif()
if(UNIX
    AND WINUAE_UNIX_WITH_PPC_QEMU
    AND WINUAE_UNIX_BUILD_QEMU_UAE_PLUGIN
    AND EXISTS "${WINUAE_QEMU_UAE_SOURCE_DIR}/configure-qemu-uae")
    add_custom_target(winuae_unix_qemu_uae_plugin
        COMMAND "${CMAKE_COMMAND}" -E env
            "WINUAE_MACOS_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "QEMU_UAE_DEPS_PREFIX=${WINUAE_QEMU_UAE_DEPS_PREFIX}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-qemu-uae.sh"
            "${WINUAE_QEMU_UAE_SOURCE_DIR}"
            "${WINUAE_QEMU_UAE_PLUGIN_PATH}"
        WORKING_DIRECTORY "${WINUAE_QEMU_UAE_SOURCE_DIR}"
        COMMENT "Building QEMU-UAE PPC plugin"
        VERBATIM
    )
endif()

if(WINUAE_UNIX_WITH_PPC_QEMU)
    if(WINUAE_QEMU_UAE_PLUGIN_FILE)
        if(NOT EXISTS "${WINUAE_QEMU_UAE_PLUGIN_PATH}")
            message(FATAL_ERROR
                "WINUAE_QEMU_UAE_PLUGIN_FILE does not exist: "
                "${WINUAE_QEMU_UAE_PLUGIN_PATH}")
        endif()
    endif()
    set(WINUAE_UNIX_QEMU_UAE_PACKAGE_CHECK_DEPS)
    if(TARGET winuae_unix_qemu_uae_plugin)
        list(APPEND WINUAE_UNIX_QEMU_UAE_PACKAGE_CHECK_DEPS
            winuae_unix_qemu_uae_plugin)
    endif()
    add_custom_target(winuae_unix_qemu_uae_package_check
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/require-qemu-uae-plugin.sh"
                "${WINUAE_QEMU_UAE_PLUGIN_PATH}"
        DEPENDS ${WINUAE_UNIX_QEMU_UAE_PACKAGE_CHECK_DEPS}
        COMMENT "Checking packaged QEMU-UAE PPC plugin"
        VERBATIM
    )
endif()

if(TARGET winuae_unix AND NOT APPLE)
    install(PROGRAMS "$<TARGET_FILE:winuae_unix>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(FILES README_unix.md
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    )
    install(DIRECTORY od-win32/resources/
        DESTINATION "${CMAKE_INSTALL_DATADIR}/winuae/od-win32/resources"
        PATTERN "*.rc" EXCLUDE
        PATTERN "*.manifest" EXCLUDE
        PATTERN "resource.h" EXCLUDE
    )
    install(DIRECTORY od-unix/share/filter-presets/
        DESTINATION "${CMAKE_INSTALL_DATADIR}/winuae/od-unix/share/filter-presets"
    )
    if(WINUAE_UNIX_WITH_PPC_QEMU)
        install(FILES "${WINUAE_QEMU_UAE_PLUGIN_PATH}"
            DESTINATION "${WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE}"
        )
    endif()
    if(NOT APPLE)
        install(FILES od-unix/share/applications/net.winuae.WinUAE.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        )
        install(FILES od-unix/share/mime/packages/net.winuae.WinUAE.xml
            DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages"
        )
        install(FILES od-win32/resources/amiga.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
            RENAME winuae.png
        )
        install(FILES od-win32/resources/amiga.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/mimetypes"
            RENAME application-x-winuae-config.png
        )
    endif()
endif()

if(APPLE AND TARGET winuae_unix)
    set(WINUAE_UNIX_MACOS_APP_DEPS winuae_unix)
    if(TARGET winuae_unix_qemu_uae_plugin)
        list(APPEND WINUAE_UNIX_MACOS_APP_DEPS winuae_unix_qemu_uae_plugin)
    endif()
    if(TARGET winuae_unix_qemu_uae_package_check)
        list(APPEND WINUAE_UNIX_MACOS_APP_DEPS
            winuae_unix_qemu_uae_package_check)
    endif()
    add_custom_target(winuae_unix_macos_app
        COMMAND "${CMAKE_COMMAND}" -E env
            "WINUAE_QEMU_UAE_PLUGIN=${WINUAE_QEMU_UAE_PLUGIN_PATH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-bundle.sh" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/package"
        DEPENDS ${WINUAE_UNIX_MACOS_APP_DEPS}
        COMMENT "Creating WinUAE.app"
        VERBATIM
    )
    add_custom_target(winuae_unix_macos_dmg
        COMMAND "${CMAKE_COMMAND}" -E env
            "WINUAE_QEMU_UAE_PLUGIN=${WINUAE_QEMU_UAE_PLUGIN_PATH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-dmg.sh" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/package"
        DEPENDS winuae_unix_macos_app
        COMMENT "Creating WinUAE drag-install DMG"
        VERBATIM
    )
    if(TARGET winuae_unix_qt_frontend AND WINUAE_UNIX_WITH_INTEGRATED_QT_UI)
        add_custom_target(winuae_unix_macos_app_smoke
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-smoke-app.sh" "${CMAKE_BINARY_DIR}/package/WinUAE.app"
            DEPENDS winuae_unix_macos_app
            COMMENT "Smoke testing packaged WinUAE.app Qt launch"
            VERBATIM
        )
        add_custom_target(winuae_unix_macos_release_check
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos-smoke-app.sh" "${CMAKE_BINARY_DIR}/package/WinUAE.app"
            DEPENDS winuae_unix_macos_dmg
            COMMENT "Release checking packaged WinUAE DMG and Qt app launch"
            VERBATIM
        )
    else()
        add_custom_target(winuae_unix_macos_release_check
            COMMAND "${CMAKE_COMMAND}" -E echo "Qt integrated UI is disabled; release check is limited to DMG verification"
            DEPENDS winuae_unix_macos_dmg
            COMMENT "Release checking packaged WinUAE DMG"
            VERBATIM
        )
    endif()
endif()

if(NOT APPLE)
    set(CPACK_PACKAGE_NAME "WinUAE")
    set(CPACK_PACKAGE_VENDOR "WinUAE")
    set(CPACK_PACKAGE_VERSION "${WINUAE_UNIX_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WinUAE Unix port")
    set(CPACK_PACKAGE_CONTACT "https://www.winuae.net/")
    set(CPACK_PACKAGE_FILE_NAME "WinUAE-${WINUAE_UNIX_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README_unix.md")
    set(CPACK_GENERATOR "TGZ")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_PACKAGE_NAME "winuae")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://www.winuae.net/")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "WinUAE")
        set(CPACK_DEBIAN_PACKAGE_SECTION "emulators")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
            "desktop-file-utils, shared-mime-info, hicolor-icon-theme"
        )
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst"
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm"
        )
    endif()
    include(CPack)
    if(TARGET package AND TARGET winuae_unix)
        add_dependencies(package winuae_unix)
    endif()
    if(TARGET package AND TARGET winuae_unix_qemu_uae_plugin)
        add_dependencies(package winuae_unix_qemu_uae_plugin)
    endif()
    if(TARGET package AND TARGET winuae_unix_qemu_uae_package_check)
        add_dependencies(package winuae_unix_qemu_uae_package_check)
    endif()
endif()
