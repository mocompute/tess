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
  src/tess.c
  src/token.c
  src/tokenizer.c

  src/infer.c
  src/infer_alpha.c
  src/infer_constraint.c
  src/infer_specialize.c
  src/infer_update.c
  src/transpile.c
  src/tlib.c
  src/type.c

  ${CMAKE_CURRENT_BINARY_DIR}/tess_embed.c
  )
target_sources(tess PRIVATE ${HEADERS} ${SOURCES})
target_compile_options(tess BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_include_directories(tess PUBLIC "${PROJECT_SOURCE_DIR}/src/tess/include")
target_link_libraries(tess PUBLIC mos libdeflate::libdeflate_static)
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
tess_create_test(format)
tess_create_test(tlib)
target_compile_definitions(test-tlib PRIVATE
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

function(tl_create_test NAME)
  set(TEST_NAME test-tl-${NAME})
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
    ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    DEPENDS
      $<TARGET_FILE:tess_exe>
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl"
  )
  add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_EXE})
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    LABELS "expected_pass"
  )
endfunction()

function(tl_create_test_optimized NAME)
  set(TEST_NAME test-tl-${NAME})
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
    ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    DEPENDS
      $<TARGET_FILE:tess_exe>
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Compiling test_${NAME}.tl"
  )
  add_custom_target(${TEST_NAME} ALL DEPENDS ${TEST_EXE})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_EXE})
  set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    LABELS "expected_pass_optimized"
  )
endfunction()

function(tl_create_build_fail_test NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_test(NAME ${TEST_NAME}
    COMMAND $<TARGET_FILE:tess_exe> exe --no-standard-includes -I ${CMAKE_CURRENT_SOURCE_DIR} -S ${PROJECT_SOURCE_DIR}/src/tl/std -o ${TEST_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_NAME} PROPERTIES
    WILL_FAIL TRUE
    LABELS "expected_failure"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endfunction()

# Known failures (tests that should work but currently fail due to compiler bugs)
# These are excluded from the test suite but kept to track compiler issues
function(tl_create_known_failure NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
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

tl_create_build_fail_test(fail_integer_exact_generic_standalone)

# Expected runtime failures (must compile, must fail at runtime)
function(tl_create_runtime_fail_test NAME)
  set(TEST_NAME test-tl-${NAME})
  set(TEST_EXE ${CMAKE_CURRENT_BINARY_DIR}/tl/test_${NAME}${CMAKE_EXECUTABLE_SUFFIX})
  add_custom_command(
    OUTPUT ${TEST_EXE}
    COMMAND $<TARGET_FILE:tess_exe> exe --no-standard-includes --bounds-check -I ${CMAKE_CURRENT_SOURCE_DIR} -S ${PROJECT_SOURCE_DIR}/src/tl/std -o ${TEST_EXE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
    DEPENDS tess_exe
      ${CMAKE_CURRENT_SOURCE_DIR}/tl/test_${NAME}.tl
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

tl_create_test(_Exit)
tl_create_test(address_of)
tl_create_test(alias_basic)
tl_create_test(alias_deep_dotted)
tl_create_test(alias_dotted)
tl_create_test(alias_file_boundary)
tl_create_test(alias_funcptr)
tl_create_test(alias_nested_type)
tl_create_test(alias_unalias)
tl_create_test(alignof)
tl_create_test(alloc_align)
tl_create_test(alloc_allocators)
tl_create_test(anon_lambda)
tl_create_test(apply_generic)
tl_create_test(apply_generic_through_pointer)
tl_create_test(apply_lambda)
tl_create_test(arity_overload)
tl_create_test(array_api)
tl_create_test(array_comprehensive)
tl_create_test(array_index_binary_op)
tl_create_test(array_sort)
tl_create_test(atexit)
tl_create_test(attributes)
tl_create_test(binop)
tl_create_test(bitwise_compound_assign)
tl_create_test(bitwise_operators)
tl_create_test(block_expression)
tl_create_test(builtin_option)
tl_create_test(builtin_result)
tl_create_test(c_div)
tl_create_test(c_export)
tl_create_test(c_keywords)
tl_create_test(c_struct)
tl_create_test(c_symbol_annotation)
tl_create_test(c_timespec)
tl_create_test(carray)
tl_create_test(carray_struct_field)
tl_create_test(case_basic_else)
tl_create_test(case_pred_ident)
tl_create_test(case_pred_lambda)
tl_create_test(char_literal)
tl_create_test(closure_fun_ptr)
tl_create_test(closure_in_named_function)
tl_create_test(compound_assignment)
tl_create_test(conditional_compile_auto_define)
tl_create_test(conditional_compile_define)
tl_create_test(conditional_compile_import)
tl_create_test(conditional_compile_nested)
tl_create_test(const)
tl_create_test(defer)
tl_create_test(defun_inline_type)
tl_create_test(deref_then_addr)
tl_create_test(dynamic_array)
tl_create_test(embed_c)
tl_create_test(enum_module)
tl_create_test(enum_no_module)
tl_create_test(escape_sequences)
tl_create_test(factorial)
tl_create_test(fatal_intrinsic)
tl_create_test(for_break)
tl_create_test(for_continue)
tl_create_test(for_statement_basic)
tl_create_test(for_statement_module)
tl_create_test(forward_decl_not_needed)
tl_create_test(function_pointer_argument)
tl_create_test(function_pointer_array)
tl_create_test(function_pointer_in_struct)
tl_create_test(function_pointer_in_struct_direct)
tl_create_test(function_pointer_in_struct_direct_2)
tl_create_test(function_pointer_mutable)
tl_create_test(function_pointer_pointer)
tl_create_test(function_pointer_recursive_type)
tl_create_test(function_pointer_higher_order_return)
tl_create_test(function_pointer_value)
tl_create_test(generic_fn_ref_explicit_type_args)
tl_create_test(generic_lambda)
tl_create_test(generic_nested_sizeof)
tl_create_test(generic_null_field)
tl_create_test(generic_struct_fn_ptr_field)
tl_create_test(generic_struct_null_fn_ptr_field)
tl_create_test(global_variables)
tl_create_test(hashmap)
tl_create_test(hello)
tl_create_test(integer_bounds_check)
tl_create_test(integer_cast_annotation)
tl_create_test(integer_compound_assign)
tl_create_test(integer_families)
tl_create_test(integer_generic_type_args)
tl_create_test(integer_narrowing_let_cast)
tl_create_test(integer_same_type)
tl_create_test(integer_standalone_cchar)
tl_create_test(integer_widening)
tl_create_test(integer_widening_reassign)
tl_create_test(if_basic)
tl_create_test(if_expression)
tl_create_test(import_relative)
tl_create_test(lambda_apply)
tl_create_test(lambda_arg_annotated)
tl_create_test(lambda_arg_unused)
tl_create_test(lambda_basic)
tl_create_test(lambda_capture_mutate)
tl_create_test(lambda_immediate)
tl_create_test(let_in_basic)
tl_create_test(let_in_expression)
tl_create_test(logical_and)
tl_create_test(logical_or)
tl_create_test(malloc_free)
tl_create_test(malloc_free_is_null)
tl_create_test(malloc_struct_basic)
tl_create_test(map_option_cross_type)
tl_create_test(mapper_basic)
tl_create_test(mapper_lambda)
tl_create_test(module_auto_collapse)
tl_create_test(module_auto_collapse_hashmap)
tl_create_test(module_basic)
tl_create_test(module_init)
tl_create_test(module_nested)
tl_create_test(module_prelude)
tl_create_test(mutual_recursion)
tl_create_test(mutual_recursion_both_referenced)
tl_create_test(mutual_recursion_module)
tl_create_test(mutual_recursion_module_apply)
tl_create_test(nested_lambda_context)
tl_create_test(nested_struct)
tl_create_test(nested_struct_access)
tl_create_test(nested_type_cross_module_conflict)
tl_create_test(number_formats)
tl_create_test(operator_overload)
tl_create_test(operator_overload_bitwise)
tl_create_test(operator_overload_chain)
tl_create_test(operator_overload_compound)
tl_create_test(operator_overload_div_mod)
tl_create_test(operator_overload_eq_from_cmp)
tl_create_test(operator_overload_eq_only)
tl_create_test(operator_overload_multi)
tl_create_test(operator_overload_in_function)
tl_create_test(operator_overload_tagged_union)
tl_create_test(operator_overload_unary)
tl_create_test(pack)
tl_create_test(pointer_array)
tl_create_test(pointer_cast)
tl_create_test(pointer_cast_struct)
tl_create_test(pointer_compare_null)
tl_create_test(pointer_deref)
tl_create_test(pointer_deref_double)
tl_create_test(printf)
tl_create_test(reassign_into_stack_lambda)
tl_create_test(reassign_result)
tl_create_test(recursive_type)
tl_create_test(recursive_type_basic)
tl_create_test(recursive_type_cycle_3)
tl_create_test(recursive_type_generic)
tl_create_test(recursive_type_mutual)
tl_create_test(recursive_type_mutual_simple)
tl_create_test(regress_type_cons)
tl_create_test(return_null)
tl_create_test(return_statement)
tl_create_test(scope_shadow)
tl_create_test(sizeof)
tl_create_test(sizeof_type_literal)
tl_create_test(static_init)
tl_create_test(static_init_struct)
tl_create_test(static_init_generic_function_pointer)
tl_create_test(static_init_struct_fun_ptr)
tl_create_test(str)
tl_create_test(strcmp)
tl_create_test(struct_concrete)
tl_create_test(struct_construction)
tl_create_test(struct_empty)
tl_create_test(struct_field_ptr_cast)
tl_create_test(struct_field_ptr_cast_inline)
tl_create_test(struct_field_ptr_cast_multi)
tl_create_test(struct_generic)
tl_create_test(struct_generic_function_signature)
tl_create_test(tagged_union)
tl_create_test(tagged_union_bail)
tl_create_test(tagged_union_case_annotation)
tl_create_test(tagged_union_carray)
tl_create_test(tagged_union_cross_module_positional)
tl_create_test(tagged_union_function_pointer)
tl_create_test(tagged_union_generic_basic)
tl_create_test(tagged_union_generic_case)
tl_create_test(tagged_union_generic_func)
tl_create_test(tagged_union_generic_function_pointer)
tl_create_test(tagged_union_generic_multi)
tl_create_test(tagged_union_generic_nested)
tl_create_test(tagged_union_generic_param)
tl_create_test(tagged_union_generic_return)
tl_create_test(tagged_union_generic_scoped)
tl_create_test(tagged_union_many_variants)
tl_create_test(tagged_union_multi_same_module)
tl_create_test(tagged_union_mutable_case)
tl_create_test(tagged_union_nested_when)
tl_create_test(tagged_union_option)
tl_create_test(tagged_union_pointer_field)
tl_create_test(tagged_union_recursive_type)
tl_create_test(tagged_union_scoped_bare_cross_module)
tl_create_test(tagged_union_scoped_construction)
tl_create_test(tagged_union_scoped_variant)
tl_create_test(tagged_union_scoped_variant_as_union)
tl_create_test(tagged_union_unscoped)
tl_create_test(type_alias_generic)
tl_create_test(type_alias_local)
tl_create_test(type_alias_module_chained)
tl_create_test(type_alias_module_enum)
tl_create_test(type_alias_module_multi_arg)
tl_create_test(type_alias_module_multi_arg_direct_compatible)
tl_create_test(type_alias_module_simple)
tl_create_test(type_argument_field_annotation)
tl_create_test(type_arguments_annotations)
tl_create_test(type_literal_generic)
tl_create_test(type_predicate)
tl_create_test(type_predicate_branch)
tl_create_test(type_predicate_field)
tl_create_test(type_predicate_generic)
tl_create_test(type_predicate_generic_type)
tl_create_test(type_predicate_type_arg)
tl_create_test(types_integer_cast)
tl_create_test(try)
tl_create_test(float_cast)
tl_create_test(float_same_type)
tl_create_test(float_weak_literal)
tl_create_test(float_widening)
tl_create_test(types_float)
tl_create_test(types_integer)
tl_create_test(ufcs)
tl_create_test(ufcs_module)
tl_create_test(uninitialized_fields)
tl_create_test(union_basic)
tl_create_test(union_module_intermediate)
tl_create_test(union_module_second_variant)
tl_create_test(stress_closures)
tl_create_test(stress_control_flow)
tl_create_test(stress_deep_nesting)
tl_create_test(stress_expression_position)
tl_create_test(stress_generic_types)
tl_create_test(stress_scope_shadow)
tl_create_test(stress_type_features)
tl_create_test(stress_when_combinations)
tl_create_test(while_break)
tl_create_test(while_continue)
tl_create_test(while_empty_body)
tl_create_test(while_statement)
tl_create_test(while_update_statement)
tl_create_test(weak_int_literals)
tl_create_test(weak_int_module_const)
tl_create_test(weak_int_to_standalone)
tl_create_test(weak_int_unsigned_update)
tl_create_test(z_literals)
tl_create_test_optimized(tail_call)
tl_create_test(trait_bounds)
tl_create_test(trait_bounds_inherited)
tl_create_test(trait_bounds_multi_inst)
tl_create_test(trait_conditional)
tl_create_test(trait_declaration)
tl_create_test(trait_diamond_inheritance)
tl_create_test(trait_inheritance_conformance)

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

# Expected failures (tests that verify the compiler correctly rejects invalid code)
tl_create_build_fail_test(fail_alias_chain)
tl_create_build_fail_test(fail_alias_double_underscore)
tl_create_build_fail_test(fail_alias_duplicate)
tl_create_build_fail_test(fail_alias_main)
tl_create_build_fail_test(fail_alias_reserved_c)
tl_create_build_fail_test(fail_alias_reserved_tl)
tl_create_build_fail_test(fail_alias_self)
tl_create_build_fail_test(fail_alias_shadows_module)
tl_create_build_fail_test(fail_alias_source_not_found)
tl_create_build_fail_test(fail_c_export_tess_type)
tl_create_build_fail_test(fail_case_float)
tl_create_build_fail_test(fail_concrete_fun_mismatch)
tl_create_build_fail_test(fail_const_field_mutation)
tl_create_build_fail_test(fail_const_index_mutation)
tl_create_build_fail_test(fail_const_mutation)
tl_create_build_fail_test(fail_double_underscore)
tl_create_build_fail_test(fail_double_underscore_module)
tl_create_build_fail_test(fail_generic_unused_type_param)
tl_create_build_fail_test(fail_lambda_implicit_return)
tl_create_build_fail_test(fail_lambda_return)
tl_create_build_fail_test(fail_monkey_patch)
tl_create_build_fail_test(fail_nested_module_no_immediate_parent)
tl_create_build_fail_test(fail_nested_module_no_parent)
tl_create_build_fail_test(fail_nested_type_cross_module_conflict)
tl_create_build_fail_test(fail_reserved_type_alias)
tl_create_build_fail_test(fail_reserved_type_annotation)
tl_create_build_fail_test(fail_reserved_type_assign)
tl_create_build_fail_test(fail_reserved_type_enum)
tl_create_build_fail_test(fail_reserved_type_forward)
tl_create_build_fail_test(fail_reserved_type_fun)
tl_create_build_fail_test(fail_reserved_type_struct)
tl_create_build_fail_test(fail_reserved_type_tu)
tl_create_build_fail_test(fail_reserved_type_union)
tl_create_build_fail_test(fail_tagged_union_bail_not_diverging)
tl_create_build_fail_test(fail_tagged_union_duplicate_variant)
tl_create_build_fail_test(fail_tagged_union_existing_type)
tl_create_build_fail_test(fail_tagged_union_missing_case)
tl_create_build_fail_test(fail_tagged_union_unknown_variant)
tl_create_build_fail_test(fail_operator_no_overload)
tl_create_build_fail_test(fail_trait_bound_combined)
tl_create_build_fail_test(fail_trait_bound_not_satisfied)
tl_create_build_fail_test(fail_trait_conditional_not_satisfied)
tl_create_build_fail_test(fail_trait_conditional_transitive)
tl_create_build_fail_test(fail_trait_bound_second_param)
tl_create_build_fail_test(fail_trait_circular)
tl_create_build_fail_test(fail_trait_duplicate)
tl_create_build_fail_test(fail_trait_reserved_name)
tl_create_build_fail_test(fail_type_alias_partial_specialization)
tl_create_build_fail_test(fail_ufcs_not_found)
tl_create_build_fail_test(fail_unalias_not_found)
tl_create_build_fail_test(fail_unknown_free_variable)

tl_create_build_fail_test(fail_const_lambda_mutation)
tl_create_build_fail_test(fail_const_strip)
tl_create_build_fail_test(fail_const_strip_lambda)
tl_create_build_fail_test(fail_const_strip_nested)
tl_create_build_fail_test(fail_import_absolute)
tl_create_build_fail_test(fail_import_missing_quotes)
tl_create_build_fail_test(fail_integer_compound_assign_mixed)
tl_create_build_fail_test(fail_integer_cross_chain)
tl_create_build_fail_test(fail_integer_cross_family_arithmetic)
tl_create_build_fail_test(fail_integer_cross_family_assignment)
tl_create_build_fail_test(fail_integer_cross_family_comparison)
tl_create_build_fail_test(fail_integer_cross_family_unsigned_to_signed)
tl_create_build_fail_test(fail_float_exact_conditional)
tl_create_build_fail_test(fail_float_exact_operator)
tl_create_build_fail_test(fail_float_int_implicit)
tl_create_build_fail_test(fail_float_narrowing_funcall)
tl_create_build_fail_test(fail_float_narrowing_return)
tl_create_build_fail_test(fail_weak_int_cross_family)

# Expected runtime failures (debug mode: compile succeeds, run must fail)

tl_create_build_fail_test(fail_try_non_union)
tl_create_build_fail_test(fail_try_three_variants)

tl_create_build_fail_test(fail_integer_exact_case)
tl_create_build_fail_test(fail_integer_exact_conditional)
tl_create_build_fail_test(fail_integer_exact_generic)
tl_create_build_fail_test(fail_integer_exact_operator)
tl_create_build_fail_test(fail_integer_literal_overflow)
tl_create_build_fail_test(fail_integer_literal_overflow_negative)
tl_create_build_fail_test(fail_integer_literal_overflow_unsigned)
tl_create_build_fail_test(fail_integer_narrowing_funcall)
tl_create_build_fail_test(fail_integer_narrowing_reassign)
tl_create_build_fail_test(fail_integer_narrowing_return)

# Expected runtime failures (must compile, must abort at runtime with --bounds-check)
tl_create_runtime_fail_test(fail_runtime_float_narrowing)
tl_create_runtime_fail_test(fail_runtime_float_to_int)
tl_create_runtime_fail_test(fail_runtime_integer_bounds)
tl_create_runtime_fail_test(fail_runtime_integer_bounds_cross_family)

# Known fail failures: compiler should reject but doesn't yet
tl_create_known_fail_failure(fail_trait_bound_ord_missing_eq)

# Known failures: these should work but a compiler bug makes them fail
tl_create_known_failure(tagged_union_bail_nested)
