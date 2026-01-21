message(STATUS "Using diagnostics: ${MOS_C_DIAGNOSTIC}")

# -- version --

file(STRINGS ${PROJECT_SOURCE_DIR}/VERSION TESS_VERSION)
set(VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/version.h)

# Get git hash at configure time (will be "unknown" if not in a git repo)
execute_process(
  COMMAND git rev-parse --short=7 HEAD
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE GIT_RESULT
)
if(NOT GIT_RESULT EQUAL 0)
  set(GIT_HASH "unknown")
endif()

# Generate version header at configure time
file(WRITE ${VERSION_HEADER}
"/* Auto-generated version header */
#ifndef TESS_VERSION_H
#define TESS_VERSION_H

#define TESS_VERSION \"${TESS_VERSION}-${GIT_HASH}\"

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
  ${PROJECT_SOURCE_DIR}/src/tess/include/type_registry.h
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
target_link_libraries(tess PUBLIC mos)
target_link_options(tess PRIVATE ${MOS_C_LINK_OPTIONS})

# -- exe --

add_executable(tess_exe src/tess_exe.c)
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

# -- tesslang tests --

function(tesslang_create_test NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND
    tess_exe
    exe
    --no-standard-includes
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
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endfunction()

function(tesslang_create_test_optimized NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND
    tess_exe
    exe
    -O
    --no-standard-includes
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
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endfunction()

function(tesslang_create_build_fail_test NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_test(NAME ${TEST_NAME}
    COMMAND
    $<TARGET_FILE:tess_exe>
    exe
    --no-standard-includes
    -I ${CMAKE_CURRENT_SOURCE_DIR}
    -I ${PROJECT_SOURCE_DIR}/src/tl/std
    -o ${TEST_EXE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_property(TEST ${TEST_NAME} PROPERTY WILL_FAIL true)
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endfunction()

tesslang_create_test(address_of)
tesslang_create_test(alignof)
tesslang_create_test(alloc_align)
tesslang_create_test(alloc_allocators)
tesslang_create_test(anon_lambda)
tesslang_create_test(apply_generic)
tesslang_create_test(apply_generic_through_pointer)
tesslang_create_test(apply_lambda)
tesslang_create_test(arithmetic_unary_op)
tesslang_create_test(array_index_binary_op)
tesslang_create_test(assignment_by_op)
tesslang_create_test(atexit)
tesslang_create_test(_Exit)
tesslang_create_test(binop)
tesslang_create_test(bitwise_operators)
tesslang_create_test(c_div)
tesslang_create_test(c_symbol_annotation)
tesslang_create_test(c_struct)
tesslang_create_test(c_timespec)
tesslang_create_test(carray)
tesslang_create_test(case_basic_else)
tesslang_create_test(case_pred_ident)
tesslang_create_test(case_pred_lambda)
tesslang_create_test(char_literal)
tesslang_create_test(closure_fun_ptr)
tesslang_create_test(compound_assignment)
tesslang_create_test(defun_inline_type)
tesslang_create_test(dynamic_array)
tesslang_create_test(enum_no_module)
tesslang_create_test(enum_module)
tesslang_create_test(embed_c)
tesslang_create_test(escape_sequences)
tesslang_create_test(if_basic)
tesslang_create_test(if_expression)
tesslang_create_test(integer_literals)
tesslang_create_test(factorial)
tesslang_create_test(fatal_intrinsic)
tesslang_create_test(float_scientific)
tesslang_create_test(for_break)
tesslang_create_test(for_continue)
tesslang_create_test(for_statement_basic)
tesslang_create_test(for_statement_module)
tesslang_create_test(forward_decl_not_needed)
tesslang_create_test(function_pointer_argument)
tesslang_create_test(function_pointer_value)
tesslang_create_test(function_pointer_in_struct)
tesslang_create_test(function_pointer_in_struct_direct)
tesslang_create_test(function_pointer_in_struct_direct_2)
tesslang_create_test(generic_lambda)
tesslang_create_test(global_variables)
tesslang_create_test(hello)
tesslang_create_test(lambda_basic)
tesslang_create_test(lambda_apply)
tesslang_create_test(lambda_immediate)
tesslang_create_test(lambda_immediate_type_argument)
tesslang_create_test(let_in_basic)
tesslang_create_test(let_in_expression)
tesslang_create_test(logical_or)
tesslang_create_test(malloc_free)
tesslang_create_test(malloc_free_is_null)
tesslang_create_test(malloc_struct_basic)
tesslang_create_test(mapper_basic)
tesslang_create_test(mapper_lambda)
tesslang_create_test(module_basic)
tesslang_create_test(module_init)
tesslang_create_test(mutual_recursion)
tesslang_create_test(mutual_recursion_module)
tesslang_create_test(mutual_recursion_module_apply)
tesslang_create_test(mutual_recursion_both_referenced)
tesslang_create_test(nested_struct_access)
tesslang_create_test(nested_lambda_context)
tesslang_create_test(number_separators)
tesslang_create_test(printf)
tesslang_create_test(pointer_array)
tesslang_create_test(pointer_cast)
tesslang_create_test(pointer_cast_struct)
tesslang_create_test(pointer_compare_null)
tesslang_create_test(pointer_deref)
tesslang_create_test(pointer_deref_double)
tesslang_create_test(reassign_into_stack_lambda)
tesslang_create_test(reassign_result)
tesslang_create_test(recursive_type)
tesslang_create_test(recursive_type_basic)
tesslang_create_test(recursive_type_cycle_3)
tesslang_create_test(recursive_type_generic)
tesslang_create_test(recursive_type_mutual)
tesslang_create_test(regress_type_cons)
tesslang_create_test(relational_basic)
tesslang_create_test(return_statement)
tesslang_create_test(return_null)
tesslang_create_test(scope_shadow)
tesslang_create_test(sizeof)
tesslang_create_test(sizeof_type_literal)
tesslang_create_test(static_init)
tesslang_create_test(static_init_struct)
tesslang_create_test(static_init_struct_fun_ptr)
tesslang_create_test(strcmp)
tesslang_create_test(struct_concrete)
tesslang_create_test(struct_empty)
tesslang_create_test(struct_generic)
tesslang_create_test(struct_construction)
tesslang_create_test_optimized(tail_call)
tesslang_create_test(type_assertion)
tesslang_create_test(type_assertion_field)
tesslang_create_test(tagged_union)
tesslang_create_test(tagged_union_generic_basic)
tesslang_create_test(tagged_union_generic_case)
tesslang_create_test(tagged_union_generic_func)
tesslang_create_test(tagged_union_generic_multi)
tesslang_create_test(tagged_union_generic_nested)
tesslang_create_test(tagged_union_generic_param)
tesslang_create_test(tagged_union_generic_return)
tesslang_create_test(tagged_union_mutable_case)
tesslang_create_test(tagged_union_option)
tesslang_create_test(type_literal_generic)
tesslang_create_test(type_arguments_annotations)
tesslang_create_test(type_argument_field_annotation)
tesslang_create_test(type_alias_generic)
tesslang_create_test(type_alias_local)
tesslang_create_test(type_alias_local_direct)
tesslang_create_test(type_alias_module_chained)
tesslang_create_test(type_alias_module_chained_direct)
tesslang_create_test(type_alias_module_enum)
tesslang_create_test(type_alias_module_enum_direct)
tesslang_create_test(type_alias_module_multi_arg)
tesslang_create_test(type_alias_module_multi_arg_direct)
tesslang_create_test(type_alias_module_multi_arg_direct_compatible)
tesslang_create_test(type_alias_module_simple)
tesslang_create_test(type_alias_module_simple_direct)
tesslang_create_test(types_float)
tesslang_create_test(types_integer)
tesslang_create_test(types_integer_cast)
tesslang_create_test(union_basic)
tesslang_create_test(union_module_intermediate)
tesslang_create_test(union_module_second_variant)
tesslang_create_test(uninitialized_fields)
tesslang_create_test(while_break)
tesslang_create_test(while_continue)
tesslang_create_test(while_statement)
tesslang_create_test(while_update_statement)

# Expected failures
tesslang_create_build_fail_test(case_float)
tesslang_create_build_fail_test(fail_monkey_patch)
tesslang_create_build_fail_test(fail_tagged_union_missing_case)
tesslang_create_build_fail_test(fail_tagged_union_unknown_variant)
tesslang_create_build_fail_test(type_alias_partial_specialization)
tesslang_create_build_fail_test(unknown_free_variable)
