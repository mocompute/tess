message(STATUS "Using diagnostics: ${MOS_C_DIAGNOSTIC}")

# -- version --

file(STRINGS ${PROJECT_SOURCE_DIR}/VERSION TESS_VERSION)
set(VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/version.h)

# Get git hash — accept GIT_HASH env var (for Nix), else try git, else "nogit"
if(DEFINED ENV{GIT_HASH})
  set(GIT_HASH $ENV{GIT_HASH})
else()
  execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
  )
  if(NOT GIT_RESULT EQUAL 0)
    set(GIT_HASH "nogit")
  endif()
endif()

# System info
set(TESS_ARCH ${CMAKE_SYSTEM_PROCESSOR})
string(TOLOWER ${CMAKE_SYSTEM_NAME} TESS_OS)

# Generate version header at configure time
file(WRITE ${VERSION_HEADER}
"/* Auto-generated version header */
#ifndef TESS_VERSION_H
#define TESS_VERSION_H

#define TESS_VERSION \"${TESS_VERSION}-${GIT_HASH}-${TESS_ARCH}-${TESS_OS}\"

#endif /* TESS_VERSION_H */
")

# -- embed --

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  COMMAND mos_embed
    ${CMAKE_CURRENT_SOURCE_DIR}/embed
    ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  DEPENDS
    $<TARGET_FILE:mos_embed>
    ${CMAKE_CURRENT_SOURCE_DIR}/embed/std.c
    ${CMAKE_CURRENT_SOURCE_DIR}/embed/prelude.tl
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Processing embedded files from directory ${CMAKE_CURRENT_SOURCE_DIR}/embed"
)

# -- library --

add_library(tess)
set(HEADERS
  ${PROJECT_SOURCE_DIR}/src/tess/include/ast.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/ast_tags.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/error.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/import_resolver.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/manifest.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/source_scanner.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/parser.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tess.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/token.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tokenizer.h

  ${PROJECT_SOURCE_DIR}/src/tess/include/format.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/infer.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/transpile.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/type.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/type_registry.h
)

set(SOURCES
  src/ast.c
  src/error.c
  src/format.c
  src/import_resolver.c
  src/manifest.c
  src/source_scanner.c
  src/parser.c
  src/parser_expr.c
  src/parser_statements.c
  src/parser_tagged_union.c
  src/parser_types.c
  src/tess.c
  src/token.c
  src/tokenizer.c

  src/infer.c
  src/infer_alpha.c
  src/infer_constraint.c
  src/infer_specialize.c
  src/infer_update.c
  src/transpile.c
  src/tpkg.c
  src/type.c

  ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  )
target_sources(tess PRIVATE ${HEADERS} ${SOURCES})
target_compile_options(tess BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_include_directories(tess PUBLIC "${PROJECT_SOURCE_DIR}/src/tess/include")
target_link_libraries(tess PUBLIC mos libdeflate::libdeflate_static)
target_link_options(tess PRIVATE ${MOS_C_LINK_OPTIONS})

# -- stdlib embedding --

add_executable(stdlib_pack src/stdlib_pack.c)
target_compile_options(stdlib_pack BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_link_libraries(stdlib_pack PRIVATE tess)
target_link_options(stdlib_pack PRIVATE ${MOS_C_LINK_OPTIONS})

file(GLOB_RECURSE TL_STD_SOURCES "${PROJECT_SOURCE_DIR}/src/tl/std/*.tl")
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tess_stdlib_embed.c
  COMMAND stdlib_pack
    ${PROJECT_SOURCE_DIR}/src/tl/std
    ${CMAKE_CURRENT_BINARY_DIR}/tess_stdlib_embed.c
  DEPENDS
    $<TARGET_FILE:stdlib_pack>
    ${TL_STD_SOURCES}
  COMMENT "Packing stdlib into embeddable C source"
)

# -- exe --

add_executable(tess_exe src/tess_exe.c ${CMAKE_CURRENT_BINARY_DIR}/tess_stdlib_embed.c)
set_target_properties(tess_exe PROPERTIES OUTPUT_NAME tess)
target_compile_options(tess_exe BEFORE PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC})
target_include_directories(tess_exe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(tess_exe PUBLIC tess)
target_link_options(tess_exe PRIVATE ${MOS_C_LINK_OPTIONS})

# -- tests --

function(tess_create_test NAME)
  set(TEST_NAME test-${NAME})
  add_executable(${TEST_NAME} "src/test_${NAME}.c")
  target_link_libraries(${TEST_NAME} PRIVATE tess)
  target_compile_options(${TEST_NAME} PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC})
  target_link_options(${TEST_NAME} PRIVATE ${MOS_C_LINK_OPTIONS})
  add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endfunction()

tess_create_test(tess)
tess_create_test(type_v2)
tess_create_test(format)
tess_create_test(tpkg)
target_compile_definitions(test-tpkg PRIVATE
  "TEST_TESS_EXE=\"$<TARGET_FILE:tess_exe>\""
  "TEST_STDLIB_DIR=\"${PROJECT_SOURCE_DIR}/src/tl/std\"")
tess_create_test(import_resolver)
tess_create_test(manifest)
tess_create_test(source_scanner)

# -- vendor tests --

add_executable(test-vendor-deflate src/test_deflate.c)
target_link_libraries(test-vendor-deflate PRIVATE libdeflate::libdeflate_static)
target_compile_options(test-vendor-deflate PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC})
target_link_options(test-vendor-deflate PRIVATE ${MOS_C_LINK_OPTIONS})
add_test(NAME test-vendor-deflate COMMAND $<TARGET_FILE:test-vendor-deflate>)


# -- tesslang tests --

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tl)

function(tl_create_test NAME SUBDIR)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tl/${SUBDIR}/test_${NAME}.tl)
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND
    tess_exe
    exe
    --no-standard-includes
    -I ${CMAKE_CURRENT_SOURCE_DIR}
    -S ${PROJECT_SOURCE_DIR}/src/tl/std
    -o ${TEST_EXE}
    ${TEST_SRC}
    DEPENDS
      $<TARGET_FILE:tess_exe>
      ${TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl"
  )
  add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_EXE})
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    LABELS "expected_pass"
  )
endfunction()

function(tl_create_test_optimized NAME SUBDIR)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tl/${SUBDIR}/test_${NAME}.tl)
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND
    tess_exe
    exe
    --no-standard-includes
    -I ${CMAKE_CURRENT_SOURCE_DIR}
    -S ${PROJECT_SOURCE_DIR}/src/tl/std
    -o ${TEST_EXE}
    ${TEST_SRC}
    DEPENDS
      $<TARGET_FILE:tess_exe>
      ${TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl"
  )
  add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_EXE})
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    LABELS "expected_pass_optimized"
  )
endfunction()

function(tl_create_build_fail_test NAME SUBDIR)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tl/${SUBDIR}/test_${NAME}.tl)
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_test(NAME ${TEST_NAME}
    COMMAND $<TARGET_FILE:tess_exe> exe --no-standard-includes -I ${CMAKE_CURRENT_SOURCE_DIR} -S ${PROJECT_SOURCE_DIR}/src/tl/std -o ${TEST_EXE} ${TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_NAME} PROPERTIES
    WILL_FAIL TRUE
    LABELS "expected_failure"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endfunction()

# Known failures (tests that should work but currently fail due to compiler bugs)
function(tl_create_known_failure NAME)
  set(TEST_NAME test-tl-${NAME})
  add_test(NAME ${TEST_NAME}
    COMMAND
    ${CMAKE_COMMAND} -E echo "KNOWN FAILURE: test_${NAME} (skipped)"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_NAME} PROPERTIES
    LABELS "known_failure"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endfunction()

# Known fail-failures (expected-failure tests that the compiler doesn't reject yet)
function(tl_create_known_fail_failure NAME)
  set(TEST_NAME test-tl-${NAME})
  add_test(NAME ${TEST_NAME}
    COMMAND ${CMAKE_COMMAND} -E echo "KNOWN FAIL FAILURE: test_${NAME} (skipped)"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_NAME} PROPERTIES
    LABELS "known_fail_failure"
  )
endfunction()

# Expected runtime failures (must compile, must fail at runtime)
function(tl_create_runtime_fail_test NAME SUBDIR)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tl/${SUBDIR}/test_${NAME}.tl)
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND $<TARGET_FILE:tess_exe> exe --no-standard-includes --bounds-check -I ${CMAKE_CURRENT_SOURCE_DIR} -S ${PROJECT_SOURCE_DIR}/src/tl/std -o ${TEST_EXE}
      ${TEST_SRC}
    DEPENDS tess_exe
      ${TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl"
  )
  add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
  add_test(NAME ${TEST_NAME}
    COMMAND ${CMAKE_COMMAND} -E env ${TEST_EXE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_NAME} PROPERTIES
    WILL_FAIL TRUE
    LABELS "expected_runtime_failure"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endfunction()

# Auto-discover pass tests
file(GLOB TL_PASS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tl/test/pass/test_*.tl")
foreach(SRC ${TL_PASS_SOURCES})
  get_filename_component(F ${SRC} NAME_WE)
  string(REGEX REPLACE "^test_" "" NAME ${F})
  tl_create_test(${NAME} "test/pass")
endforeach()

# Auto-discover optimized pass tests
file(GLOB TL_PASS_OPT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tl/test/pass_optimized/test_*.tl")
foreach(SRC ${TL_PASS_OPT_SOURCES})
  get_filename_component(F ${SRC} NAME_WE)
  string(REGEX REPLACE "^test_" "" NAME ${F})
  tl_create_test_optimized(${NAME} "test/pass_optimized")
endforeach()

# Special test for import_relative_dotdot (needs different working directory)
set(TEST_NAME test-tl-import_relative_dotdot)
set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_import_relative_dotdot${CMAKE_EXECUTABLE_SUFFIX})
add_custom_command(
  OUTPUT ${TEST_EXE}
  COMMAND
  tess_exe
  exe
  --no-standard-includes
  -S ${PROJECT_SOURCE_DIR}/src/tl/std
  -o ${TEST_EXE}
  test_import_relative_dotdot.tl
  DEPENDS
    $<TARGET_FILE:tess_exe>
    ${CMAKE_CURRENT_SOURCE_DIR}/tl/fixtures/test_import_relative_dotdot.tl
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tl/fixtures
  COMMENT "Compiling test_import_relative_dotdot.tl"
)
add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
add_test(NAME ${TEST_NAME} COMMAND ${TEST_EXE})
set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  LABELS "expected_pass"
)

# Auto-discover expected failure tests
file(GLOB TL_FAIL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tl/test/fail/test_*.tl")
foreach(SRC ${TL_FAIL_SOURCES})
  get_filename_component(F ${SRC} NAME_WE)
  string(REGEX REPLACE "^test_" "" NAME ${F})
  tl_create_build_fail_test(${NAME} "test/fail")
endforeach()

# Auto-discover expected runtime failure tests
file(GLOB TL_FAIL_RT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tl/test/fail_runtime/test_*.tl")
foreach(SRC ${TL_FAIL_RT_SOURCES})
  get_filename_component(F ${SRC} NAME_WE)
  string(REGEX REPLACE "^test_" "" NAME ${F})
  tl_create_runtime_fail_test(${NAME} "test/fail_runtime")
endforeach()

# Auto-discover known fail failures
file(GLOB TL_KNOWN_FF_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tl/test/known_fail_failures/test_*.tl")
foreach(SRC ${TL_KNOWN_FF_SOURCES})
  get_filename_component(F ${SRC} NAME_WE)
  string(REGEX REPLACE "^test_" "" NAME ${F})
  tl_create_known_fail_failure(${NAME})
endforeach()

# Auto-discover known failures
file(GLOB TL_KNOWN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tl/test/known_failures/test_*.tl")
foreach(SRC ${TL_KNOWN_SOURCES})
  get_filename_component(F ${SRC} NAME_WE)
  string(REGEX REPLACE "^test_" "" NAME ${F})
  tl_create_known_failure(${NAME})
endforeach()
