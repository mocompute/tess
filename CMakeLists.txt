cmake_minimum_required(VERSION 3.31)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)
include(ExternalProject)
include(CheckIPOSupported)

set(CMAKE_VERBOSE_MAKEFILE OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)    # computed goto
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)    # computed goto

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(MOS_UBSAN "Enable -fsanitize=undefined" OFF)
option(MOS_ASAN  "Enable -fsanitize=address" OFF)

project(tess
  VERSION 0.0.0
  DESCRIPTION "The Tess language"
  LANGUAGES C)

message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

include(CTest)
enable_testing()

message(STATUS "CMAKE_SYSTEM_PROCESSOR = ${CMAKE_SYSTEM_PROCESSOR}")

set(MOS_C_LINK_OPTIONS)

if(MSVC)
  set(MOS_C_EXEC)
  set(MOS_C_SHARED)
else()
  set(MOS_C_EXEC -fPIE)
  set(MOS_C_SHARED -fPIC -shared)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Release")

  if(MSVC)
    set(MOS_C_DIAGNOSTIC
      # /WX /Wall
    )
  else()
    set(MOS_C_DIAGNOSTIC -Werror -Wall -Wextra
      -Wswitch-enum
      -Wunused
      -Winline
      -Wimplicit-fallthrough

      # allow GNU alignof extension
      -Wno-gnu-alignof-expression
    )
  endif()

else()

  # https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
  if(MSVC)
    set(MOS_C_DIAGNOSTIC
      # /WX /Wall
    )
  else()
    set(MOS_C_DIAGNOSTIC
      # note: no -Wpedantic, due to use of computed goto
      -Werror
      -Wall -Wextra
      -Winline
      -Wswitch-enum
      -Wimplicit-fallthrough
      -Wunused

      # allow GNU alignof extension
      -Wno-gnu-alignof-expression

      # -Wno-unused-function
    )
  endif()

endif()

if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Release")
  check_ipo_supported(RESULT ipo_supported)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
  message(STATUS "CMAKE_INTERPROCEDURAL_OPTIMIZATION = ${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
endif()

if(MOS_UBSAN)
  list(APPEND MOS_C_DIAGNOSTIC
    -fsanitize=undefined -fno-sanitize-recover=all
  )
  list(APPEND MOS_C_LINK_OPTIONS
    -fsanitize=undefined -fno-sanitize-recover=all
  )
  message(STATUS "Setting UBSAN")
endif()

if(MOS_ASAN)
  set(MOS_C_ASAN
    -fsanitize=address
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)

  set(MOS_C_LINK_ASAN
    -fsanitize=address
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)

  if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    list(APPEND MOS_C_ASAN      -fsanitize-address-use-after-return=always)
    list(APPEND MOS_C_LINK_ASAN -fsanitize-address-use-after-return=always)
  endif()

  list(APPEND MOS_C_DIAGNOSTIC   ${MOS_C_ASAN})
  list(APPEND MOS_C_LINK_OPTIONS ${MOS_C_LINK_ASAN})

  message(STATUS "Setting ASAN")
endif()

# -- zlib --

# option(ZLIB_BUILD_EXAMPLES "Enable Zlib Examples" OFF)
# CPMAddPackage("https://www.zlib.net/zlib-1.3.1.tar.gz")

# -- system specific -----------------------------------------------
# -- Keep this AFTER including external packages --

# Detect NIXOS, for clang include path workaround on unix
if (DEFINED ENV{NIX_PROFILES} OR DEFINED ENV{NIX_PATH} OR DEFINED ENV{IN_NIX_SHELL})
  set(IS_NIX TRUE)
else()
  set(IS_NIX FALSE)
endif()

if(UNIX)
  if (IS_NIX AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Teach clang about where to find its include files. This is required to make eglot work under NixOS,
    # and to make SCAN_FOR_MODULES work. eglot needs compile_commands.json to point to the correct include
    # directories, and the logic below creates -isystem command line arguments to point to the correct
    # /nix/store paths.

    execute_process(COMMAND bash "-c"
      "echo 'int main() {}' | clang++ -E -x c++ -v /dev/null 2>&1 | grep -E '/include$|/v1$'"
      OUTPUT_VARIABLE paths)

    # separate the output into distinct list of directories
    separate_arguments(paths NATIVE_COMMAND ${paths})

    # prepend -isystem to each directory for use in command line
    list(TRANSFORM paths PREPEND "-isystem"
      OUTPUT_VARIABLE MOS_SYSTEM_INCLUDE)

    # tell clang not to use any other search directories for include files
    list(PREPEND MOS_SYSTEM_INCLUDE
      -nostdinc
      -Wno-unused-command-line-argument # clang-19
    )
    message(STATUS "MOS_SYSTEM_INCLUDE = ${MOS_SYSTEM_INCLUDE}")
  else()
    set(MOS_SYSTEM_INCLUDE "")
  endif()

  #
  # globally set these options
  #
  add_compile_options(${MOS_SYSTEM_INCLUDE})
endif()

# -- Windows-specific definitions --------------------------------

if(WIN32)
  add_compile_definitions(MOS_WINDOWS)
endif()

# -- modules -----------------------------------------------------

add_subdirectory(src/mos)
add_subdirectory(src/tess)
