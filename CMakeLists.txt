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

enable_testing()

message(STATUS "CMAKE_SYSTEM_PROCESSOR = ${CMAKE_SYSTEM_PROCESSOR}")

set(MOS_C_LINK_OPTIONS)
set(MOS_C_EXEC -fPIE)
set(MOS_C_SHARED -fPIC -shared)

if (CMAKE_BUILD_TYPE STREQUAL "Release")

  set(MOS_C_DIAGNOSTIC -Werror -Wall -Wextra
    -Wswitch-enum
    -Wunused
    -Winline
    -Wimplicit-fallthrough
    -Wno-gnu-alignof-expression # allow gnu extension
  )

else()

  # https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
  set(MOS_C_DIAGNOSTIC
    # note: no -Wpedantic, due to use of computed goto
    -Werror
    -Wall -Wextra
    -Winline
    -Wswitch-enum
    -Wimplicit-fallthrough
    -Wunused

    # allow gnu extensions
    -Wno-gnu-alignof-expression

    # FIXME
    -Wno-unused-function
  )

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
    -fsanitize=leak
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)

  set(MOS_C_LINK_ASAN
    -fsanitize=address
    -fsanitize=leak
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


# -- static things --

# if(UNIX AND NOT APPLE)
#   set(MOS_LINK_OPTIONS -static -static-libgcc -static-libstdc++)
# elseif(APPLE)
#   set(MOS_LINK_OPTIONS -static-libstdc++)
# endif()

# -- zlib --

option(ZLIB_BUILD_EXAMPLES "Enable Zlib Examples" OFF)
CPMAddPackage("https://www.zlib.net/zlib-1.3.1.tar.gz")

# -- doxygen --
# must be provided by system, see flake.nix
find_package(Doxygen REQUIRED)

# -- readline --

find_program(MAKE_EXE NAMES gmake nmake make)
find_library(tinfo NAMES tinfo curses) # readline requires tinfo (comes with ncurses)
ExternalProject_Add(readline_build
  URL       "ftp://ftp.cwru.edu/pub/bash/readline-8.2.tar.gz"
  URL_HASH  SHA256=3feb7171f16a84ee82ca18a36d7b9be109a52c04f492a053331d7d1095007c35
  DOWNLOAD_DIR ${PROJECT_SOURCE_DIR}/.cache/readline
  BUILD_IN_SOURCE YES
  CONFIGURE_COMMAND CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} <SOURCE_DIR>/configure --prefix <INSTALL_DIR> --enable-static --disable-shared --with-curses
  BUILD_COMMAND "${MAKE_EXE}"
  BUILD_BYPRODUCTS "<SOURCE_DIR>/libreadline.a"
  INSTALL_COMMAND ${MAKE_EXE} install
  INSTALL_BYPRODUCTS <INSTALL_DIR>/lib/libreadline.a
)
ExternalProject_Get_property(readline_build INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include) # required for
                                            # INTERFACE_INCLUDE_DIRECTORIES
                                            # to work during configure
                                            # phase before directory
                                            # is installed

add_library(readline STATIC IMPORTED)
set_target_properties(readline PROPERTIES
  IMPORTED_LOCATION ${INSTALL_DIR}/lib/libreadline.a
  INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include
)
add_dependencies(readline readline_build)
target_link_libraries(readline INTERFACE tinfo) # users of readline must link tinfo

# -- system specific -----------------------------------------------
# -- Keep this AFTER including external packages --

if(UNIX)
  if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Teach clang about where to find its include files. This is
    # required to make eglot work under NixOS, and to make
    # SCAN_FOR_MODULES work.

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

# -- modules -----------------------------------------------------

add_subdirectory(src/mos)
add_subdirectory(src/tess)
