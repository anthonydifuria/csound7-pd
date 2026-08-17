project(csound7_pd)

# Pd externals are flat shared-library modules (unlike Max's .mxo bundle),
# loaded by name - "csound7~.pd_darwin" on macOS, "csound7~.pd_linux" on
# Linux, "csound7~.dll" on Windows. No "lib" prefix, and the file's base
# name (before the extension) is exactly the object's name as typed in a
# patch, tilde included.
add_library(csound7_pd MODULE csound7_pd.c)

set_target_properties(csound7_pd PROPERTIES
  OUTPUT_NAME "csound7~"
  PREFIX ""
  # Land the built module directly in PD/externals/, sibling to source/ -
  # same convention as the Max side, so you don't have to go hunting
  # inside the build/ tree for it after every build. RUNTIME_OUTPUT_DIRECTORY
  # covers Windows (.dll counts as "runtime" there, not "library").
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../externals"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../externals"
)
if(APPLE)
  set_target_properties(csound7_pd PROPERTIES SUFFIX ".pd_darwin")
elseif(UNIX)
  set_target_properties(csound7_pd PROPERTIES SUFFIX ".pd_linux")
elseif(WIN32)
  set_target_properties(csound7_pd PROPERTIES SUFFIX ".dll")
endif()

# Pd's own headers - just the source tree fetched by ../../CMakeLists.txt,
# we don't build or link against Pd itself (same reasoning as Max: the host
# process resolves these symbols when it loads us, we only need the
# declarations at compile time).
target_include_directories(csound7_pd PRIVATE
  "${pure-data_SOURCE_DIR}/src"
)

# --- Static Csound 7 (built by ../../scripts/build_csound_static.sh) ---
target_include_directories(csound7_pd PRIVATE
  "${CSOUND_STATIC_ROOT}/include"
  "${CSOUND_STATIC_ROOT}/include/csound"
)

# Csound's own static lib file name is NOT the same on every OS - confirmed
# by reading CMakeLists.txt's own CSOUNDLIB variable directly: it's only
# "CsoundLib64" (-> libCsoundLib64.a) on Apple; everywhere else (Linux,
# and Windows too - that if/else isn't platform-specific beyond the Apple
# check) it's just "csound64". Verified with a real static build on Linux
# (aarch64 sandbox) - macOS's own libCsoundLib64.a name, which this file
# used to hardcode unconditionally, does not exist there at all.
if(APPLE)
  set(CSOUND_STATIC_LIB "${CSOUND_STATIC_ROOT}/lib/libCsoundLib64.a")
elseif(UNIX)
  set(CSOUND_STATIC_LIB "${CSOUND_STATIC_ROOT}/lib/libcsound64.a")
endif()
set(SNDFILE_STATIC_LIB "${CSOUND_STATIC_ROOT}/lib/libsndfile.a")
set(SAMPLERATE_STATIC_LIB "${CSOUND_STATIC_ROOT}/lib/libsamplerate.a")

if(WIN32)
  # Same CSOUNDLIB="csound64" naming as Linux applies here too (see comment
  # above) - Csound's own CMakeLists.txt also unconditionally forces a
  # "lib" prefix on the static target's OUTPUT_NAME regardless of platform
  # (`SET_TARGET_PROPERTIES(... PROPERTIES PREFIX "lib")`, not guarded by
  # any if(APPLE)/if(UNIX)), so "libcsound64.lib" is the well-grounded
  # prediction - confirmed by reading the source, same as the Linux name
  # was, just not yet confirmed by an actual Windows build/link the way
  # Linux was. libsndfile/libsamplerate's own CMake projects weren't read
  # closely enough to know whether they do the same "lib" prefix forcing on
  # Windows (CMake's own default for a STATIC target does NOT add one on
  # MSVC, unlike its Unix default) - find_library() below tries both
  # spellings rather than guessing a single one, so this doesn't need a
  # second real Windows build just to find out which.
  find_library(CSOUND_STATIC_LIB_WIN NAMES libcsound64 csound64
    PATHS "${CSOUND_STATIC_ROOT}/lib" NO_DEFAULT_PATH)
  find_library(SNDFILE_STATIC_LIB_WIN NAMES libsndfile sndfile
    PATHS "${CSOUND_STATIC_ROOT}/lib" NO_DEFAULT_PATH)
  find_library(SAMPLERATE_STATIC_LIB_WIN NAMES libsamplerate samplerate
    PATHS "${CSOUND_STATIC_ROOT}/lib" NO_DEFAULT_PATH)
  if(CSOUND_STATIC_LIB_WIN)
    set(CSOUND_STATIC_LIB "${CSOUND_STATIC_LIB_WIN}")
  endif()
  if(SNDFILE_STATIC_LIB_WIN)
    set(SNDFILE_STATIC_LIB "${SNDFILE_STATIC_LIB_WIN}")
  endif()
  if(SAMPLERATE_STATIC_LIB_WIN)
    set(SAMPLERATE_STATIC_LIB "${SAMPLERATE_STATIC_LIB_WIN}")
  endif()
endif()

foreach(lib IN ITEMS CSOUND_STATIC_LIB SNDFILE_STATIC_LIB SAMPLERATE_STATIC_LIB)
  if(NOT EXISTS "${${lib}}")
    message(WARNING "${lib} not found at ${${lib}} — run scripts/build_csound_static.sh first, or fix the path/filename here.")
  endif()
endforeach()

# CoreFoundation/Accelerate are macOS frameworks - Csound's own static lib
# pulls symbols from them on Apple only (confirmed unconditional before this
# fix, which broke CMake's configure step outright on Linux: "-framework"
# isn't a flag Linux's linker understands). pthread is needed on every
# platform this external runs on (csound7_pd.c uses pthread_mutex_* directly).
target_link_libraries(csound7_pd PRIVATE
  "${CSOUND_STATIC_LIB}"
  "${SNDFILE_STATIC_LIB}"
  "${SAMPLERATE_STATIC_LIB}"
)
if(APPLE OR (UNIX AND NOT WIN32))
  # pthread: needed on every Unix-y platform this external runs on
  # (csound7_pd.c uses pthread_mutex_* directly). Not meaningful on
  # Windows - MSVC's runtime doesn't have a "-lpthread" equivalent to link
  # (threading is part of the standard runtime there already).
  target_link_libraries(csound7_pd PRIVATE "-lpthread")
endif()
if(APPLE)
  target_link_libraries(csound7_pd PRIVATE
    "-framework CoreFoundation"
    "-framework Accelerate"
    "-lc++"
  )
endif()
# Linux: no "-lstdc++" needed here even though libcsound64.a bundles C++
# opcode object files (same reason as Apple needing libc++/RTTI/exceptions
# pulled in) - LINKER_LANGUAGE CXX below makes CMake invoke g++ (not gcc) as
# the actual link driver, and g++ links libstdc++ automatically whenever
# it's the one doing the linking, no explicit flag needed. Confirmed with a
# real link on Linux: adding "-lstdc++" by hand made no difference either
# way, it was already being pulled in. Windows/MSVC: same idea, the C++
# runtime comes along automatically once LINKER_LANGUAGE CXX below makes
# CMake invoke the C++ driver - no separate runtime lib to name by hand.

# Same reason as the Max side: the Csound static lib bundles C++ opcode
# object files, so the link step needs a C++ compiler driver (via
# LINKER_LANGUAGE CXX) to pull in the C++ runtime/exception handling/RTTI,
# even though our own source here is plain C.
set_target_properties(csound7_pd PROPERTIES LINKER_LANGUAGE CXX)

if(APPLE)
  # Pd itself provides all the pd_*/class_*/atom_*/etc. symbols at load
  # time (same idea as Max resolving its own SDK symbols against the host
  # app) - but unlike Max, macOS's linker refuses to produce a MH_BUNDLE
  # module with any undefined symbols left in it by default, hence the
  # wall of "Undefined symbols ... atom_getfloat, class_new, ..." on the
  # first real build. -undefined dynamic_lookup tells the linker "these
  # are fine, resolve them when the host process loads this" - the same
  # flag pd-lib-builder (the de facto standard Pd external build tool)
  # uses for exactly this reason.
  target_link_options(csound7_pd PRIVATE "-undefined" "dynamic_lookup")
elseif(WIN32)
  # MSVC is the one linker of the three that does NOT tolerate unresolved
  # symbols in a shared module by default - no direct equivalent of
  # -undefined dynamic_lookup exists. The real, standard fix (confirmed by
  # reading pure-data's own official pd.cmake build tooling, not guessed:
  # https://github.com/pure-data/pd.cmake links MSVC/Windows externals
  # against "${PDBINDIR}/pd.lib", an import library matching pd.dll) is to
  # link against an import library for pd.dll. We don't have a real one
  # (see pd_stub.def's own header comment for why), so we generate a
  # minimal stand-in ourselves from that .def file, using CMAKE_AR (CMake's
  # own already-resolved path to the active toolchain's librarian - lib.exe
  # under MSVC) rather than hardcoding "lib.exe" and hoping it's on PATH.
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pd.lib"
    COMMAND "${CMAKE_AR}" /DEF:"${CMAKE_CURRENT_SOURCE_DIR}/pd_stub.def"
            /OUT:"${CMAKE_CURRENT_BINARY_DIR}/pd.lib" /MACHINE:X64
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pd_stub.def"
    COMMENT "Generating a stub pd.lib from pd_stub.def (real symbols resolve at runtime, once Pd loads this DLL)"
  )
  add_custom_target(pd_stub_lib DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pd.lib")
  add_dependencies(csound7_pd pd_stub_lib)
  target_link_libraries(csound7_pd PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pd.lib")
endif()

# Linux needs no equivalent flag or stand-in library at all - confirmed
# with a real build: GNU ld's default behavior when linking a shared
# object (-shared, which is what MODULE + no SUFFIX override effectively
# produces here) already tolerates undefined symbols with zero extra
# flags, unlike macOS's and Windows's defaults. Pd's own process resolves
# pd_typedmess/gensym/class_new/etc. when it dlopen()s this .pd_linux
# file, same idea as everywhere else - Linux is just the most lenient of
# the three about it at link time.
