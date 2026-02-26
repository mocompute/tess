# ==============================================================================
# Tess Language - Makefile
# ==============================================================================

# ------------------------------------------------------------------------------
# Build Configuration
# ------------------------------------------------------------------------------

CONFIG ?= release
ASAN_OPTIONS ?= detect_leaks=1

ifeq ($(CONFIG),release)
  CFLAGS_CONFIG = -O2 -DNDEBUG
  LDFLAGS_CONFIG =
  BUILD_DIR = build-release
else ifeq ($(CONFIG),debug)
  CFLAGS_CONFIG = -g -DDEBUG -fno-omit-frame-pointer
  LDFLAGS_CONFIG =
  BUILD_DIR = build-debug
else ifeq ($(CONFIG),asan)
  CFLAGS_CONFIG = -O -g -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS_CONFIG = -fsanitize=address,undefined
  BUILD_DIR = build-asan
else
  $(error Unknown CONFIG: $(CONFIG). Valid options: release, debug, asan)
endif

# ------------------------------------------------------------------------------
# Compiler and Flags
# ------------------------------------------------------------------------------

CC     ?= cc
CFLAGS += $(CFLAGS_CONFIG)
CFLAGS += -std=gnu11 -fPIE
CFLAGS += -Werror -Wall -Wextra -Wswitch-enum -Wunused -Winline -Wimplicit-fallthrough

# Test if the compiler supports a given flag (substitute -no- flags)
check_flag = $(shell $(CC) -Werror $(patsubst -Wno-%,-W%,$(1)) -x c -c /dev/null -o /dev/null 2>/dev/null && echo $(1))

CLANG_FLAGS := -Wno-gnu-alignof-expression
CFLAGS += $(foreach flag,$(CLANG_FLAGS),$(call check_flag,$(flag)))

CPPFLAGS = -I$(MOS_INC_DIR) -I$(TESS_INC_DIR) -I$(LIBDEFLATE_DIR)
LDFLAGS ?=
LDFLAGS += $(LDFLAGS_CONFIG)


# ------------------------------------------------------------------------------
# Directories
# ------------------------------------------------------------------------------

MOS_SRC_DIR  = src/mos
MOS_INC_DIR  = $(MOS_SRC_DIR)/include
TESS_SRC_DIR = src/tess
TESS_INC_DIR = $(TESS_SRC_DIR)/include

# Installation
PREFIX      ?= /usr/local
INSTALL_BIN  = $(DESTDIR)$(PREFIX)/bin
INSTALL_LIB  = $(DESTDIR)$(PREFIX)/lib/tess

# Test directories
TL_TEST_DIR  = $(TESS_SRC_DIR)/tl
TL_STD_DIR   = src/tl/std
TL_BUILD_DIR = $(BUILD_DIR)/tl

# ------------------------------------------------------------------------------
# Output Control
# ------------------------------------------------------------------------------

V ?= 0
ifeq ($(V),0)
  Q = @
  STDERR    = >/dev/null 2>&1
else
  Q =
  STDERR    =
endif

MSG_CC    = @printf "  \033[1;34m[CC]\033[0m     %s\n"
MSG_LD    = @printf "  \033[1;32m[LD]\033[0m     %s\n"
MSG_GEN   = @printf "  \033[1;33m[GEN]\033[0m    %s\n"
MSG_TEST  = printf  "  \033[1;34m[TEST]\033[0m   %s\n"
MSG_FAIL  = printf  "  \033[1;31m[FAIL]\033[0m   %s\n"
MSG_PASS  = printf  "  \033[1;32m[PASS]   %s\033[0m\n"
MSG_FAIL2 = printf  "  \033[1;31m[FAIL]   %s (expected build failure)\033[0m\n"


# ------------------------------------------------------------------------------
# mos Library
# ------------------------------------------------------------------------------

MOS_SOURCES =					\
	$(MOS_SRC_DIR)/src/alloc.c		\
	$(MOS_SRC_DIR)/src/array.c		\
	$(MOS_SRC_DIR)/src/dbg.c		\
	$(MOS_SRC_DIR)/src/hash.c		\
	$(MOS_SRC_DIR)/src/file.c		\
	$(MOS_SRC_DIR)/src/hashmap.c		\
	$(MOS_SRC_DIR)/src/platform.c		\
	$(MOS_SRC_DIR)/src/sexp.c		\
	$(MOS_SRC_DIR)/src/sexp_parser.c	\
	$(MOS_SRC_DIR)/src/str.c

MOS_OBJECTS = $(patsubst $(MOS_SRC_DIR)/src/%.c,$(BUILD_DIR)/mos/%.o,$(MOS_SOURCES))

$(BUILD_DIR)/mos/%.o: $(MOS_SRC_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Library
# ------------------------------------------------------------------------------

TESS_SOURCES =				\
	$(TESS_SRC_DIR)/src/ast.c	\
	$(TESS_SRC_DIR)/src/error.c	\
	$(TESS_SRC_DIR)/src/format.c	\
	$(TESS_SRC_DIR)/src/import_resolver.c \
	$(TESS_SRC_DIR)/src/parser.c	\
	$(TESS_SRC_DIR)/src/tess.c	\
	$(TESS_SRC_DIR)/src/token.c	\
	$(TESS_SRC_DIR)/src/tokenizer.c \
	$(TESS_SRC_DIR)/src/infer.c	\
	$(TESS_SRC_DIR)/src/transpile.c \
	$(TESS_SRC_DIR)/src/manifest.c \
	$(TESS_SRC_DIR)/src/source_scanner.c \
	$(TESS_SRC_DIR)/src/tlib.c	\
	$(TESS_SRC_DIR)/src/type.c

TESS_OBJECTS = $(patsubst $(TESS_SRC_DIR)/src/%.c,$(BUILD_DIR)/tess/%.o,$(TESS_SOURCES))

$(BUILD_DIR)/tess/%.o: $(TESS_SRC_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# Embed Tool and Generated Sources
# ------------------------------------------------------------------------------

EMBED_TOOL     = $(BUILD_DIR)/mos_embed
TESS_EMBED_SRC = $(BUILD_DIR)/tess_embed.c
TESS_EMBED_OBJ = $(BUILD_DIR)/tess/tess_embed.o

# Version header generation
VERSION_HEADER = $(BUILD_DIR)/version.h
VERSION := $(shell cat VERSION)

$(VERSION_HEADER): VERSION
	@mkdir -p $(dir $@)
	$(MSG_GEN) $@
	$(Q)( \
		HASH=$$(git rev-parse --short=7 HEAD 2>/dev/null || echo "unknown"); \
		echo "/* Auto-generated version header */" > $@; \
		echo "#ifndef TESS_VERSION_H" >> $@; \
		echo "#define TESS_VERSION_H" >> $@; \
		echo "" >> $@; \
		echo "#define TESS_VERSION \"$(VERSION)-$$HASH\"" >> $@; \
		echo "" >> $@; \
		echo "#endif /* TESS_VERSION_H */" >> $@; \
	)

$(EMBED_TOOL): $(MOS_SRC_DIR)/src/embed.c $(MOS_OBJECTS)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

$(TESS_EMBED_SRC): $(EMBED_TOOL) $(TESS_SRC_DIR)/embed/std.c $(TESS_SRC_DIR)/embed/prelude.tl
	$(MSG_GEN) $<
	$(Q)$(EMBED_TOOL) $(TESS_SRC_DIR)/embed $(TESS_EMBED_SRC) $(STDERR)

$(TESS_EMBED_OBJ): $(TESS_EMBED_SRC)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# libdeflate (vendored)
# ------------------------------------------------------------------------------

LIBDEFLATE_DIR = vendor/libdeflate-1.25
LIBDEFLATE_SOURCES = \
	$(LIBDEFLATE_DIR)/lib/arm/cpu_features.c \
	$(LIBDEFLATE_DIR)/lib/x86/cpu_features.c \
	$(LIBDEFLATE_DIR)/lib/utils.c \
	$(LIBDEFLATE_DIR)/lib/deflate_compress.c \
	$(LIBDEFLATE_DIR)/lib/deflate_decompress.c \
	$(LIBDEFLATE_DIR)/lib/crc32.c
LIBDEFLATE_OBJECTS = $(patsubst $(LIBDEFLATE_DIR)/%.c,$(BUILD_DIR)/libdeflate/%.o,$(LIBDEFLATE_SOURCES))
LIBDEFLATE_CFLAGS = -std=gnu11 -O2 -fPIE -I$(LIBDEFLATE_DIR)

$(BUILD_DIR)/libdeflate/%.o: $(LIBDEFLATE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(LIBDEFLATE_CFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Executable
# ------------------------------------------------------------------------------

TESS_EXE_SRC = $(TESS_SRC_DIR)/src/tess_exe.c
TESS_EXE_OBJ = $(BUILD_DIR)/tess_exe.o
TESS_EXE     = $(BUILD_DIR)/bin/tess

$(TESS_EXE): $(TESS_EXE_OBJ) $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS) $(LIBDEFLATE_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TESS_EXE_OBJ): $(TESS_EXE_SRC) $(VERSION_HEADER)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -I$(BUILD_DIR) -c -o $@ $<


# ------------------------------------------------------------------------------
# Default Target
# ------------------------------------------------------------------------------

all: $(TESS_EXE) tess

tess: $(TESS_EXE)
	$(Q)cp $< $@

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------

install: $(TESS_EXE)
	install -D -m 755 $(TESS_EXE) $(INSTALL_BIN)/tess
	@mkdir -p $(INSTALL_LIB)/std
	find src/tl/std -name '*.tl' -exec install -D -m 644 {} $(INSTALL_LIB)/std/ \;

# ------------------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) tess

cleanall:
	rm -rf build-release build-debug build-asan tess

# ==============================================================================
# Tests
# ==============================================================================

# $(1): test suite name (for display)
# $(2): list of test executables
# $(3): test count
define run_test_suite
	@failed=0; \
	export ASAN_OPTIONS=$(ASAN_OPTIONS); \
	for test in $(2); do \
		name=$$(basename $$test); \
		$(MSG_TEST) $$name; \
		if ! $$test $(STDERR); then \
			printf "  \033[1;31m[FAIL] $$name\033[0m\n"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $(3) $(1) tests\n"; \
	if [ $$failed -gt 0 ]; then \
		printf "  \033[1;31m[FAIL] $$failed $(1) test(s) failed\033[0m\n"; \
		exit 1; \
	fi; \
	printf "\n" ; \
	$(MSG_PASS) "All $(1) tests passed"; \
	printf "\n"
endef

# ------------------------------------------------------------------------------
# mos Library Tests
# ------------------------------------------------------------------------------

MOS_TESTS      = alloc array file map sexp str types util
MOS_BENCHMARKS = hash
MOS_TEST_EXES      = $(patsubst %,$(BUILD_DIR)/test_mos_%,$(MOS_TESTS))
MOS_BENCHMARK_EXES = $(patsubst %,$(BUILD_DIR)/test_mos_%,$(MOS_BENCHMARKS))

$(BUILD_DIR)/test_mos_%: $(MOS_SRC_DIR)/src/test_%.c $(MOS_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

build-mos-tests: $(MOS_TEST_EXES)

build-mos-benchmarks: $(MOS_BENCHMARK_EXES)

bench-mos: build-mos-benchmarks
	$(call run_test_suite,mos benchmarks,$(MOS_BENCHMARK_EXES),$(words $(MOS_BENCHMARKS)))

test-mos: build-mos-tests
	$(call run_test_suite,mos,$(MOS_TEST_EXES),$(words $(MOS_TESTS)))

# ------------------------------------------------------------------------------
# tess Compiler Tests
# ------------------------------------------------------------------------------

TESS_TESTS     = tess type_v2 format tlib import_resolver manifest source_scanner
TESS_TEST_EXES = $(patsubst %,$(BUILD_DIR)/test_%,$(TESS_TESTS))

$(BUILD_DIR)/test_%: $(TESS_SRC_DIR)/src/test_%.c $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS) $(LIBDEFLATE_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

build-tess-tests: $(TESS_TEST_EXES)

test-tess: build-tess-tests
	$(call run_test_suite,tess,$(TESS_TEST_EXES),$(words $(TESS_TESTS)))

# ------------------------------------------------------------------------------
# Vendor Tests
# ------------------------------------------------------------------------------

VENDOR_TESTS     = deflate
VENDOR_TEST_EXES = $(patsubst %,$(BUILD_DIR)/test_vendor_%,$(VENDOR_TESTS))

$(BUILD_DIR)/test_vendor_%: $(TESS_SRC_DIR)/src/test_%.c $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS) $(LIBDEFLATE_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

build-vendor-tests: $(VENDOR_TEST_EXES)

test-vendor: build-vendor-tests
	$(call run_test_suite,vendor,$(VENDOR_TEST_EXES),$(words $(VENDOR_TESTS)))

# ------------------------------------------------------------------------------
# Tesslang (.tl) Tests
# ------------------------------------------------------------------------------

TL_TESTS =					\
	_Exit					\
	address_of				\
	alias_basic				\
	alias_deep_dotted			\
	alias_dotted				\
	alias_file_boundary			\
	alias_funcptr				\
	alias_nested_type			\
	alias_unalias				\
	alignof					\
	alloc_align				\
	alloc_allocators			\
	anon_lambda				\
	apply_generic				\
	apply_generic_through_pointer		\
	apply_lambda				\
	arity_overload				\
	array_api				\
	array_comprehensive			\
	array_index_binary_op			\
	array_sort				\
	atexit					\
	attributes				\
	binop					\
	bitwise_operators			\
	block_expression			\
	builtin_option				\
	builtin_result				\
	c_div					\
	c_export				\
	c_keywords				\
	c_struct				\
	c_symbol_annotation			\
	c_timespec				\
	carray					\
	carray_struct_field			\
	case_basic_else				\
	case_pred_ident				\
	case_pred_lambda			\
	char_literal				\
	closure_fun_ptr				\
	closure_in_named_function		\
	compound_assignment			\
	conditional_compile_auto_define		\
	conditional_compile_define		\
	conditional_compile_import		\
	conditional_compile_nested		\
	const					\
	defer					\
	defun_inline_type			\
	deref_then_addr				\
	dynamic_array				\
	embed_c					\
	enum_module				\
	enum_no_module				\
	escape_sequences			\
	factorial				\
	fatal_intrinsic				\
	for_break				\
	for_continue				\
	for_statement_basic			\
	for_statement_module			\
	forward_decl_not_needed			\
	function_pointer_argument		\
	function_pointer_array			\
	function_pointer_in_struct		\
	function_pointer_in_struct_direct	\
	function_pointer_in_struct_direct_2	\
	function_pointer_mutable		\
	function_pointer_pointer		\
	function_pointer_recursive_type		\
	function_pointer_value			\
	generic_lambda				\
	global_variables			\
	hello					\
	if_basic				\
	if_expression				\
	import_relative				\
	import_relative_dotdot			\
	integer_families			\
	integer_same_type			\
	integer_widening			\
	lambda_apply				\
	lambda_arg_annotated			\
	lambda_arg_unused			\
	lambda_basic				\
	lambda_capture_mutate			\
	lambda_immediate			\
	let_in_basic				\
	let_in_expression			\
	logical_and				\
	logical_or				\
	malloc_free				\
	malloc_free_is_null			\
	malloc_struct_basic			\
	mapper_basic				\
	mapper_lambda				\
	module_basic				\
	module_init				\
	module_nested				\
	module_prelude				\
	mutual_recursion			\
	mutual_recursion_both_referenced	\
	mutual_recursion_module			\
	mutual_recursion_module_apply		\
	nested_lambda_context			\
	nested_struct_access			\
	nested_type_cross_module_conflict	\
	number_formats				\
	pack					\
	pointer_array				\
	pointer_cast				\
	pointer_cast_struct			\
	pointer_compare_null			\
	pointer_deref				\
	pointer_deref_double			\
	printf					\
	reassign_into_stack_lambda		\
	reassign_result				\
	recursive_type				\
	recursive_type_basic			\
	recursive_type_cycle_3			\
	recursive_type_generic			\
	recursive_type_mutual			\
	recursive_type_mutual_simple		\
	regress_type_cons			\
	return_null				\
	return_statement			\
	scope_shadow				\
	sizeof					\
	sizeof_type_literal			\
	static_init				\
	static_init_struct			\
	static_init_struct_fun_ptr		\
	str					\
	strcmp					\
	struct_concrete				\
	struct_construction			\
	struct_empty				\
	struct_field_ptr_cast			\
	struct_field_ptr_cast_inline		\
	struct_field_ptr_cast_multi		\
	struct_generic				\
	struct_generic_function_signature	\
	tagged_union				\
	tagged_union_bail			\
	tagged_union_carray			\
	tagged_union_existing_type		\
	tagged_union_existing_type_main		\
	tagged_union_function_pointer		\
	tagged_union_generic_basic		\
	tagged_union_generic_case		\
	tagged_union_generic_func		\
	tagged_union_generic_function_pointer	\
	tagged_union_generic_multi		\
	tagged_union_generic_param		\
	tagged_union_make			\
	tagged_union_many_variants		\
	tagged_union_mutable_case		\
	tagged_union_nested_when		\
	tagged_union_option			\
	tagged_union_pointer_field		\
	tagged_union_recursive_type		\
	tagged_union_scoped_variant		\
	tagged_union_unscoped			\
	tail_call				\
	type_alias_generic			\
	type_alias_local			\
	type_alias_module_chained		\
	type_alias_module_enum			\
	type_alias_module_multi_arg		\
	type_alias_module_multi_arg_direct_compatible	\
	type_alias_module_simple		\
	type_argument_field_annotation		\
	type_arguments_annotations		\
	type_literal_generic			\
	type_predicate				\
	type_predicate_branch			\
	type_predicate_field			\
	type_predicate_generic			\
	type_predicate_type_arg			\
	try					\
	types_float				\
	types_integer				\
	types_integer_cast			\
	uninitialized_fields			\
	union_basic				\
	union_module_intermediate		\
	union_module_second_variant		\
	stress_closures				\
	stress_control_flow			\
	stress_deep_nesting			\
	stress_expression_position		\
	stress_generic_types			\
	stress_scope_shadow			\
	stress_type_features			\
	stress_when_combinations		\
	while_break				\
	while_continue				\
	while_statement				\
	weak_int_literals			\
	while_update_statement			\
	z_literals

TL_FAIL_TESTS =					\
	fail_alias_chain			\
	fail_alias_double_underscore		\
	fail_alias_duplicate			\
	fail_alias_main				\
	fail_alias_reserved_c			\
	fail_alias_reserved_tl			\
	fail_alias_self				\
	fail_alias_shadows_module		\
	fail_alias_source_not_found		\
	fail_case_float				\
	fail_c_export_tess_type			\
	fail_concrete_fun_mismatch		\
	fail_const_field_mutation		\
	fail_const_index_mutation		\
	fail_const_lambda_mutation		\
	fail_const_mutation			\
	fail_const_strip			\
	fail_const_strip_lambda			\
	fail_const_strip_nested			\
	fail_double_underscore			\
	fail_double_underscore_module		\
	fail_generic_unused_type_param		\
	fail_import_absolute			\
	fail_import_missing_quotes		\
	fail_integer_cross_family		\
	fail_integer_cross_family_arithmetic	\
	fail_integer_cross_family_assignment	\
	fail_integer_cross_family_comparison	\
	fail_integer_cross_family_unsigned_to_signed \
	fail_integer_exact_case			\
	fail_integer_exact_conditional		\
	fail_integer_exact_operator		\
	fail_integer_narrowing_funcall		\
	fail_integer_narrowing_let		\
	fail_integer_narrowing_reassign		\
	fail_integer_narrowing_return		\
	fail_lambda_implicit_return		\
	fail_lambda_return			\
	fail_monkey_patch			\
	fail_nested_module_no_immediate_parent	\
	fail_nested_module_no_parent		\
	fail_nested_type_cross_module_conflict	\
	fail_reserved_type_alias		\
	fail_reserved_type_annotation		\
	fail_reserved_type_assign		\
	fail_reserved_type_enum			\
	fail_reserved_type_forward		\
	fail_reserved_type_fun			\
	fail_reserved_type_struct		\
	fail_reserved_type_tu			\
	fail_reserved_type_union		\
	fail_tagged_union_bail_not_diverging	\
	fail_tagged_union_duplicate_variant	\
	fail_tagged_union_existing_type_bad_type_arg \
	fail_tagged_union_missing_case		\
	fail_tagged_union_unknown_variant	\
	fail_type_alias_partial_specialization	\
	fail_try_non_union			\
	fail_try_three_variants			\
	fail_unalias_not_found			\
	fail_unknown_free_variable		\
	fail_weak_int_cross_family		\
	fail_weak_int_to_standalone

# Expected runtime failure tests (debug only: must compile, must fail at runtime)
TL_FAIL_RUNTIME_TESTS =

# Expected-failure tests that the compiler doesn't reject yet
TL_KNOWN_FAIL_FAILURES =			\
	fail_integer_cross_chain

# Tests that should work but currently fail due to compiler bugs
TL_KNOWN_FAILURES =				\
	while_empty_body

# Total test count across all suites
TOTAL_TESTS = $(words $(MOS_TESTS) $(TESS_TESTS) $(VENDOR_TESTS) \
	$(TL_TESTS) $(TL_FAIL_TESTS) $(TL_FAIL_RUNTIME_TESTS) \
	$(TL_KNOWN_FAIL_FAILURES) $(TL_KNOWN_FAILURES))

TL_TEST_EXES = $(patsubst %,$(TL_BUILD_DIR)/test_%,$(TL_TESTS))

# Special rule for test_import_relative_dotdot (needs to run from fixtures directory)
$(TL_BUILD_DIR)/test_import_relative_dotdot: $(TL_TEST_DIR)/fixtures/test_import_relative_dotdot.tl $(TESS_EXE)
	@mkdir -p $(dir $@)
	$(MSG_GEN) $@
	@cd $(TL_TEST_DIR)/fixtures && \
	export ASAN_OPTIONS=$(ASAN_OPTIONS) && \
	if ! $(CURDIR)/$(TESS_EXE) exe --no-standard-includes -S $(CURDIR)/$(TL_STD_DIR) -o $(CURDIR)/$@ test_import_relative_dotdot.tl ; then \
		rm -f $(CURDIR)/$@; \
		$(MSG_FAIL) $@; \
	fi

$(TL_BUILD_DIR)/test_%: $(TL_TEST_DIR)/test_%.tl $(TESS_EXE)
	@mkdir -p $(dir $@)
	$(MSG_GEN) $@
	@export ASAN_OPTIONS=$(ASAN_OPTIONS); \
	if ! ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o $@ $< ; then \
		rm -f $@; \
		$(MSG_FAIL) $@; \
	fi

build-tl-tests: $(TL_TEST_EXES)

test-tl: build-tl-tests
	@failed=0; \
	export ASAN_OPTIONS=$(ASAN_OPTIONS); \
	count_pass=0; \
	count_fail=0; \
	count_known=0; \
	for test in $(TL_TEST_EXES); do \
		name=$$(basename $$test); \
		$(MSG_TEST) $$name; \
		if ! $$test $(STDERR); then \
			$(MSG_FAIL) $$name; \
			failed=$$((failed + 1)); \
		fi; \
		count_pass=$$((count_pass + 1)); \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $$count_pass expected passing tests\n\n"; \
	for name in $(TL_FAIL_TESTS); do \
		$(MSG_TEST) $$name; \
		if ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /dev/null $(TL_TEST_DIR)/test_$$name.tl 2>/dev/null; then \
			$(MSG_FAIL2) $$name; \
			failed=$$((failed + 1)); \
		fi; \
		count_fail=$$((count_fail + 1)); \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $$count_fail expected failure tests\n\n"; \
	count_fail_rt=0; \
	for name in $(TL_FAIL_RUNTIME_TESTS); do \
		$(MSG_TEST) $$name; \
		if ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /tmp/tl_test_$$name $(TL_TEST_DIR)/test_$$name.tl 2>/dev/null; then \
			if /tmp/tl_test_$$name 2>/dev/null; then \
				$(MSG_FAIL2) $$name; \
				failed=$$((failed + 1)); \
			fi; \
		else \
			$(MSG_FAIL) $$name; \
			failed=$$((failed + 1)); \
		fi; \
		count_fail_rt=$$((count_fail_rt + 1)); \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $$count_fail_rt expected runtime failure tests\n\n"; \
	count_known_fail=0; \
	known_fail=0; \
	for name in $(TL_KNOWN_FAIL_FAILURES); do \
		if ! ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /dev/null $(TL_TEST_DIR)/test_$$name.tl 2>/dev/null; then \
			printf "  \033[1;32m[FIXED]\033[0m  test_$$name (remove from TL_KNOWN_FAIL_FAILURES)\n"; \
		else \
			printf "  \033[1;33m[KNOWN]\033[0m  test_$$name\n"; \
			known_fail=$$((known_fail + 1)); \
		fi; \
		count_known_fail=$$((count_known_fail + 1)); \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $$count_known_fail known fail-failure tests\n\n"; \
	known=0; \
	for name in $(TL_KNOWN_FAILURES); do \
		if ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /tmp/tl_test_$$name $(TL_TEST_DIR)/test_$$name.tl 2>/dev/null && /tmp/tl_test_$$name 2>/dev/null; then \
			printf "  \033[1;32m[FIXED]\033[0m  test_$$name (remove from TL_KNOWN_FAILURES)\n"; \
		else \
			printf "  \033[1;33m[KNOWN]\033[0m  test_$$name\n"; \
			known=$$((known + 1)); \
		fi; \
		count_known=$$((count_known + 1)); \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $$count_known known failure tests\n"; \
	if [ $$failed -gt 0 ]; then \
		printf "  \033[1;31m[FAIL] $$failed TL test(s) failed\033[0m\n"; \
		exit 1; \
	fi; \
	printf "\n"; \
	$(MSG_PASS) "All TL tests passed"; \
	if [ $$known -gt 0 ]; then \
		printf "  \033[1;33m[INFO] $$known known failure(s) skipped\033[0m\n"; \
	fi; \
	printf "\n"


# ------------------------------------------------------------------------------
# Combined Test Target
# ------------------------------------------------------------------------------

build-tests: build-mos-tests build-tess-tests build-vendor-tests build-tl-tests

test:
	@mos_ok=0; tess_ok=0; vendor_ok=0; tl_ok=0; \
	$(MAKE) --no-print-directory test-mos && mos_ok=1; \
	$(MAKE) --no-print-directory test-tess && tess_ok=1; \
	$(MAKE) --no-print-directory test-vendor && vendor_ok=1; \
	$(MAKE) --no-print-directory test-tl && tl_ok=1; \
	printf "\n"; \
	printf "==============================================================================\n"; \
	printf "  \033[1;36m[TOTAL]\033[0m $(TOTAL_TESTS) tests\n\n"; \
	if [ $$mos_ok -eq 1 ] && [ $$tess_ok -eq 1 ] && [ $$vendor_ok -eq 1 ] && [ $$tl_ok -eq 1 ]; then \
		$(MSG_PASS) "All test suites passed"; \
	else \
		[ $$mos_ok -eq 0 ]    && printf "  \033[1;31m[FAIL] mos tests failed\033[0m\n"; \
		[ $$tess_ok -eq 0 ]   && printf "  \033[1;31m[FAIL] tess tests failed\033[0m\n"; \
		[ $$vendor_ok -eq 0 ] && printf "  \033[1;31m[FAIL] vendor tests failed\033[0m\n"; \
		[ $$tl_ok -eq 0 ]     && printf "  \033[1;31m[FAIL] TL tests failed\033[0m\n"; \
		printf "\n"; \
		exit 1; \
	fi


# ------------------------------------------------------------------------------
# Tags
# ------------------------------------------------------------------------------

# Function definitions: name(...) ... {
ETAGS_TL_RE_FUNC := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)[ \t]*(.*{/\1/'
# Struct/union definitions without type params: Name : { or Name : |
ETAGS_TL_RE_TYPE := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)[ \t]*:[ \t]*[{|]/\1/'
# Struct/union definitions with type params: Name(T) : { or Name(T) : |
ETAGS_TL_RE_GTYPE := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)([^)]*)[ \t]*:[ \t]*[{|]/\1/'
# Type aliases: Name = Type (single =, not :=)
ETAGS_TL_RE_ALIAS := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)[ \t]*=[^=]/\1/'
# Module declarations: #module Name
ETAGS_TL_RE_MOD := --regex='/^\#module[ \t]+\([a-zA-Z_][a-zA-Z0-9_]*\)/\1/'
# Global bindings: name := expr
ETAGS_TL_RE_BIND := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)[ \t]*:=[^=]/\1/'

tags:
	etags --language=none \
	    $(ETAGS_TL_RE_FUNC) $(ETAGS_TL_RE_TYPE) $(ETAGS_TL_RE_GTYPE) \
	    $(ETAGS_TL_RE_ALIAS) $(ETAGS_TL_RE_MOD) $(ETAGS_TL_RE_BIND) \
	    `find src -name '*.tl'`
	etags --append src/mos/src/*.[ch] src/tess/src/*.[ch]

.PHONY: clean cleanall install test tags
.PHONY: test-mos test-tess test-tl
.PHONY: build-mos-benchmarks bench-mos
.DEFAULT_GOAL := all
