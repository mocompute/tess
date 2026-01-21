# ==============================================================================
# Tess Language - Makefile
# ==============================================================================

# ------------------------------------------------------------------------------
# Build Configuration
# ------------------------------------------------------------------------------

CONFIG ?= release

ifeq ($(CONFIG),release)
  CFLAGS_CONFIG = -O2 -DNDEBUG
  LDFLAGS_CONFIG =
  BUILD_DIR = build-release
else ifeq ($(CONFIG),debug)
  CFLAGS_CONFIG = -g -DDEBUG
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
CFLAGS ?= $(CFLAGS_CONFIG)
CFLAGS += -std=gnu11 -fPIE
CFLAGS += -Werror -Wall -Wextra -Wswitch-enum -Wunused -Winline -Wimplicit-fallthrough

# Test if the compiler supports a given flag (substitute -no- flags)
check_flag = $(shell $(CC) -Werror $(patsubst -Wno-%,-W%,$(1)) -x c -c /dev/null -o /dev/null 2>/dev/null && echo $(1))

CLANG_FLAGS := -Wno-gnu-alignof-expression
CFLAGS += $(foreach flag,$(CLANG_FLAGS),$(call check_flag,$(flag)))

CPPFLAGS = -I$(MOS_INC_DIR) -I$(TESS_INC_DIR)
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
	$(MOS_SRC_DIR)/src/sexp.c		\
	$(MOS_SRC_DIR)/src/sexp_parser.c	\
	$(MOS_SRC_DIR)/src/str.c

MOS_OBJECTS = $(patsubst $(MOS_SRC_DIR)/%.c,$(BUILD_DIR)/mos/%.o,$(MOS_SOURCES))

$(BUILD_DIR)/mos/%.o: $(MOS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Library
# ------------------------------------------------------------------------------

TESS_SOURCES =				\
	$(TESS_SRC_DIR)/src/ast.c	\
	$(TESS_SRC_DIR)/src/error.c	\
	$(TESS_SRC_DIR)/src/parser.c	\
	$(TESS_SRC_DIR)/src/tess.c	\
	$(TESS_SRC_DIR)/src/token.c	\
	$(TESS_SRC_DIR)/src/tokenizer.c \
	$(TESS_SRC_DIR)/src/infer.c	\
	$(TESS_SRC_DIR)/src/transpile.c \
	$(TESS_SRC_DIR)/src/type.c

TESS_OBJECTS = $(patsubst $(TESS_SRC_DIR)/%.c,$(BUILD_DIR)/tess/%.o,$(TESS_SOURCES))

$(BUILD_DIR)/tess/%.o: $(TESS_SRC_DIR)/%.c
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

$(VERSION_HEADER):
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

.PHONY: $(VERSION_HEADER)

$(EMBED_TOOL): $(MOS_SRC_DIR)/src/embed.c $(MOS_OBJECTS)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

$(TESS_EMBED_SRC): $(EMBED_TOOL) $(TESS_SRC_DIR)/embed/std.c
	$(MSG_GEN) $<
	$(Q)$(EMBED_TOOL) $(TESS_SRC_DIR)/embed $(TESS_EMBED_SRC) $(STDERR)

$(TESS_EMBED_OBJ): $(TESS_EMBED_SRC)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Executable
# ------------------------------------------------------------------------------

TESS_EXE_SRC = $(TESS_SRC_DIR)/src/tess_exe.c
TESS_EXE_OBJ = $(BUILD_DIR)/tess_exe.o
TESS_EXE     = tess

$(TESS_EXE): $(TESS_EXE_OBJ) $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TESS_EXE_OBJ): $(TESS_EXE_SRC) $(VERSION_HEADER)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -I$(BUILD_DIR) -c -o $@ $<


# ------------------------------------------------------------------------------
# Default Target
# ------------------------------------------------------------------------------

all: $(TESS_EXE)

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------

install: $(TESS_EXE)
	install -D -m 755 $(TESS_EXE) $(INSTALL_BIN)/$(TESS_EXE)
	@mkdir -p $(INSTALL_LIB)/std
	find src/tl/std -name '*.tl' -exec install -D -m 644 {} $(INSTALL_LIB)/std/ \;

# ------------------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) $(TESS_EXE)

cleanall:
	rm -rf build-release build-debug build-asan $(TESS_EXE)

# ==============================================================================
# Tests
# ==============================================================================

# $(1): test suite name (for display)
# $(2): list of test executables
define run_test_suite
	@failed=0; \
	for test in $(2); do \
		name=$$(basename $$test); \
		$(MSG_TEST) $$name; \
		if ! $$test $(STDERR); then \
			printf "  \033[1;31m[FAIL] $$name\033[0m\n"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
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

MOS_TESTS     = alloc array file hash map sexp str types util
MOS_TEST_EXES = $(patsubst %,$(BUILD_DIR)/test_mos_%,$(MOS_TESTS))

$(BUILD_DIR)/test_mos_%: $(MOS_SRC_DIR)/src/test_%.c $(MOS_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

build-mos-tests: $(MOS_TEST_EXES)

test-mos: build-mos-tests
	$(call run_test_suite,mos,$(MOS_TEST_EXES))

# ------------------------------------------------------------------------------
# tess Compiler Tests
# ------------------------------------------------------------------------------

TESS_TESTS     = tess type_v2
TESS_TEST_EXES = $(patsubst %,$(BUILD_DIR)/test_%,$(TESS_TESTS))

$(BUILD_DIR)/test_%: $(TESS_SRC_DIR)/src/test_%.c $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

build-tess-tests: $(TESS_TEST_EXES)

test-tess: build-tess-tests
	$(call run_test_suite,tess,$(TESS_TEST_EXES))

# ------------------------------------------------------------------------------
# Tesslang (.tl) Tests
# ------------------------------------------------------------------------------

TL_TESTS =					\
	address_of				\
	alignof					\
	alloc_align				\
	alloc_allocators			\
	anon_lambda				\
	apply_generic				\
	apply_generic_through_pointer		\
	apply_lambda				\
	arithmetic_unary_op			\
	array_index_binary_op			\
	assignment_by_op			\
	atexit					\
	_Exit					\
	binop					\
	bitwise_operators			\
	c_div					\
	c_symbol_annotation			\
	c_struct				\
	c_timespec				\
	carray					\
	case_basic_else				\
	case_pred_ident				\
	case_pred_lambda			\
	char_literal				\
	closure_fun_ptr				\
	compound_assignment			\
	defun_inline_type			\
	dynamic_array				\
	enum_no_module				\
	enum_module				\
	embed_c					\
	escape_sequences			\
	if_basic				\
	if_expression				\
	integer_literals			\
	factorial				\
	fatal_intrinsic				\
	float_scientific			\
	for_break				\
	for_continue				\
	for_statement_basic			\
	for_statement_module			\
	forward_decl_not_needed			\
	function_pointer_argument		\
	function_pointer_value			\
	function_pointer_in_struct		\
	function_pointer_in_struct_direct	\
	function_pointer_in_struct_direct_2	\
	generic_lambda				\
	global_variables			\
	hello					\
	lambda_basic				\
	lambda_apply				\
	lambda_immediate			\
	lambda_immediate_type_argument		\
	let_in_basic				\
	let_in_expression			\
	logical_or				\
	malloc_free				\
	malloc_free_is_null			\
	malloc_struct_basic			\
	mapper_basic				\
	mapper_lambda				\
	module_basic				\
	module_init				\
	mutual_recursion			\
	mutual_recursion_module			\
	mutual_recursion_module_apply		\
	mutual_recursion_both_referenced	\
	nested_struct_access			\
	nested_lambda_context			\
	number_separators			\
	printf					\
	pointer_array				\
	pointer_cast				\
	pointer_cast_struct			\
	pointer_compare_null			\
	pointer_deref				\
	pointer_deref_double			\
	reassign_into_stack_lambda		\
	reassign_result				\
	recursive_type				\
	recursive_type_basic			\
	recursive_type_cycle_3			\
	recursive_type_generic			\
	recursive_type_mutual			\
	regress_type_cons			\
	relational_basic			\
	return_null				\
	return_statement			\
	scope_shadow				\
	sizeof					\
	sizeof_type_literal			\
	static_init				\
	static_init_struct			\
	static_init_struct_fun_ptr		\
	strcmp					\
	struct_concrete				\
	struct_empty				\
	struct_generic				\
	struct_construction			\
	tail_call				\
	type_alias_generic			\
	type_alias_local			\
	type_alias_local_direct			\
	type_alias_module_chained		\
	type_alias_module_chained_direct	\
	type_alias_module_enum			\
	type_alias_module_enum_direct		\
	type_alias_module_multi_arg		\
	type_alias_module_multi_arg_direct	\
	type_alias_module_multi_arg_direct_compatible	\
	type_alias_module_simple		\
	type_alias_module_simple_direct		\
	type_assertion				\
	type_assertion_field			\
	tagged_union				\
	tagged_union_generic_basic		\
	tagged_union_generic_case		\
	tagged_union_generic_func		\
	tagged_union_generic_multi		\
	tagged_union_generic_nested		\
	tagged_union_generic_param		\
	tagged_union_generic_return		\
	tagged_union_mutable_case		\
	tagged_union_option			\
	type_literal_generic			\
	type_arguments_annotations		\
	type_argument_field_annotation		\
	types_float				\
	types_integer				\
	types_integer_cast			\
	union_basic				\
	union_module_intermediate		\
	union_module_second_variant		\
	uninitialized_fields			\
	while_break				\
	while_continue				\
	while_statement				\
	while_update_statement

TL_FAIL_TESTS =					\
	case_float				\
	fail_monkey_patch			\
	fail_tagged_union_missing_case		\
	fail_tagged_union_unknown_variant	\
	type_alias_partial_specialization	\
	unknown_free_variable

TL_TEST_EXES = $(patsubst %,$(TL_BUILD_DIR)/test_%,$(TL_TESTS))

$(TL_BUILD_DIR)/test_%: $(TL_TEST_DIR)/test_%.tl $(TESS_EXE)
	@mkdir -p $(dir $@)
	$(MSG_GEN) $@
	@if ! ./$(TESS_EXE) exe --no-standard-includes -I $(TL_STD_DIR) -o $@ $< ; then \
		$(MSG_FAIL) $@; \
	fi

build-tl-tests: $(TL_TEST_EXES)

test-tl: build-tl-tests
	@failed=0; \
	for test in $(TL_TEST_EXES); do \
		name=$$(basename $$test); \
		$(MSG_TEST) $$name; \
		if ! $$test $(STDERR); then \
			$(MSG_FAIL) $$name; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	for name in $(TL_FAIL_TESTS); do \
		$(MSG_TEST) $$name; \
		if ./$(TESS_EXE) exe --no-standard-includes -I $(TL_STD_DIR) -o /dev/null $(TL_TEST_DIR)/test_$$name.tl 2>/dev/null; then \
			$(MSG_FAIL2) $$name; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	if [ $$failed -gt 0 ]; then \
		printf "  \033[1;31m[FAIL] $$failed TL test(s) failed\033[0m\n"; \
		exit 1; \
	fi; \
	printf "\n"; \
	$(MSG_PASS) "All TL tests passed"; \
	printf "\n"


# ------------------------------------------------------------------------------
# Combined Test Target
# ------------------------------------------------------------------------------

build-tests: build-mos-tests build-tess-tests build-tl-tests

test:
	@mos_ok=0; tess_ok=0; tl_ok=0; \
	$(MAKE) --no-print-directory test-mos && mos_ok=1; \
	$(MAKE) --no-print-directory test-tess && tess_ok=1; \
	$(MAKE) --no-print-directory test-tl && tl_ok=1; \
	printf "\n"; \
	printf "==============================================================================\n"; \
	if [ $$mos_ok -eq 1 ] && [ $$tess_ok -eq 1 ] && [ $$tl_ok -eq 1 ]; then \
		printf "\n"; \
		$(MSG_PASS) "All test suites passed"; \
	else \
		[ $$mos_ok -eq 0 ]  && printf "  \033[1;31m[FAIL] mos tests failed\033[0m\n"; \
		[ $$tess_ok -eq 0 ] && printf "  \033[1;31m[FAIL] tess tests failed\033[0m\n"; \
		[ $$tl_ok -eq 0 ]   && printf "  \033[1;31m[FAIL] TL tests failed\033[0m\n"; \
		printf "\n"; \
		exit 1; \
	fi


.PHONY: clean cleanall install test
.PHONY: test-mos test-tess test-tl
.DEFAULT_GOAL := all
