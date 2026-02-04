#include "import_resolver.h"
#include "file.h"

#include <stdio.h>
#include <string.h>

struct import_resolver {
    allocator *arena;
    str_array  user_include_paths;     // -I paths
    str_array  standard_include_paths; // Standard library paths
    hashmap   *imported_files;         // Files fully imported (dedup)
    hashmap   *active_imports;         // Files currently being imported (cycle detect)
};

import_resolver *import_resolver_create(allocator *arena) {
    import_resolver *self = alloc_malloc(arena, sizeof(import_resolver));
    self->arena           = arena;
    self->user_include_paths     = (str_array){.alloc = arena};
    self->standard_include_paths = (str_array){.alloc = arena};
    self->imported_files         = hset_create(arena, 32);
    self->active_imports         = hset_create(arena, 16);
    return self;
}

void import_resolver_destroy(import_resolver **self) {
    if (!self || !*self) return;
    // Arena-allocated, nothing to free individually
    *self = null;
}

void import_resolver_add_user_path(import_resolver *self, str path) {
    array_push(self->user_include_paths, path);
}

void import_resolver_add_standard_path(import_resolver *self, str path) {
    array_push(self->standard_include_paths, path);
}

import_kind import_resolver_get_kind(str quoted_path) {
    span s = str_span(&quoted_path);
    if (s.len >= 2 && s.buf[0] == '<' && s.buf[s.len - 1] == '>') {
        return IMPORT_ANGLE_BRACKET;
    }
    return IMPORT_QUOTED;
}

str import_resolver_strip_quotes(allocator *alloc, str quoted_path) {
    span s = str_span(&quoted_path);
    if (s.len < 2) return str_empty();

    char first = s.buf[0];
    char last  = s.buf[s.len - 1];

    // Check for matching quotes/brackets
    if ((first == '"' && last == '"') || (first == '<' && last == '>')) {
        return str_init_n(alloc, s.buf + 1, s.len - 2);
    }

    return str_empty();
}

void import_resolver_print_paths(import_resolver *self) {
    fprintf(stderr, "  User include paths (-I):\n");
    if (self->user_include_paths.size == 0) {
        fprintf(stderr, "    (none)\n");
    } else {
        forall(i, self->user_include_paths) {
            fprintf(stderr, "    %s\n", str_cstr(&self->user_include_paths.v[i]));
        }
    }

    fprintf(stderr, "  Standard library paths:\n");
    if (self->standard_include_paths.size == 0) {
        fprintf(stderr, "    (none)\n");
    } else {
        forall(i, self->standard_include_paths) {
            fprintf(stderr, "    %s\n", str_cstr(&self->standard_include_paths.v[i]));
        }
    }
}

static str try_resolve_path(allocator *alloc, str dir, str file) {
    str joined = file_path_join(alloc, dir, file);
    str normed = file_path_normalize(alloc, str_cstr(&joined));

    if (!str_is_empty(normed) && file_exists(str_cstr(&normed))) {
        return normed;
    }
    return str_empty();
}

import_result import_resolver_resolve(import_resolver *self,
                                      str              import_path,
                                      str              importing_file) {
    import_result result = {.canonical_path = str_empty(), .is_duplicate = 0};

    // Determine import kind and strip quotes
    import_kind kind     = import_resolver_get_kind(import_path);
    str         path     = import_resolver_strip_quotes(self->arena, import_path);

    if (str_is_empty(path)) {
        fprintf(stderr, "error: invalid import syntax: %s\n", str_cstr(&import_path));
        fprintf(stderr, "  imports must use \"...\" or <...>\n");
        return result;
    }

    // Reject absolute paths
    if (file_is_absolute(str_cstr(&path))) {
        fprintf(stderr, "error: absolute paths not allowed in imports: %s\n",
                str_cstr(&import_path));
        return result;
    }

    str resolved = str_empty();

    if (kind == IMPORT_ANGLE_BRACKET) {
        // Angle bracket: search ONLY standard paths
        forall(i, self->standard_include_paths) {
            resolved = try_resolve_path(self->arena, self->standard_include_paths.v[i], path);
            if (!str_is_empty(resolved)) break;
        }

        if (str_is_empty(resolved)) {
            fprintf(stderr, "error: import not found: %s\n", str_cstr(&import_path));
            fprintf(stderr, "  Angle bracket imports search only standard library paths:\n");
            forall(i, self->standard_include_paths) {
                fprintf(stderr, "    %s\n", str_cstr(&self->standard_include_paths.v[i]));
            }
            return result;
        }
    } else {
        // Quoted: try relative to importing file first, then -I paths
        // Do NOT search standard paths for quoted imports

        // 1. Try relative to importing file's directory
        if (!str_is_empty(importing_file)) {
            str dir = file_dirname(self->arena, str_cstr(&importing_file));
            if (!str_is_empty(dir)) {
                resolved = try_resolve_path(self->arena, dir, path);
            }
        }

        // 2. Try user include paths (-I)
        if (str_is_empty(resolved)) {
            forall(i, self->user_include_paths) {
                resolved = try_resolve_path(self->arena, self->user_include_paths.v[i], path);
                if (!str_is_empty(resolved)) break;
            }
        }

        if (str_is_empty(resolved)) {
            fprintf(stderr, "error: import not found: %s\n", str_cstr(&import_path));
            fprintf(stderr, "  Quoted imports search:\n");
            if (!str_is_empty(importing_file)) {
                str dir = file_dirname(self->arena, str_cstr(&importing_file));
                if (!str_is_empty(dir)) {
                    fprintf(stderr, "    Relative to: %s\n", str_cstr(&dir));
                }
            }
            fprintf(stderr, "  User include paths (-I):\n");
            if (self->user_include_paths.size == 0) {
                fprintf(stderr, "    (none)\n");
            } else {
                forall(i, self->user_include_paths) {
                    fprintf(stderr, "    %s\n", str_cstr(&self->user_include_paths.v[i]));
                }
            }
            return result;
        }
    }

    // Check for duplicates
    if (str_hset_contains(self->imported_files, resolved)) {
        result.canonical_path = resolved;
        result.is_duplicate   = 1;
        return result;
    }

    result.canonical_path = resolved;
    result.is_duplicate   = 0;
    return result;
}

int import_resolver_begin_import(import_resolver *self, str canonical_path) {
    if (str_hset_contains(self->active_imports, canonical_path)) {
        return 1; // Cycle detected
    }
    str_hset_insert(&self->active_imports, canonical_path);
    return 0;
}

void import_resolver_end_import(import_resolver *self, str canonical_path) {
    str_hset_remove(self->active_imports, canonical_path);
}

int import_resolver_is_imported(import_resolver *self, str canonical_path) {
    return str_hset_contains(self->imported_files, canonical_path);
}

void import_resolver_mark_imported(import_resolver *self, str canonical_path) {
    str_hset_insert(&self->imported_files, canonical_path);
}

int import_resolver_is_stdlib_file(import_resolver *self, str canonical_path) {
    forall(i, self->standard_include_paths) {
        str std_path = self->standard_include_paths.v[i];
        if (str_starts_with(canonical_path, std_path)) {
            // Also check that the next char is a path separator or end of string
            size_t std_len = str_len(std_path);
            size_t path_len = str_len(canonical_path);
            if (path_len == std_len ||
                str_buf(&canonical_path)[std_len] == '/' ||
                str_buf(&canonical_path)[std_len] == '\\') {
                return 1;
            }
        }
    }
    return 0;
}
