# ==============================================================================
# Tess Language - Makefile
# ==============================================================================

# ------------------------------------------------------------------------------
# Build Configuration
# ------------------------------------------------------------------------------

CONFIG ?= release
ASAN_OPTIONS ?= detect_leaks=1
COV_EXPORT =

ifeq ($(CONFIG),release)
  CFLAGS_CONFIG = -O2 -DNDEBUG -flto=auto
  LDFLAGS_CONFIG = -flto=auto
  BUILD_DIR = build-release
else ifeq ($(CONFIG),debug)
  CFLAGS_CONFIG = -g -DDEBUG -fno-omit-frame-pointer
  LDFLAGS_CONFIG =
  BUILD_DIR = build-debug
else ifeq ($(CONFIG),asan)
  CFLAGS_CONFIG = -O -g -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS_CONFIG = -fsanitize=address,undefined
  BUILD_DIR = build-asan
else ifeq ($(CONFIG),coverage)
  CC := clang
  CFLAGS_CONFIG = -g -O1 -DDEBUG -fprofile-instr-generate -fcoverage-mapping -fno-omit-frame-pointer
  LDFLAGS_CONFIG = -fprofile-instr-generate
  BUILD_DIR = build-coverage
  COV_EXPORT = export LLVM_PROFILE_FILE="$(CURDIR)/build-coverage/profiles/prof_%p_%m.profraw";
else
  $(error Unknown CONFIG: $(CONFIG). Valid options: release, debug, asan, coverage)
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
TL_TEST_DIR    = $(TESS_SRC_DIR)/tl
TL_STD_DIR     = src/tl/std
TL_STD_SOURCES = $(shell find $(TL_STD_DIR) -name '*.tl')
TL_BUILD_DIR   = $(BUILD_DIR)/tl

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
	$(MOS_SRC_DIR)/src/sha256.c		\
	$(MOS_SRC_DIR)/src/str.c

MOS_OBJECTS = $(patsubst $(MOS_SRC_DIR)/src/%.c,$(BUILD_DIR)/mos/%.o,$(MOS_SOURCES))

MOS_HEADERS =					\
	$(MOS_SRC_DIR)/include/alloc.h		\
	$(MOS_SRC_DIR)/include/alloc_internal.h	\
	$(MOS_SRC_DIR)/include/array.h		\
	$(MOS_SRC_DIR)/include/dbg.h		\
	$(MOS_SRC_DIR)/include/file.h		\
	$(MOS_SRC_DIR)/include/hash.h		\
	$(MOS_SRC_DIR)/include/hashmap.h	\
	$(MOS_SRC_DIR)/include/nodiscard.h	\
	$(MOS_SRC_DIR)/include/platform.h	\
	$(MOS_SRC_DIR)/include/sha256.h		\
	$(MOS_SRC_DIR)/include/str.h		\
	$(MOS_SRC_DIR)/include/types.h		\
	$(MOS_SRC_DIR)/include/util.h

$(foreach h,$(MOS_HEADERS),$(if $(wildcard $h),,$(error MOS_HEADERS: not found: $h)))


$(BUILD_DIR)/mos/%.o: $(MOS_SRC_DIR)/src/%.c $(MOS_HEADERS)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Library
# ------------------------------------------------------------------------------

TESS_SOURCES =				\
	$(TESS_SRC_DIR)/src/ast.c	\
	$(TESS_SRC_DIR)/src/error.c	\
	$(TESS_SRC_DIR)/src/fetch.c	\
	$(TESS_SRC_DIR)/src/format.c	\
	$(TESS_SRC_DIR)/src/import_resolver.c \
	$(TESS_SRC_DIR)/src/parser.c	\
	$(TESS_SRC_DIR)/src/parser_expr.c \
	$(TESS_SRC_DIR)/src/parser_statements.c \
	$(TESS_SRC_DIR)/src/parser_tagged_union.c \
	$(TESS_SRC_DIR)/src/parser_types.c \
	$(TESS_SRC_DIR)/src/tess.c	\
	$(TESS_SRC_DIR)/src/token.c	\
	$(TESS_SRC_DIR)/src/tokenizer.c \
	$(TESS_SRC_DIR)/src/infer.c	\
	$(TESS_SRC_DIR)/src/infer_alpha.c	\
	$(TESS_SRC_DIR)/src/infer_constraint.c	\
	$(TESS_SRC_DIR)/src/infer_specialize.c	\
	$(TESS_SRC_DIR)/src/infer_update.c	\
	$(TESS_SRC_DIR)/src/transpile.c \
	$(TESS_SRC_DIR)/src/manifest.c \
	$(TESS_SRC_DIR)/src/source_scanner.c \
	$(TESS_SRC_DIR)/src/tpkg.c	\
	$(TESS_SRC_DIR)/src/lockfile.c	\
	$(TESS_SRC_DIR)/src/cbind.c	\
	$(TESS_SRC_DIR)/src/type.c

TESS_OBJECTS = $(patsubst $(TESS_SRC_DIR)/src/%.c,$(BUILD_DIR)/tess/%.o,$(TESS_SOURCES))

TESS_HEADERS =						\
	$(TESS_SRC_DIR)/src/infer_internal.h		\
	$(TESS_SRC_DIR)/include/ast.h			\
	$(TESS_SRC_DIR)/include/ast_tags.h		\
	$(TESS_SRC_DIR)/include/error.h			\
	$(TESS_SRC_DIR)/include/fetch.h			\
	$(TESS_SRC_DIR)/include/format.h		\
	$(TESS_SRC_DIR)/include/import_resolver.h	\
	$(TESS_SRC_DIR)/include/infer.h			\
	$(TESS_SRC_DIR)/include/manifest.h		\
	$(TESS_SRC_DIR)/include/parser.h		\
	$(TESS_SRC_DIR)/include/source_scanner.h	\
	$(TESS_SRC_DIR)/include/syntax.h		\
	$(TESS_SRC_DIR)/include/tess.h			\
	$(TESS_SRC_DIR)/include/tpkg.h			\
	$(TESS_SRC_DIR)/include/lockfile.h		\
	$(TESS_SRC_DIR)/include/token.h			\
	$(TESS_SRC_DIR)/include/tokenizer.h		\
	$(TESS_SRC_DIR)/include/transpile.h		\
	$(TESS_SRC_DIR)/include/cbind.h			\
	$(TESS_SRC_DIR)/include/type.h			\
	$(TESS_SRC_DIR)/include/type_registry.h

$(foreach h,$(TESS_HEADERS),$(if $(wildcard $h),,$(error TESS_HEADERS: not found: $h)))

$(BUILD_DIR)/tess/%.o: $(TESS_SRC_DIR)/src/%.c $(TESS_HEADERS)
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
HOST_ARCH := $(shell uname -m | sed 's/^arm64$$/aarch64/')
HOST_OS   := $(shell uname -s | tr '[:upper:]' '[:lower:]')

$(VERSION_HEADER): VERSION $(wildcard .git/HEAD .git/packed-refs .git/refs/heads/*)
	@mkdir -p $(dir $@)
	$(MSG_GEN) $@
	$(Q)( \
		if [ -n "$$GIT_HASH" ]; then HASH="$$GIT_HASH"; \
		else HASH=$$(git rev-parse --short=7 HEAD 2>/dev/null || echo "nogit"); fi; \
		if [ -n "$$GIT_BRANCH" ]; then BRANCH="$$GIT_BRANCH"; \
		else BRANCH=$$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo ""); fi; \
		if [ -n "$$BRANCH" ]; then BRANCH_SEG="$$BRANCH-"; else BRANCH_SEG=""; fi; \
		echo "/* Auto-generated version header */" > $@; \
		echo "#ifndef TESS_VERSION_H" >> $@; \
		echo "#define TESS_VERSION_H" >> $@; \
		echo "" >> $@; \
		echo "#define TESS_VERSION \"$(VERSION)-$$BRANCH_SEG$$HASH-$(HOST_ARCH)-$(HOST_OS)\"" >> $@; \
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
# Stdlib Embedding
# ------------------------------------------------------------------------------

STDLIB_PACK_TOOL = $(BUILD_DIR)/stdlib_pack
TESS_STDLIB_SRC  = $(BUILD_DIR)/tess_stdlib_embed.c
TESS_STDLIB_OBJ  = $(BUILD_DIR)/tess/tess_stdlib_embed.o

$(STDLIB_PACK_TOOL): $(TESS_SRC_DIR)/src/stdlib_pack.c $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS) $(LIBDEFLATE_OBJECTS)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

$(TESS_STDLIB_SRC): $(STDLIB_PACK_TOOL) $(TL_STD_SOURCES)
	$(MSG_GEN) $@
	$(Q)$(STDLIB_PACK_TOOL) $(TL_STD_DIR) $(TESS_STDLIB_SRC)

$(TESS_STDLIB_OBJ): $(TESS_STDLIB_SRC)
	@mkdir -p $(dir $@)
	$(MSG_CC) $<
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# tess Executable
# ------------------------------------------------------------------------------

TESS_EXE_SRC = $(TESS_SRC_DIR)/src/tess_exe.c
TESS_EXE_OBJ = $(BUILD_DIR)/tess_exe.o
TESS_EXE     = $(BUILD_DIR)/bin/tess

$(TESS_EXE): $(TESS_EXE_OBJ) $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS) $(LIBDEFLATE_OBJECTS) $(TESS_STDLIB_OBJ)
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

all: $(TESS_EXE) tess build-mos-tests build-tess-tests

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
	rm -rf build-release build-debug build-asan build-coverage tess

# ==============================================================================
# Tests
# ==============================================================================

# $(1): test suite name (for display)
# $(2): list of test executables
# $(3): test count
define run_test_suite
	@mkdir -p $(COV_PROF_DIR); \
	failed=0; \
	export ASAN_OPTIONS=$(ASAN_OPTIONS); $(COV_EXPORT) \
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

MOS_TESTS      = alloc array file map sha256 str types util
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

TESS_TESTS     = tess type_v2 format tpkg import_resolver manifest source_scanner cbind lockfile fetch
TESS_TEST_EXES = $(patsubst %,$(BUILD_DIR)/test_%,$(TESS_TESTS))

$(BUILD_DIR)/test_%: $(TESS_SRC_DIR)/src/test_%.c $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS) $(LIBDEFLATE_OBJECTS) | $(TESS_EXE)
	@mkdir -p $(dir $@)
	$(MSG_LD) $@
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -DTEST_TESS_EXE='"$(CURDIR)/$(TESS_EXE)"' -DTEST_STDLIB_DIR='"$(CURDIR)/$(TL_STD_DIR)"' -o $@ $^

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

# Auto-discover tests from subdirectories
TL_DIR_PASS        = $(TL_TEST_DIR)/test/pass
TL_DIR_PASS_OPT    = $(TL_TEST_DIR)/test/pass_optimized
TL_DIR_FAIL        = $(TL_TEST_DIR)/test/fail
TL_DIR_FAIL_RT     = $(TL_TEST_DIR)/test/fail_runtime
TL_DIR_KNOWN       = $(TL_TEST_DIR)/test/known_failures
TL_DIR_KNOWN_FF    = $(TL_TEST_DIR)/test/known_fail_failures

TL_TESTS              = $(patsubst $(TL_DIR_PASS)/test_%.tl,%,$(wildcard $(TL_DIR_PASS)/test_*.tl)) import_relative_dotdot
TL_TESTS_OPTIMIZED    = $(patsubst $(TL_DIR_PASS_OPT)/test_%.tl,%,$(wildcard $(TL_DIR_PASS_OPT)/test_*.tl))
TL_FAIL_TESTS         = $(patsubst $(TL_DIR_FAIL)/test_%.tl,%,$(wildcard $(TL_DIR_FAIL)/test_*.tl))
TL_FAIL_RUNTIME_TESTS = $(patsubst $(TL_DIR_FAIL_RT)/test_%.tl,%,$(wildcard $(TL_DIR_FAIL_RT)/test_*.tl))
TL_KNOWN_FAILURES     = $(patsubst $(TL_DIR_KNOWN)/test_%.tl,%,$(wildcard $(TL_DIR_KNOWN)/test_*.tl))
TL_KNOWN_FAIL_FAILURES = $(patsubst $(TL_DIR_KNOWN_FF)/test_%.tl,%,$(wildcard $(TL_DIR_KNOWN_FF)/test_*.tl))

# Total test count across all suites
TOTAL_TESTS = $(words $(MOS_TESTS) $(TESS_TESTS) $(VENDOR_TESTS) \
	$(TL_TESTS) $(TL_TESTS_OPTIMIZED) $(TL_FAIL_TESTS) $(TL_FAIL_RUNTIME_TESTS) \
	$(TL_KNOWN_FAIL_FAILURES) $(TL_KNOWN_FAILURES))

TL_TEST_EXES = $(patsubst %,$(TL_BUILD_DIR)/test_%,$(TL_TESTS))
TL_TEST_OPT_EXES = $(patsubst %,$(TL_BUILD_DIR)/test_opt_%,$(TL_TESTS_OPTIMIZED))

# Special rule for test_import_relative_dotdot (needs to run from fixtures directory)
$(TL_BUILD_DIR)/test_import_relative_dotdot: $(TL_TEST_DIR)/fixtures/test_import_relative_dotdot.tl $(TESS_EXE) $(TL_STD_SOURCES)
	@mkdir -p $(dir $@) $(COV_PROF_DIR)
	$(MSG_GEN) $@
	@cd $(TL_TEST_DIR)/fixtures && \
	export ASAN_OPTIONS=$(ASAN_OPTIONS) && $(COV_EXPORT) \
	if ! $(CURDIR)/$(TESS_EXE) exe --no-standard-includes -S $(CURDIR)/$(TL_STD_DIR) -o $(CURDIR)/$@ test_import_relative_dotdot.tl ; then \
		rm -f $(CURDIR)/$@; \
		$(MSG_FAIL) $@; \
	fi

# Pass tests (from test/pass/)
$(TL_BUILD_DIR)/test_%: $(TL_DIR_PASS)/test_%.tl $(TESS_EXE) $(TL_STD_SOURCES)
	@mkdir -p $(dir $@) $(COV_PROF_DIR)
	$(MSG_GEN) $@
	@$(COV_EXPORT) export ASAN_OPTIONS=$(ASAN_OPTIONS); \
	if ! ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o $@ $< ; then \
		rm -f $@; \
		$(MSG_FAIL) $@; \
	fi

# Optimized pass tests (from test/pass_optimized/, need default -O2 optimization)
$(TL_BUILD_DIR)/test_opt_%: $(TL_DIR_PASS_OPT)/test_%.tl $(TESS_EXE) $(TL_STD_SOURCES)
	@mkdir -p $(dir $@) $(COV_PROF_DIR)
	$(MSG_GEN) $@
	@$(COV_EXPORT) export ASAN_OPTIONS=$(ASAN_OPTIONS); \
	if ! ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o $@ $< ; then \
		rm -f $@; \
		$(MSG_FAIL) $@; \
	fi

build-tl-tests: $(TESS_EXE) $(TL_TEST_EXES) $(TL_TEST_OPT_EXES)

test-tl: build-tl-tests
	@mkdir -p $(COV_PROF_DIR); \
	failed=0; \
	export ASAN_OPTIONS=$(ASAN_OPTIONS); $(COV_EXPORT) \
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
	for test in $(TL_TEST_OPT_EXES); do \
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
		if ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /dev/null $(TL_DIR_FAIL)/test_$$name.tl 2>/dev/null; then \
			$(MSG_FAIL2) $$name; \
			failed=$$((failed + 1)); \
		fi; \
		count_fail=$$((count_fail + 1)); \
	done; \
	printf "  \033[1;36m[COUNT]\033[0m $$count_fail expected failure tests\n\n"; \
	count_fail_rt=0; \
	for name in $(TL_FAIL_RUNTIME_TESTS); do \
		$(MSG_TEST) $$name; \
		if ./$(TESS_EXE) exe --no-standard-includes --bounds-check -S $(TL_STD_DIR) -o /tmp/tl_test_$$name $(TL_DIR_FAIL_RT)/test_$$name.tl 2>/dev/null; then \
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
		if ! ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /dev/null $(TL_DIR_KNOWN_FF)/test_$$name.tl 2>/dev/null; then \
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
		if ./$(TESS_EXE) exe --no-standard-includes -S $(TL_STD_DIR) -o /tmp/tl_test_$$name $(TL_DIR_KNOWN)/test_$$name.tl 2>/dev/null && /tmp/tl_test_$$name 2>/dev/null; then \
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

test: build-tests
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

# Function definitions: name(...) ... { or name[T](...) ... {
ETAGS_TL_RE_FUNC := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)\(\[[^]]*\]\)?[ \t]*(.*{/\1/'
# Struct/union definitions without type params: Name : { or Name : |
ETAGS_TL_RE_TYPE := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)[ \t]*:[ \t]*[{|]/\1/'
# Struct/union definitions with type params: Name[T] : { or Name[T] : |
ETAGS_TL_RE_GTYPE := --regex='/^\([a-zA-Z_][a-zA-Z0-9_]*\)\[[^]]*\][ \t]*:[ \t]*[{|]/\1/'
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

# ------------------------------------------------------------------------------
# Coverage (CONFIG=coverage only, requires clang + llvm-cov + llvm-profdata)
# ------------------------------------------------------------------------------

COV_PROF_DIR  = $(BUILD_DIR)/profiles
COV_MERGED    = $(BUILD_DIR)/default.profdata
COV_REPORT    = $(BUILD_DIR)/coverage-report

# Run all tests with coverage instrumentation and generate reports.
# Usage: make CONFIG=coverage coverage
coverage:
ifeq ($(CONFIG),coverage)
	$(Q)rm -rf $(COV_PROF_DIR) $(COV_MERGED) $(COV_REPORT)
	$(Q)mkdir -p $(COV_PROF_DIR)
	@$(MAKE) --no-print-directory CONFIG=coverage test
	@printf "\n  \033[1;33m[COV]\033[0m  Merging profile data...\n"
	$(Q)llvm-profdata merge -sparse $(COV_PROF_DIR)/*.profraw -o $(COV_MERGED)
	@printf "  \033[1;33m[COV]\033[0m  Generating report...\n\n"
	$(Q)llvm-cov report $(TESS_EXE) \
		$(patsubst %,-object %,$(MOS_TEST_EXES) $(TESS_TEST_EXES)) \
		-instr-profile=$(COV_MERGED) \
		-ignore-filename-regex='vendor/|test_'
	@printf "\n  \033[1;33m[COV]\033[0m  Generating HTML report...\n"
	$(Q)llvm-cov show $(TESS_EXE) \
		$(patsubst %,-object %,$(MOS_TEST_EXES) $(TESS_TEST_EXES)) \
		-instr-profile=$(COV_MERGED) \
		-format=html -output-dir=$(COV_REPORT) \
		-ignore-filename-regex='vendor/|test_' \
		-show-line-counts-or-regions -show-expansions
	@printf "  \033[1;32m[COV]\033[0m  HTML report: %s/index.html\n\n" "$(COV_REPORT)"
else
	@printf "  \033[1;31m[ERROR]\033[0m coverage target requires CONFIG=coverage\n"; exit 1
endif

.PHONY: clean cleanall install test tags coverage
.PHONY: test-mos test-tess test-tl
.PHONY: build-mos-benchmarks bench-mos
.DEFAULT_GOAL := all
