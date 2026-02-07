#include "source_scanner.h"

#include "import_resolver.h"

#include <ctype.h>
#include <stdio.h>

tl_source_scanner tl_source_scanner_create(allocator *arena, struct import_resolver *resolver) {
    return (tl_source_scanner){
      .arena                  = arena,
      .resolver               = resolver,
      .modules_seen           = map_new(arena, str, str, 8),
      .import_defines         = hset_create(arena, 8),
      .conditional_skip_depth = 0,
    };
}

void tl_source_scanner_define(tl_source_scanner *self, str symbol) {
    str_hset_insert(&self->import_defines, symbol);
}

// Process a single # directive line.
// Returns 0 on success, 1 on error (duplicate module).
static int process_hash_directive(tl_source_scanner *self, str file_path, str_array *imports,
                                  char const *data, u32 capture_start, u32 pos) {
    // Strip trailing newline so str_parse_words produces exact word counts.
    u32 len = pos - capture_start;
    if (len > 0 && data[pos - 1] == '\n') len -= 1;
    if (len > 0 && data[pos - 2] == '\r') len -= 1;

    int       is_stdlib_file = import_resolver_is_stdlib_file(self->resolver, file_path);

    str       command        = str_init_n(imports->alloc, &data[capture_start], len);
    str_array words          = {.alloc = imports->alloc};
    str_parse_words(command, &words);

    // Conditional compilation: same logic as tokenizer.c stop_hash_command.
    // When skip_depth > 0, tokens are skipped. #ifdef/#ifndef always increment
    // skip_depth when already skipping (to match #endif pairs), but #define/#undef
    // and #import are only processed at depth 0.
    if (words.size == 1 && str_eq(words.v[0], S("endif"))) {
        if (self->conditional_skip_depth) self->conditional_skip_depth -= 1;
    } else if (words.size == 2) {
        if (0 == self->conditional_skip_depth && str_eq(words.v[0], S("define"))) {
            str_hset_insert(&self->import_defines, words.v[1]);
        } else if (0 == self->conditional_skip_depth && str_eq(words.v[0], S("undef"))) {
            str_hset_remove(self->import_defines, words.v[1]);
        } else if (str_eq(words.v[0], S("ifdef"))) {
            if (self->conditional_skip_depth || !str_hset_contains(self->import_defines, words.v[1]))
                self->conditional_skip_depth += 1;
        } else if (str_eq(words.v[0], S("ifndef"))) {
            if (self->conditional_skip_depth || str_hset_contains(self->import_defines, words.v[1]))
                self->conditional_skip_depth += 1;
        }
    }

    if (0 == self->conditional_skip_depth && words.size >= 2) {
        if (str_eq(words.v[0], S("import"))) {
            array_push(*imports, words.v[1]);
        } else if (!is_stdlib_file && str_eq(words.v[0], S("module"))) {
            if (str_map_contains(self->modules_seen, words.v[1])) {
                str *existing = str_map_get(self->modules_seen, words.v[1]);
                fprintf(stderr, "error: module '%s' defined in '%s' was already defined in '%s'\n",
                        str_cstr(&words.v[1]), str_cstr(&file_path), str_cstr(existing));
                return 1;
            }

            str_map_set(&self->modules_seen, words.v[1], &file_path);
        }
    }

    return 0;
}

int tl_source_scanner_scan(tl_source_scanner *self, str file_path, char_csized input, str_array *imports) {
    u32         pos = 0, capture_start = 0;
    char const *data                                            = input.v;
    u32         size                                            = input.size;
    enum { start, noise, start_hash, in_hash, stop_hash } state = start;

    // Reset conditional skip depth in case of unmatched conditionals in previous file
    self->conditional_skip_depth = 0;
    while (pos < size) {
        switch (state) {
        case start: {
            char c = data[pos++];
            if ('#' == c) state = start_hash;
            else if (isspace(c)) state = start;
            else state = noise;
        } break;
        case noise: {
            char c = data[pos++];
            if ('\n' == c) state = start;
        } break;
        case start_hash: {
            capture_start = pos;
            state         = in_hash;
        } break;
        case in_hash: {
            char c = data[pos++];
            if ('\n' == c) state = stop_hash;
        } break;
        case stop_hash: {
            if (process_hash_directive(self, file_path, imports, data, capture_start, pos)) return 1;
            state = start;
        } break;
        }
    }

    if (stop_hash == state) {
        // catch command at end of file
        if (process_hash_directive(self, file_path, imports, data, capture_start, pos)) return 1;
    }

    return 0;
}
