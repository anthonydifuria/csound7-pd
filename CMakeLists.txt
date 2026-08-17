cmake_minimum_required(VERSION 3.25)

# Universal binary by default: arm64 + x86_64, buildable from a single
# Apple Silicon Mac (Xcode cross-compiles both slices, no Intel Mac needed).
# Same choice as the Max build - see ../MAX/CMakeLists.txt.
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

if(NOT DEFINED CACHE{CMAKE_OSX_DEPLOYMENT_TARGET})
  # Matches Csound's own forced minimum (its CMake sets 11 regardless of
  # what we ask for) - keeps them aligned so the linker stops warning
  # about mismatched deployment targets.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version")
endif()

include(FetchContent)

# We only need Pd's own headers (m_pd.h, g_canvas.h) - not a full Pd build.
# Pd externals don't link against Pd itself (same idea as Max: the host
# process resolves these symbols at load time), so a shallow clone of the
# real Pd source tree is enough, and much cheaper than a full checkout.
message(STATUS "Fetching pure-data (Pd headers)")
FetchContent_Declare(
  pure-data
  GIT_REPOSITORY https://github.com/pure-data/pure-data.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pure-data)

project(csound7_pd_package)

# Where the static Csound build (scripts/build_csound_static.sh) drops its
# libs/headers. Built separately from the Max side (../MAX/scripts/) on
# purpose - keeps PD/ self-contained so it can move to its own repo later
# without depending on anything in MAX/. Override with -DCSOUND_STATIC_ROOT=...
# if you built it somewhere else (e.g. to reuse the Max side's build on
# macOS and skip rebuilding Csound from scratch).
#
# Path differs by OS because the build script itself does: macOS gets one
# universal (arm64+x86_64) folder from scripts/build_csound_static.sh
# (bash), Linux gets a single-arch folder named after the host's own
# architecture from that same script's Linux branch (verified with a real
# build in an aarch64 Linux sandbox - CMAKE_SYSTEM_PROCESSOR there reported
# "aarch64", matching `uname -m`, which is exactly what the script uses to
# name its own output folder). Windows gets its own fixed "windows-x64"
# folder from the separate scripts/build_csound_static.ps1 (PowerShell,
# not bash - MSVC's toolchain is different enough that reusing the same
# script isn't realistic, see that script's own top comment); x64 only,
# unlike the other two, since that's the only Windows architecture
# targeted anywhere in this project so far.
if(NOT DEFINED CSOUND_STATIC_ROOT)
  if(APPLE)
    set(CSOUND_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/csound-install/universal")
  elseif(WIN32)
    set(CSOUND_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/csound-install/windows-x64")
  elseif(UNIX)
    set(CSOUND_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/csound-install/linux-${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endif()

add_subdirectory(source/csound7_pd)
