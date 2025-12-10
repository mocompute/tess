# ==============================================================================
# Tess Language - Makefile
# ==============================================================================

# ------------------------------------------------------------------------------
# Compiler and Flags
# ------------------------------------------------------------------------------

CC     ?= cc
CFLAGS ?= -O2 -DNDEBUG
CFLAGS += -std=gnu11 -fPIE
CFLAGS += -Werror -Wall -Wextra -Wswitch-enum -Wunused -Winline -Wimplicit-fallthrough
CFLAGS += -Wno-gnu-alignof-expression

CPPFLAGS = -I$(MOS_INC_DIR) -I$(TESS_INC_DIR)
LDFLAGS ?=

# ------------------------------------------------------------------------------
# Directories
# ------------------------------------------------------------------------------

MOS_SRC_DIR  = src/mos
MOS_INC_DIR  = $(MOS_SRC_DIR)/include
TESS_SRC_DIR = src/tess
TESS_INC_DIR = $(TESS_SRC_DIR)/include
BUILD_DIR    = build-release

# Installation
PREFIX      ?= /usr/local
INSTALL_BIN  = $(DESTDIR)$(PREFIX)/bin
INSTALL_LIB  = $(DESTDIR)$(PREFIX)/lib/tess

# Test directories
TL_TEST_DIR  = $(TESS_SRC_DIR)/tl
TL_STD_DIR   = src/tl/std
TL_BUILD_DIR = $(BUILD_DIR)/tl

# ------------------------------------------------------------------------------
# mos Library
# ------------------------------------------------------------------------------

MOS_SOURCES = \
	$(MOS_SRC_DIR)/src/alloc.c \
	$(MOS_SRC_DIR)/src/array.c \
	$(MOS_SRC_DIR)/src/dbg.c \
	$(MOS_SRC_DIR)/src/hash.c \
	$(MOS_SRC_DIR)/src/file.c \
	$(MOS_SRC_DIR)/src/hashmap.c \
	$(MOS_SRC_DIR)/src/sexp.c \
	$(MOS_SRC_DIR)/src/sexp_parser.c \
	$(MOS_SRC_DIR)/src/str.c

MOS_OBJECTS = $(patsubst $(MOS_SRC_DIR)/%.c,$(BUILD_DIR)/mos/%.o,$(MOS_SOURCES))

$(BUILD_DIR)/mos/%.o: $(MOS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Library
# ------------------------------------------------------------------------------

TESS_SOURCES = \
	$(TESS_SRC_DIR)/src/ast.c \
	$(TESS_SRC_DIR)/src/error.c \
	$(TESS_SRC_DIR)/src/parser.c \
	$(TESS_SRC_DIR)/src/tess.c \
	$(TESS_SRC_DIR)/src/token.c \
	$(TESS_SRC_DIR)/src/tokenizer.c \
	$(TESS_SRC_DIR)/src/infer.c \
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

$(TESS_EXE_OBJ): $(TESS_EXE_SRC)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# Output Control
# ------------------------------------------------------------------------------

V ?= 0
ifeq ($(V),0)
  Q = @
  STDERR    = >/dev/null 2>&1
  MSG_CC    = @printf "  \033[1;34m[CC]\033[0m     %s\n"
  MSG_LD    = @printf "  \033[1;32m[LD]\033[0m     %s\n"
  MSG_GEN   = @printf "  \033[1;33m[GEN]\033[0m    %s\n"
  MSG_TEST  = @printf "  \033[1;35m[TEST]\033[0m   %s\n"
else
  Q =
  STDERR    =
  MSG_CC    = @printf "  [CC]     %s\n"
  MSG_LD    = @printf "  [LD]     %s\n"
  MSG_GEN   = @printf "  [GEN]    %s\n"
  MSG_TEST  = @printf "  [TEST]   %s\n"
endif


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
	cd src/tl/std && find . -name '*.tl' -exec install -D -m 644 {} $(CURDIR)/$(INSTALL_LIB)/{} \;

# ------------------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) $(TESS_EXE)

# ==============================================================================
# Tests
# ==============================================================================

# ------------------------------------------------------------------------------
# mos Library Tests
# ------------------------------------------------------------------------------

MOS_TESTS     = alloc array map sexp str types util
MOS_TEST_EXES = $(patsubst %,$(BUILD_DIR)/test_mos_%,$(MOS_TESTS))

$(BUILD_DIR)/test_mos_%: $(MOS_SRC_DIR)/src/test_%.c $(MOS_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

build-mos-tests: $(MOS_TEST_EXES)

test-mos: build-mos-tests
	@failed=0; \
	for test in $(MOS_TEST_EXES); do \
		name=$$(basename $$test); \
		if $$test $(STDERR); then \
			echo "PASS: $$name"; \
		else \
			echo "FAIL: $$name"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "\033[1;31m❌ $$failed test(s) failed\033[0m"; \
		exit 1; \
	fi; \
	echo "\033[1;32m✅ All mos tests passed\033[0m"

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
	@failed=0; \
	for test in $(TESS_TEST_EXES); do \
		name=$$(basename $$test); \
		if $$test $(STDERR); then \
			echo "PASS: $$name"; \
		else \
			echo "FAIL: $$name"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "\033[1;31m❌ $$failed test(s) failed\033[0m"; \
		exit 1; \
	fi; \
	echo "\033[1;32m✅ All tess tests passed\033[0m"

# ------------------------------------------------------------------------------
# Tesslang (.tl) Tests
# ------------------------------------------------------------------------------

TL_TESTS = \
	address_of \
	alloc_align \
	alloc_allocators \
	anon_lambda \
	apply_generic \
	apply_generic_through_pointer \
	apply_lambda \
	arithmetic_unary_op \
	array_index_binary_op \
	atexit \
	_Exit \
	binop \
	c_div \
	c_symbol_annotation \
	c_struct \
	c_timespec \
	case_basic_else \
	case_float \
	case_pred_ident \
	case_pred_lambda \
	cast_string_to_ptr \
	cast_string_to_ptr_and_index \
	char_literal \
	closure_polymorphism \
	defun_inline_type \
	dynamic_array \
	enum_no_module \
	enum_module \
	embed_c \
	if_basic \
	if_expression \
	factorial \
	for_statement_basic \
	for_statement_module \
	forward_decl_not_needed \
	function_pointer_argument \
	function_pointer_value \
	function_pointer_in_struct \
	function_pointer_in_struct_direct \
	function_pointer_in_struct_direct_2 \
	generic_lambda \
	global_variables \
	lambda_basic \
	lambda_apply \
	lambda_immediate \
	lambda_immediate_type_argument \
	let_in_basic \
	malloc_free \
	malloc_free_is_null \
	malloc_struct_basic \
	mapper_basic \
	mapper_lambda \
	module_basic \
	mutual_recursion \
	mutual_recursion_module \
	mutual_recursion_module_apply \
	mutual_recursion_both_referenced \
	nested_struct_access \
	nested_lambda_context \
	number_separators \
	printf \
	pointer_array \
	pointer_cast \
	pointer_deref \
	pointer_deref_double \
	reassign_into_stack_lambda \
	reassign_result \
	recursive_type \
	recursive_type_basic \
	recursive_type_cycle_3 \
	recursive_type_generic \
	recursive_type_mutual \
	relational_basic \
	return_statement \
	scope_shadow \
	sizeof \
	sizeof_type_literal \
	static_init \
	static_init_struct \
	static_init_struct_fun_ptr \
	strcmp \
	struct_concrete \
	struct_empty \
	struct_generic \
	type_literal_generic \
	type_arguments_annotations \
	type_argument_field_annotation \
	types_float \
	types_integer \
	types_integer_cast \
	union_basic \
	union_module_intermediate \
	union_module_second_variant \
	while_statement \
	while_update_statement

TL_FAIL_TESTS = \
	unknown_free_variable

TL_TEST_EXES = $(patsubst %,$(TL_BUILD_DIR)/test_%,$(TL_TESTS))

$(TL_BUILD_DIR)/test_%: $(TL_TEST_DIR)/test_%.tl $(TESS_EXE)
	@mkdir -p $(dir $@)
	$(MSG_GEN) $@
	$(Q)./$(TESS_EXE) exe -I $(TL_STD_DIR) -o $@ $<

build-tl-tests: $(TL_TEST_EXES)

test-tl: build-tl-tests
	@failed=0; \
	for test in $(TL_TEST_EXES); do \
		name=$$(basename $$test); \
		if $$test $(STDERR); then \
			echo "PASS: $$name"; \
		else \
			echo "FAIL: $$name"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	for name in $(TL_FAIL_TESTS); do \
		if ./$(TESS_EXE) exe -I $(TL_TEST_DIR) -I $(TL_STD_DIR) -o /dev/null $(TL_TEST_DIR)/test_$$name.tl 2>/dev/null; then \
			echo "FAIL: $$name (expected build failure)"; \
			failed=$$((failed + 1)); \
		else \
			echo "PASS: $$name (build failed as expected)"; \
		fi; \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "\033[1;31m❌ $$failed test(s) failed\033[0m"; \
		exit 1; \
	fi; \
	echo "\033[1;32m✅ All tl tests passed\033[0m"

# ------------------------------------------------------------------------------
# Combined Test Target
# ------------------------------------------------------------------------------

test:
	@mos_ok=0; tess_ok=0; tl_ok=0; \
	$(MAKE) --no-print-directory test-mos && mos_ok=1; \
	$(MAKE) --no-print-directory test-tess && tess_ok=1; \
	$(MAKE) --no-print-directory test-tl && tl_ok=1; \
	echo ""; \
	echo "=============================================================================="; \
	if [ $$mos_ok -eq 1 ] && [ $$tess_ok -eq 1 ] && [ $$tl_ok -eq 1 ]; then \
		echo "\033[1;32m✅ All test suites passed\033[0m"; \
	else \
		[ $$mos_ok -eq 0 ]  && echo "\033[1;31m❌ mos tests failed\033[0m"; \
		[ $$tess_ok -eq 0 ] && echo "\033[1;31m❌ tess tests failed\033[0m"; \
		[ $$tl_ok -eq 0 ]   && echo "\033[1;31m❌ tl tests failed\033[0m"; \
		echo ""; \
		exit 1; \
	fi

# ------------------------------------------------------------------------------
# Phony Targets
# ------------------------------------------------------------------------------

.PHONY: all clean install test
.PHONY: build-mos-tests test-mos
.PHONY: build-tess-tests test-tess
.PHONY: build-tl-tests test-tl
