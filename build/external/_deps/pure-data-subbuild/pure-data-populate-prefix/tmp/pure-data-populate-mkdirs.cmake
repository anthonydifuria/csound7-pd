# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-src")
  file(MAKE_DIRECTORY "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-src")
endif()
file(MAKE_DIRECTORY
  "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-build"
  "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix"
  "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix/tmp"
  "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix/src/pure-data-populate-stamp"
  "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix/src"
  "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix/src/pure-data-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix/src/pure-data-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/ant/Desktop/csound7_tilde/PD/build/external/_deps/pure-data-subbuild/pure-data-populate-prefix/src/pure-data-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
