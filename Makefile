CC     ?= cc
CFLAGS ?= -O2 -DNDEBUG
CFLAGS += -std=gnu11 -fPIE
CFLAGS += -Werror -Wall -Wextra -Wswitch-enum -Wunused -Winline -Wimplicit-fallthrough
CFLAGS += -Wno-gnu-alignof-expression


# Directories
MOS_SRC_DIR	= src/mos
MOS_INC_DIR	= $(MOS_SRC_DIR)/include
TESS_SRC_DIR	= src/tess
TESS_INC_DIR	= $(TESS_SRC_DIR)/include
BUILD_DIR	= build

# Installation directories
PREFIX ?= /usr/local
INSTALL_BIN = $(DESTDIR)$(PREFIX)/bin
INSTALL_LIB = $(DESTDIR)$(PREFIX)/lib/tess

# Include paths
CPPFLAGS        = -I$(MOS_INC_DIR) -I$(TESS_INC_DIR)

# Linker flags
LDFLAGS ?=

# mos library sources
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

# tess library sources (excluding tess_embed.c for now)
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

# Embedded file handling
EMBED_TOOL = $(BUILD_DIR)/mos_embed
TESS_EMBED_SRC = $(BUILD_DIR)/tess_embed.c
TESS_EMBED_OBJ = $(BUILD_DIR)/tess/tess_embed.o

# tess executable
TESS_EXE_SRC = $(TESS_SRC_DIR)/src/tess_exe.c
TESS_EXE_OBJ = $(BUILD_DIR)/tess_exe.o
TESS_EXE = tess

# Default target
all: $(TESS_EXE)

$(TESS_EXE): $(TESS_EXE_OBJ) $(TESS_OBJECTS) $(TESS_EMBED_OBJ) $(MOS_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(EMBED_TOOL): $(MOS_SRC_DIR)/src/embed.c $(MOS_OBJECTS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

$(TESS_EMBED_SRC): $(EMBED_TOOL) $(TESS_SRC_DIR)/embed/std.c
	$(EMBED_TOOL) $(TESS_SRC_DIR)/embed $(TESS_EMBED_SRC)

$(BUILD_DIR)/mos/%.o: $(MOS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD_DIR)/tess/%.o: $(TESS_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TESS_EMBED_OBJ): $(TESS_EMBED_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TESS_EXE_OBJ): $(TESS_EXE_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TESS_EXE)

install: $(TESS_EXE)
	install -D -m 755 $(TESS_EXE) $(INSTALL_BIN)/$(TESS_EXE)
	@mkdir -p $(INSTALL_LIB)/std
	cd src/tl/std && find . -name '*.tl' -exec install -D -m 644 {} $(CURDIR)/$(INSTALL_LIB)/{} \;

.PHONY: all clean install
