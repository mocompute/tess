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
target_link_libraries(tess_exe PUBLIC tess)
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

function(tesslang_create_test NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND
    tess_exe
    exe
    -I ${CMAKE_CURRENT_SOURCE_DIR}
    -I ${PROJECT_SOURCE_DIR}/src/tl/std
    -o ${TEST_EXE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    DEPENDS
      $<TARGET_FILE:tess_exe>
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl"
  )
  add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_EXE})
endfunction()

function(tesslang_create_build_fail_test NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME})
  add_test(NAME ${TEST_NAME}
    COMMAND
    $<TARGET_FILE:tess_exe>
    exe
    -I ${CMAKE_CURRENT_SOURCE_DIR}
    -I ${PROJECT_SOURCE_DIR}/src/tl/std
    -o ${TEST_EXE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_property(TEST ${TEST_NAME} PROPERTY WILL_FAIL true)
endfunction()

tesslang_create_test(address_of)
tesslang_create_test(anon_lambda)
tesslang_create_test(apply_generic)
tesslang_create_test(apply_generic_through_pointer)
tesslang_create_test(apply_lambda)
tesslang_create_test(arithmetic_unary_op)
tesslang_create_test(array_index_binary_op)
tesslang_create_test(binop)
tesslang_create_test(c_symbol_annotation)
tesslang_create_test(c_timespec)
tesslang_create_test(case_basic_else)
tesslang_create_test(case_float)
tesslang_create_test(cast_string_to_ptr)
tesslang_create_test(cast_string_to_ptr_and_index)
tesslang_create_test(char_literal)
tesslang_create_test(closure_polymorphism)
tesslang_create_build_fail_test(cond_basic) # FIXME
tesslang_create_build_fail_test(cond_expression) # FIXME
tesslang_create_test(defun_inline_type)
tesslang_create_test(dynamic_array)
tesslang_create_test(enum_no_module)
tesslang_create_test(enum_module)
tesslang_create_test(embed_c)
tesslang_create_test(if_basic)
tesslang_create_test(if_expression)
tesslang_create_test(factorial)
tesslang_create_test(function_pointer_argument)
tesslang_create_test(function_pointer_value)
tesslang_create_test(function_pointer_in_struct)
tesslang_create_test(function_pointer_in_struct_direct)
tesslang_create_test(generic_lambda)
tesslang_create_test(global_variables)
tesslang_create_test(lambda_basic)
tesslang_create_test(lambda_apply)
tesslang_create_test(lambda_immediate)
tesslang_create_test(lambda_immediate_type_argument)
tesslang_create_test(let_in_basic)
tesslang_create_test(malloc_free)
tesslang_create_test(malloc_free_is_null)
tesslang_create_test(malloc_struct_basic)
tesslang_create_test(module_basic)
tesslang_create_test(mutual_recursion)
tesslang_create_test(mutual_recursion_module)
tesslang_create_test(mutual_recursion_module_apply)
tesslang_create_test(mutual_recursion_both_referenced)
tesslang_create_test(nested_struct_access)
tesslang_create_test(nested_lambda_context)
tesslang_create_test(number_separators)
tesslang_create_test(printf)
tesslang_create_test(pointer_deref)
tesslang_create_test(reassign_into_stack_lambda)
tesslang_create_test(relational_basic)
tesslang_create_test(return_statement)
tesslang_create_test(sizeof)
tesslang_create_test(sizeof_type_literal)
tesslang_create_test(static_init)
tesslang_create_test(strcmp)
tesslang_create_test(struct_concrete)
tesslang_create_test(struct_empty)
tesslang_create_test(struct_generic)
tesslang_create_test(type_literal_generic)
tesslang_create_test(type_arguments_annotations)
tesslang_create_test(type_argument_field_annotation)
tesslang_create_build_fail_test(unknown_free_variable)
tesslang_create_test(union_module_intermediate)
tesslang_create_test(union_module_second_variant)
tesslang_create_test(while_statement)
tesslang_create_test(while_update_statement)
