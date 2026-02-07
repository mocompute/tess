#include "source_scanner.h"

#include "import_resolver.h"
#include "str.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

tl_source_scanner tl_source_scanner_create(allocator *arena, struct import_resolver *resolver) {
    return (tl_source_scanner){
      .arena                  = arena,
      .resolver               = resolver,
      .modules_seen           = map_new(arena, str, str, 8),
      .export_seen            = hset_create(arena, 8),
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
            self->current_file_module = words.v[1];
        }
    }

    return 0;
}

// Callback for each # directive found in source.
// data[start..end) is the directive text (after '#', up to and including '\n').
// Return 0 to continue scanning, non-zero to stop (value propagated to caller).
typedef int (*scan_directive_fn)(void *ctx, char const *data, u32 start, u32 end);

// Walk source text with string/comment awareness.
// Calls fn for each # directive at line start.
// Sets *out_has_export to 1 if [[export]] found (pass NULL if not needed).
// Returns 0 if all directives processed, or the non-zero return from fn.
static int scan_directives(char_csized input, scan_directive_fn fn, void *ctx, int *out_has_export) {
    u32         pos = 0, capture_start = 0;
    char const *data = input.v;
    u32         size = input.size;

    enum {
        start,
        noise,
        start_hash,
        in_hash,
        stop_hash,
        in_string,
        in_string_bs,
        in_comment
    } state        = start;

    int has_export = 0;

    while (pos < size) {
        switch (state) {
        case start: {
            char c = data[pos++];
            if ('#' == c) state = start_hash;
            else if ('"' == c) state = in_string;
            else if ('[' == c && pos + 8 <= size && 0 == memcmp(&data[pos], "[export]]", 9)) {
                pos += 9;
                has_export = 1;
                state      = noise;
            } else if ('/' == c && pos < size && '/' == data[pos]) {
                pos++;
                state = in_comment;
            } else if (isspace(c)) state = start;
            else state = noise;
        } break;
        case noise: {
            char c = data[pos++];
            if ('\n' == c) state = start;
            else if ('"' == c || '/' == c || '[' == c) {
                pos--;
                state = start;
            }
        } break;
        case in_comment: {
            char c = data[pos++];
            if ('\n' == c) state = start;
        } break;
        case in_string: {
            char c = data[pos++];
            if ('"' == c) state = noise;
            else if ('\\' == c) state = in_string_bs;
        } break;
        case in_string_bs: {
            pos++; // skip escaped character
            state = in_string;
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
            int rc = fn(ctx, data, capture_start, pos);
            if (rc) return rc;
            state = start;
        } break;
        }
    }

    if (stop_hash == state) {
        int rc = fn(ctx, data, capture_start, pos);
        if (rc) return rc;
    }

    if (out_has_export) *out_has_export = has_export;

    return 0;
}

// Context and callback for the full scanner (tl_source_scanner_scan).
typedef struct {
    tl_source_scanner *self;
    str                file_path;
    str_array         *imports;
} scan_full_ctx;

static int scan_full_callback(void *raw_ctx, char const *data, u32 start, u32 end) {
    scan_full_ctx *ctx = raw_ctx;
    return process_hash_directive(ctx->self, ctx->file_path, ctx->imports, data, start, end);
}

int tl_source_scanner_scan(tl_source_scanner *self, str file_path, char_csized input, str_array *imports) {
    // Reset per-file transient state
    self->conditional_skip_depth  = 0;
    self->current_file_module     = (str){0};

    int           file_has_export = 0;
    scan_full_ctx ctx             = {.self = self, .file_path = file_path, .imports = imports};

    int           rc              = scan_directives(input, scan_full_callback, &ctx, &file_has_export);
    if (rc) return rc;

    if (file_has_export && !str_is_empty(self->current_file_module)) {
        str_hset_insert(&self->export_seen, self->current_file_module);
    }

    return 0;
}

// Context and callback for collect_imports (no conditionals, imports only).
typedef struct {
    allocator *alloc;
    str_array *imports;
} collect_imports_ctx;

static int collect_imports_callback(void *raw_ctx, char const *data, u32 start, u32 end) {
    collect_imports_ctx *ctx = raw_ctx;

    u32                  len = end - start;
    if (len > 0 && data[end - 1] == '\n') len -= 1;
    if (len > 0 && data[end - 2] == '\r') len -= 1;

    str       command = str_init_n(ctx->alloc, &data[start], len);
    str_array words   = {.alloc = ctx->alloc};
    str_parse_words(command, &words);

    if (words.size >= 2 && str_eq(words.v[0], S("import"))) {
        array_push(*ctx->imports, words.v[1]);
    }

    return 0;
}

void tl_source_scanner_collect_imports(allocator *alloc, char_csized input, str_array *imports) {
    collect_imports_ctx ctx = {.alloc = alloc, .imports = imports};
    scan_directives(input, collect_imports_callback, &ctx, null);
}

tl_source_scanner_validate_result tl_source_scanner_validate(tl_source_scanner *self,
                                                             str const         *manifest_modules,
                                                             u32 manifest_module_count, int verbose) {

    tl_source_scanner_validate_result result = {0, 0};

    if (manifest_module_count == 0) return result;

    // Build hashset of manifest module names for O(1) lookup
    hashmap *manifest_set = hset_create(self->arena, manifest_module_count * 2);
    for (u32 i = 0; i < manifest_module_count; i++) {
        str_hset_insert(&manifest_set, manifest_modules[i]);
    }

    // Check 1: every manifest module must exist in modules_seen
    for (u32 i = 0; i < manifest_module_count; i++) {
        str m = manifest_modules[i];
        if (!str_map_contains(self->modules_seen, m)) {
            fprintf(stderr,
                    "error: manifest declares module '%s' but no #module directive found in source\n",
                    str_cstr(&m));
            result.error_count++;
        }
    }

    // If errors found, skip warnings (they depend on correct module discovery)
    if (result.error_count > 0) return result;

    // Check 2: public module with no [[export]] symbols
    for (u32 i = 0; i < manifest_module_count; i++) {
        str m = manifest_modules[i];
        if (!str_hset_contains(self->export_seen, m)) {
            fprintf(stderr, "warning: module '%s' is listed as public but has no [[export]] symbols\n",
                    str_cstr(&m));
            result.warning_count++;
        }
    }

    // Check 3: non-public module with [[export]] symbols
    hashmap_iterator iter = {0};
    while (hset_iter(self->export_seen, &iter)) {
        str name = str_init_n(self->arena, iter.key_ptr, iter.key_size);
        if (!str_hset_contains(manifest_set, name)) {
            fprintf(stderr,
                    "warning: module '%s' has [[export]] symbols but is not listed in manifest modules\n",
                    str_cstr(&name));
            result.warning_count++;
        }
        str_deinit(self->arena, &name);
    }

    // Verbose: list internal modules (in modules_seen but not in manifest)
    if (verbose) {
        str_array keys         = str_map_sorted_keys(self->arena, self->modules_seen);
        int       has_internal = 0;
        forall(i, keys) {
            if (!str_hset_contains(manifest_set, keys.v[i])) {
                if (!has_internal) {
                    fprintf(stderr, "Internal modules:\n");
                    has_internal = 1;
                }
                fprintf(stderr, "  (internal) %s\n", str_cstr(&keys.v[i]));
            }
        }
    }

    return result;
}
