message(STATUS "Using diagnostics: ${MOS_C_DIAGNOSTIC}")

# -- embed --

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  COMMAND mos_embed
    ${CMAKE_CURRENT_SOURCE_DIR}/embed
    ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  DEPENDS
    $<TARGET_FILE:mos_embed>
    ${CMAKE_CURRENT_SOURCE_DIR}/embed/std.c
    ${CMAKE_CURRENT_SOURCE_DIR}/embed/std.tl
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Processing embedded files from directory ${CMAKE_CURRENT_SOURCE_DIR}/embed"
)

# -- library --

add_library(tess)
set(HEADERS
  ${PROJECT_SOURCE_DIR}/src/tess/include/ast.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/ast_tags.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/transpiler.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/error.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/parser.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/syntax.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tess.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/type.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/token.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tokenizer.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/type_inference.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/type_registry.h
)

set(SOURCES
  src/ast.c
  src/transpiler.c
  src/error.c
  src/parser.c
  src/syntax.c
  src/tess.c
  src/type.c
  src/token.c
  src/tokenizer.c
  src/type_inference.c
  src/type_registry.c
  ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  )
target_sources(tess PRIVATE ${HEADERS} ${SOURCES})
target_compile_options(tess BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_include_directories(tess PUBLIC "${PROJECT_SOURCE_DIR}/src/tess/include")
target_link_libraries(tess PRIVATE PUBLIC mos)
target_link_options(tess PRIVATE ${MOS_C_LINK_OPTIONS})

# -- exe --

add_executable(tess_exe src/tess_exe.c)
set_target_properties(tess_exe PROPERTIES OUTPUT_NAME tess)
target_compile_options(tess_exe BEFORE PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC})
target_link_libraries(tess_exe PUBLIC tess readline)
target_link_options(tess_exe PRIVATE ${MOS_C_LINK_OPTIONS})

# -- tests --

function(tess_create_test NAME)
  set(TEST_NAME test-${NAME})
  add_executable(${TEST_NAME} "src/test_${NAME}.c")
  target_link_libraries(${TEST_NAME} PRIVATE tess)
  target_compile_options(${TEST_NAME} PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC} -Wunused)
  target_link_options(${TEST_NAME} PRIVATE ${MOS_C_LINK_OPTIONS})
  add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endfunction()

tess_create_test(tess)

# -- tesslang tests --

function(tesslang_create_test NAME)
  set(TEST_NAME test-tl-${NAME})

  # transpile to C
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}.c
    COMMAND tess_exe
      c
      -o ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}.c
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    DEPENDS
      $<TARGET_FILE:tess_exe>
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl to test_${NAME}.c"
  )

  add_executable(${TEST_NAME} "${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}.c")
  target_link_libraries(${TEST_NAME} PRIVATE tess)
  target_compile_options(${TEST_NAME} PRIVATE ${MOS_C_ASAN} -Wno-format-security)
  target_link_options(${TEST_NAME} PRIVATE ${MOS_C_LINK_ASAN})
  add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endfunction()

tesslang_create_test(address_of)
tesslang_create_test(alloc)
tesslang_create_test(basic)
tesslang_create_test(begin_end)
tesslang_create_test(chained)
tesslang_create_test(dereference)
tesslang_create_test(if_then_else)
tesslang_create_test(if_then_closure)
# tesslang_create_test(fun_apply_type_error)
tesslang_create_test(forwards)
tesslang_create_test(fun_annotation)
tesslang_create_test(function_pointer_argument)
tesslang_create_test(intrinsic_sizeof)
tesslang_create_test(let_basic)
tesslang_create_test(let_in_basic)
tesslang_create_test(let_in_nil)
tesslang_create_test(std_dbg)
tesslang_create_test(struct)
tesslang_create_test(struct_get)
tesslang_create_test(struct_set)
tesslang_create_test(tuple)
tesslang_create_test(tuple_match)
tesslang_create_test(labelled_tuple)

# -- doxygen --

set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
set(DOXYGEN_QUIET YES)
doxygen_add_docs(docs_tess ${HEADERS})
add_dependencies(tess docs_tess)
