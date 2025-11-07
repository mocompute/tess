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
  ${PROJECT_SOURCE_DIR}/src/tess/include/error.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/parser.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tess.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/token.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tokenizer.h

  ${PROJECT_SOURCE_DIR}/src/tess/include/infer.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/transpile.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/type.h
)

set(SOURCES
  src/ast.c
  src/error.c
  src/parser.c
  src/tess.c
  src/token.c
  src/tokenizer.c

  src/infer.c
  src/transpile.c
  src/type.c

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
tess_create_test(type_v2)

# -- tesslang tests --

function(tesslang_create_test_no_preamble NAME)
  set(TEST_NAME test-tl-${NAME})

  # transpile to C
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}.c
    COMMAND tess_exe
      c
      --no-preamble
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

tesslang_create_test_no_preamble(v3_address_of)
tesslang_create_test_no_preamble(v3_anon_lambda)
tesslang_create_test_no_preamble(v3_apply_generic)
tesslang_create_test_no_preamble(v3_apply_generic_through_pointer)
tesslang_create_test_no_preamble(v3_apply_lambda)
tesslang_create_test_no_preamble(v3_binop)
tesslang_create_test_no_preamble(v3_c_symbol_annotation)
tesslang_create_test_no_preamble(v3_c_timespec)
tesslang_create_test_no_preamble(v3_char_literal)
tesslang_create_test_no_preamble(v3_closure_polymorphism)
tesslang_create_test_no_preamble(v3_cond_basic)
tesslang_create_test_no_preamble(v3_defun_inline_type)
tesslang_create_test_no_preamble(v3_dynamic_array)
tesslang_create_test_no_preamble(v3_enum_no_module)
tesslang_create_test_no_preamble(v3_enum_module)
tesslang_create_test_no_preamble(v3_embed_c)
tesslang_create_test_no_preamble(v3_if_basic)
tesslang_create_test_no_preamble(v3_factorial)
tesslang_create_test_no_preamble(v3_function_pointer_argument)
tesslang_create_test_no_preamble(v3_function_pointer_value)
tesslang_create_test_no_preamble(v3_function_pointer_in_struct)
tesslang_create_test_no_preamble(v3_function_pointer_in_struct_direct)
tesslang_create_test_no_preamble(v3_generic_lambda)
tesslang_create_test_no_preamble(v3_global_variables)
tesslang_create_test_no_preamble(v3_lambda_basic)
tesslang_create_test_no_preamble(v3_lambda_apply)
tesslang_create_test_no_preamble(v3_lambda_immediate)
tesslang_create_test_no_preamble(v3_let_in_basic)
tesslang_create_test_no_preamble(v3_malloc_free)
tesslang_create_test_no_preamble(v3_malloc_free_is_null)
tesslang_create_test_no_preamble(v3_malloc_struct_basic)
tesslang_create_test_no_preamble(v3_module_basic)
tesslang_create_test_no_preamble(v3_mutual_recursion)
tesslang_create_test_no_preamble(v3_nested_struct_access)
tesslang_create_test_no_preamble(v3_nested_lambda_context)
tesslang_create_test_no_preamble(v3_number_separators)
tesslang_create_test_no_preamble(v3_printf)
tesslang_create_test_no_preamble(v3_pointer_deref)
tesslang_create_test_no_preamble(v3_reassign_into_stack_lambda)
tesslang_create_test_no_preamble(v3_relational_basic)
tesslang_create_test_no_preamble(v3_return_statement)
tesslang_create_test_no_preamble(v3_sizeof)
tesslang_create_test_no_preamble(v3_sizeof_type_literal)
tesslang_create_test_no_preamble(v3_static_init)
tesslang_create_test_no_preamble(v3_struct_concrete)
tesslang_create_test_no_preamble(v3_struct_empty)
tesslang_create_test_no_preamble(v3_struct_generic)
tesslang_create_test_no_preamble(v3_type_literal_generic)
tesslang_create_test_no_preamble(v3_type_arguments_annotations)
tesslang_create_test_no_preamble(v3_union_module_intermediate)
tesslang_create_test_no_preamble(v3_union_module_second_variant)
tesslang_create_test_no_preamble(v3_while_statement)


# -- doxygen --

if(MOS_DOXY)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
  set(DOXYGEN_QUIET YES)
  doxygen_add_docs(docs_tess ${HEADERS})
  add_dependencies(tess docs_tess)
endif()
