# Build definition for the 7-Zip Inno Setup plugin and its test client.
#
# Linux (GCC):    cmake -S InnoPlugin -B build/linux && cmake --build build/linux
# MinGW (cross):  cmake -S InnoPlugin -B build/mingw64 -DCMAKE_TOOLCHAIN_FILE=InnoPlugin/cmake/mingw-x86_64.cmake
#                 cmake -S InnoPlugin -B build/mingw32 -DCMAKE_TOOLCHAIN_FILE=InnoPlugin/cmake/mingw-i686.cmake
# MSVC:           cmake -S InnoPlugin -B build/x64 -A x64 && cmake --build build/x64 --config Release
#                 cmake -S InnoPlugin -B build/x86 -A Win32 && cmake --build build/x86 --config Release
#
# Outputs: Inno.64.dll / Inno.32.dll on Windows, Inno.so on Linux, plus the
# test client inno_client (see tests/README.md).

cmake_minimum_required(VERSION 3.20)

project(InnoPlugin LANGUAGES C CXX)

option(INNO_BUILD_CLIENT "Build the test client (tests/client)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT for MSVC (/MT, /MTd).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(SEVENZIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip)

if(MSVC)
  # C4996: deprecated CRT and standard library functions used by the
  # innoextract-derived code (also disabled by the old Visual Studio project).
  add_compile_options(/W4 /WX /wd4996)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

if(WIN32)
  # UNICODE as in upstream 7-Zip (C/Precomp.h); also keeps the Visual Studio
  # generator from defining _MBCS.
  add_compile_definitions(NOMINMAX UNICODE _UNICODE)
else()
  # Same as upstream 7-Zip's GCC build (CPP/7zip/7zip_gcc.mak).
  add_compile_definitions(_REENTRANT _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
endif()

# 7-Zip sources used by both the plugin and the client.
set(SEVENZIP_COMMON_SOURCES
  C/Alloc.c
  CPP/Common/IntToString.cpp
  CPP/Common/MyString.cpp
  CPP/Common/MyVector.cpp
  CPP/Common/NewHandler.cpp
  CPP/Common/StringConvert.cpp
  CPP/Common/StringToInt.cpp
  CPP/Common/UTFConvert.cpp
  CPP/Common/Wildcard.cpp
  CPP/Windows/PropVariant.cpp
  CPP/Windows/TimeUtils.cpp
)
if(NOT WIN32)
  list(APPEND SEVENZIP_COMMON_SOURCES CPP/Common/MyWindows.cpp)
endif()

# Plugin exports (as in upstream CPP/7zip/Archive/7z/makefile), stream helpers
# and the LZMA decoders.
set(SEVENZIP_PLUGIN_SOURCES
  C/CpuArch.c
  C/LzmaDec.c
  C/Lzma2Dec.c
  CPP/7zip/Archive/ArchiveExports.cpp
  CPP/7zip/Archive/DllExports2.cpp
  CPP/7zip/Common/CreateCoder.cpp
  CPP/7zip/Common/FilterCoder.cpp
  CPP/7zip/Common/LimitedStreams.cpp
  CPP/7zip/Common/ProgressUtils.cpp
  CPP/7zip/Common/PropId.cpp
  CPP/7zip/Common/StreamObjects.cpp
  CPP/7zip/Common/StreamUtils.cpp
  CPP/7zip/Compress/CodecExports.cpp
  CPP/7zip/Compress/CopyCoder.cpp
)

# File system helpers used by the client (as in upstream CPP/7zip/UI/Client7z).
set(SEVENZIP_CLIENT_SOURCES
  CPP/7zip/Common/FileStreams.cpp
  CPP/Windows/DLL.cpp
  CPP/Windows/FileDir.cpp
  CPP/Windows/FileFind.cpp
  CPP/Windows/FileIO.cpp
  CPP/Windows/FileName.cpp
  CPP/Windows/PropVariantConv.cpp
)

list(TRANSFORM SEVENZIP_COMMON_SOURCES PREPEND ${SEVENZIP_DIR}/)
list(TRANSFORM SEVENZIP_PLUGIN_SOURCES PREPEND ${SEVENZIP_DIR}/)
list(TRANSFORM SEVENZIP_CLIENT_SOURCES PREPEND ${SEVENZIP_DIR}/)

set(INNO_HANDLER_SOURCES
  src/InnoHandler.cpp
  src/InnoIn.cpp
  src/InnoInterop.cpp
  src/InnoItem.cpp
  src/InnoRegister.cpp
  src/InnoUtils.cpp
)

set(INNO_FORMAT_SOURCES
  src/inno/minihead.cpp
  src/inno/minihead_iostreams.cpp
  src/inno/crypto/adler32.cpp
  src/inno/crypto/arc4.cpp
  src/inno/crypto/checksum.cpp
  src/inno/crypto/crc32.cpp
  src/inno/crypto/hasher.cpp
  src/inno/crypto/md5.cpp
  src/inno/crypto/sha1_crypto.cpp
  src/inno/loader/exereader.cpp
  src/inno/loader/offsets.cpp
  src/inno/setup/component.cpp
  src/inno/setup/data.cpp
  src/inno/setup/delete.cpp
  src/inno/setup/directory.cpp
  src/inno/setup/file_setup.cpp
  src/inno/setup/header.cpp
  src/inno/setup/icon.cpp
  src/inno/setup/info.cpp
  src/inno/setup/ini.cpp
  src/inno/setup/item.cpp
  src/inno/setup/language.cpp
  src/inno/setup/message.cpp
  src/inno/setup/permission.cpp
  src/inno/setup/registry.cpp
  src/inno/setup/run.cpp
  src/inno/setup/task.cpp
  src/inno/setup/type.cpp
  src/inno/setup/version.cpp
  src/inno/setup/windows_setup.cpp
  src/inno/stream/block.cpp
  src/inno/stream/chunk.cpp
  src/inno/stream/file_stream.cpp
  src/inno/stream/lzma.cpp
  src/inno/stream/slice.cpp
  src/inno/util/console.cpp
  src/inno/util/encoding.cpp
  src/inno/util/load.cpp
  src/inno/util/log.cpp
  src/inno/util/time.cpp
)
if(WIN32)
  list(APPEND INNO_FORMAT_SOURCES src/inno/util/windows_util.cpp)
endif()

# The plugin.
add_library(Inno SHARED
  ${INNO_HANDLER_SOURCES}
  ${INNO_FORMAT_SOURCES}
  ${SEVENZIP_COMMON_SOURCES}
  ${SEVENZIP_PLUGIN_SOURCES}
)
target_include_directories(Inno PRIVATE ${SEVENZIP_DIR})
set_target_properties(Inno PROPERTIES PREFIX "")

if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(Inno PROPERTIES OUTPUT_NAME "Inno.64")
  else()
    set_target_properties(Inno PROPERTIES OUTPUT_NAME "Inno.32")
  endif()
  target_sources(Inno PRIVATE ${SEVENZIP_DIR}/CPP/7zip/Archive/Archive.def)
endif()

if(MINGW)
  target_link_options(Inno PRIVATE -static -static-libgcc -static-libstdc++)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Export undecorated names (CreateObject instead of CreateObject@12);
    # the .def file lists them undecorated.
    target_link_options(Inno PRIVATE -Wl,--kill-at -Wl,--enable-stdcall-fixup)
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(Inno PRIVATE -Wl,--no-undefined)
endif()

# The test client.
if(INNO_BUILD_CLIENT)
  add_executable(inno_client
    tests/client/InnoClient.cpp
    ${SEVENZIP_COMMON_SOURCES}
    ${SEVENZIP_CLIENT_SOURCES}
  )
  target_include_directories(inno_client PRIVATE ${SEVENZIP_DIR})
  if(MINGW)
    target_link_options(inno_client PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  if(NOT WIN32)
    target_link_libraries(inno_client PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()
