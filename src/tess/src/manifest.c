#include "manifest.h"
#include "ast.h"
#include "file.h"
#include "parser.h"
#include "str.h"
#include "type.h"
#include "type_registry.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Extract a string from a DSL argument node.
// Returns the string value, or an empty str on failure (with error printed).
static str extract_string(allocator *alloc, ast_node *node, char const *func_name, int arg_index) {

    if (ast_node_is_nfa(node) && node->named_application.n_arguments == 1) {
        // accept any unary NFA with a string as first argument: This will accept
        // String literals which are mangled to String.from_literal/1 calls by the parser.
        node = node->named_application.arguments[0];
    }

    if (!ast_node_is_string(node)) goto fail;
    str out = ast_node_str(node);
    if (str_is_empty(out)) goto fail;
    return str_copy(alloc, out);

fail:
    fprintf(stderr, "package.tl: error: %s() argument %d is not a string literal\n", func_name,
            arg_index + 1);
    return str_empty();
}

// Extract an identifier (bare symbol) from a DSL argument node.
// Returns the symbol name, or an empty str on failure (with error printed).
static str extract_identifier(allocator *alloc, ast_node *node, char const *func_name, int arg_index) {

    if (!node || node->tag != ast_symbol) goto fail;
    str out = node->symbol.name;
    if (str_is_empty(out)) goto fail;
    return str_copy(alloc, out);

fail:
    fprintf(stderr, "package.tl: error: %s() argument %d is not an identifier\n", func_name, arg_index + 1);
    return str_empty();
}

// Extract an integer from a DSL argument node (ast_i64).
// Returns the value, or -1 on failure (with error printed).
static i64 extract_int(ast_node *node, char const *func_name, int arg_index) {
    if (node->tag != ast_i64) {
        fprintf(stderr, "package.tl: error: %s() argument %d is not an integer literal\n", func_name,
                arg_index + 1);
        return -1;
    }
    return node->i64.val;
}

// ---------------------------------------------------------------------------
// Dynamic array for tl_package_dep
// ---------------------------------------------------------------------------

typedef struct {
    tl_package_dep *v;
    allocator      *alloc;
    u32             size;
    u32             capacity;
} dep_array;

static void dep_array_push(dep_array *a, tl_package_dep dep) {
    if (a->size == a->capacity) {
        u32             new_cap = a->capacity ? a->capacity * 2 : 4;
        tl_package_dep *new_v   = alloc_malloc(a->alloc, new_cap * sizeof(tl_package_dep));
        if (a->v) memcpy(new_v, a->v, a->size * sizeof(tl_package_dep));
        a->v        = new_v;
        a->capacity = new_cap;
    }
    a->v[a->size++] = dep;
}

// ---------------------------------------------------------------------------
// DSL interpreter
// ---------------------------------------------------------------------------

int tl_package_parse_file(allocator *alloc, char const *path, tl_package *out) {
    memset(out, 0, sizeof(*out));
    out->info.name    = str_empty();
    out->info.version = str_empty();
    out->info.author  = str_empty();

    // Read file
    char *data     = 0;
    u32   size     = 0;
    str   path_str = str_init_static(path);
    if (!file_exists(path_str)) {
        fprintf(stderr, "error: '%s' not found (required for pack/validate commands)\n", path);
        return 1;
    }
    {
        // test if file is readable
        file_read(alloc, path, &data, &size);
        if (!data) {
            fprintf(stderr, "error: cannot read '%s'\n", path);
            return 1;
        }
        alloc_free(alloc, data);
    }

    // Create minimal type infrastructure for parser
    allocator        *parse_arena = arena_create(default_allocator(), 4096);
    tl_type_subs     *subs        = tl_type_subs_create(parse_arena);
    tl_type_registry *registry    = tl_type_registry_create(parse_arena, parse_arena, subs);

    // Set up parser opts
    str         file_str = str_init(parse_arena, path);
    str_sized   files    = {.v = &file_str, .size = 1};

    parser_opts opts     = {
          .registry = registry,
          .files    = files,
          .prelude  = null,
          .defines  = {0},
    };

    parser *p = parser_create(parse_arena, &opts);

    // Parse toplevel funcalls
    ast_node_array nodes    = {.alloc = parse_arena};
    int            parse_rc = parser_parse_all_toplevel_funcalls(p, &nodes);
    if (parse_rc) {
        parser_report_errors(p);
        parser_destroy(&p);
        arena_destroy(&parse_arena);
        return 1;
    }

    // Dynamic arrays for building results
    str_array exports      = {.alloc = alloc};
    str_array depend_paths = {.alloc = alloc};
    str_array sources      = {.alloc = alloc};
    dep_array deps         = {.alloc = alloc};
    dep_array opt_deps     = {.alloc = alloc};

    int       error        = 0;
    int       format_seen  = 0;
    int       package_seen = 0;
    int       version_seen = 0;

    // Walk AST nodes
    for (u32 i = 0; i < nodes.size; i++) {
        ast_node *node = nodes.v[i];

        if (node->tag != ast_named_function_application) {
            fprintf(stderr, "package.tl: error: unexpected node type at top level\n");
            error = 1;
            continue;
        }

        struct ast_named_application *nfa       = &node->named_application;
        str                           func_name = nfa->name->symbol.name;
        u8                            argc      = nfa->n_arguments;

        if (str_eq(func_name, S("format"))) {
            if (i != 0) {
                fprintf(stderr, "package.tl: error: format() must be the first declaration\n");
                error = 1;
                continue;
            }
            if (argc != 1) {
                fprintf(stderr, "package.tl: error: format() expects 1 integer argument, got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            i64 val = extract_int(nfa->arguments[0], "format", 0);
            if (val < 0) {
                error = 1;
                continue;
            }
            if (val != 1) {
                fprintf(stderr, "package.tl: error: unsupported format version %d (expected 1)\n",
                        (int)val);
                error = 1;
                continue;
            }
            out->info.format = (u32)val;
            format_seen      = 1;

        } else if (str_eq(func_name, S("package"))) {
            if (package_seen) {
                fprintf(stderr, "package.tl: error: duplicate package() declaration\n");
                error = 1;
                continue;
            }
            if (argc != 1) {
                fprintf(stderr, "package.tl: error: package() expects 1 identifier argument, got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            out->info.name = extract_identifier(alloc, nfa->arguments[0], "package", 0);
            if (str_is_empty(out->info.name)) {
                error = 1;
                continue;
            }
            package_seen = 1;

        } else if (str_eq(func_name, S("version"))) {
            if (version_seen) {
                fprintf(stderr, "package.tl: error: duplicate version() declaration\n");
                error = 1;
                continue;
            }
            if (argc != 1) {
                fprintf(stderr, "package.tl: error: version() expects 1 string argument(s), got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            out->info.version = extract_string(alloc, nfa->arguments[0], "version", 0);
            if (str_is_empty(out->info.version)) {
                error = 1;
                continue;
            }
            version_seen = 1;

        } else if (str_eq(func_name, S("author"))) {
            if (argc != 1) {
                fprintf(stderr, "package.tl: error: author() expects 1 string argument(s), got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            out->info.author = extract_string(alloc, nfa->arguments[0], "author", 0);
            if (str_is_empty(out->info.author)) {
                error = 1;
                continue;
            }

        } else if (str_eq(func_name, S("export"))) {
            if (argc < 1) {
                fprintf(stderr,
                        "package.tl: error: export() expects at least 1 identifier argument, got 0\n");
                error = 1;
                continue;
            }
            for (u8 j = 0; j < argc; j++) {
                str val = extract_identifier(alloc, nfa->arguments[j], "export", j);
                if (str_is_empty(val)) {
                    error = 1;
                    continue;
                }
                array_push(exports, val);
            }

        } else if (str_eq(func_name, S("depend"))) {
            if (argc < 2 || argc > 3) {
                fprintf(stderr, "package.tl: error: depend() expects 2-3 arguments, got %d\n", (int)argc);
                error = 1;
                continue;
            }
            tl_package_dep dep = {0};
            dep.name           = extract_identifier(alloc, nfa->arguments[0], "depend", 0);
            if (str_is_empty(dep.name)) {
                error = 1;
                continue;
            }
            dep.version = extract_string(alloc, nfa->arguments[1], "depend", 1);
            if (str_is_empty(dep.version)) {
                error = 1;
                continue;
            }
            dep.path = str_empty();
            if (argc == 3) {
                dep.path = extract_string(alloc, nfa->arguments[2], "depend", 2);
                if (str_is_empty(dep.path)) {
                    error = 1;
                    continue;
                }
            }
            dep_array_push(&deps, dep);

        } else if (str_eq(func_name, S("depend_optional"))) {
            if (argc < 2 || argc > 3) {
                fprintf(stderr, "package.tl: error: depend_optional() expects 2-3 arguments, got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            tl_package_dep dep = {0};
            dep.name           = extract_identifier(alloc, nfa->arguments[0], "depend_optional", 0);
            if (str_is_empty(dep.name)) {
                error = 1;
                continue;
            }
            dep.version = extract_string(alloc, nfa->arguments[1], "depend_optional", 1);
            if (str_is_empty(dep.version)) {
                error = 1;
                continue;
            }
            dep.path = str_empty();
            if (argc == 3) {
                dep.path = extract_string(alloc, nfa->arguments[2], "depend_optional", 2);
                if (str_is_empty(dep.path)) {
                    error = 1;
                    continue;
                }
            }
            dep_array_push(&opt_deps, dep);

        } else if (str_eq(func_name, S("depend_path"))) {
            if (argc != 1) {
                fprintf(stderr, "package.tl: error: depend_path() expects 1 string argument(s), got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            str val = extract_string(alloc, nfa->arguments[0], "depend_path", 0);
            if (str_is_empty(val)) {
                error = 1;
                continue;
            }
            array_push(depend_paths, val);

        } else if (str_eq(func_name, S("source"))) {
            if (argc < 1) {
                fprintf(stderr,
                        "package.tl: error: source() expects at least 1 string argument(s), got 0\n");
                error = 1;
                continue;
            }
            for (u8 j = 0; j < argc; j++) {
                str val = extract_string(alloc, nfa->arguments[j], "source", j);
                if (str_is_empty(val)) {
                    error = 1;
                    continue;
                }
                array_push(sources, val);
            }
        }
    }

    // Validate required fields
    if (!format_seen) {
        fprintf(stderr, "package.tl: error: missing required format() declaration\n");
        error = 1;
    }
    if (!package_seen) {
        fprintf(stderr, "package.tl: error: missing required package() declaration\n");
        error = 1;
    }
    if (!version_seen) {
        fprintf(stderr, "package.tl: error: missing required version() declaration\n");
        error = 1;
    }

    // Copy dynamic arrays to output
    if (exports.size > 0) {
        out->info.exports      = exports.v;
        out->info.export_count = exports.size;
    }
    if (depend_paths.size > 0) {
        out->info.depend_paths      = depend_paths.v;
        out->info.depend_path_count = depend_paths.size;
    }
    if (sources.size > 0) {
        out->info.sources      = sources.v;
        out->info.source_count = sources.size;
    }
    if (deps.size > 0) {
        out->deps      = deps.v;
        out->dep_count = deps.size;
    }
    if (opt_deps.size > 0) {
        out->optional_deps      = opt_deps.v;
        out->optional_dep_count = opt_deps.size;
    }

    // Cleanup parser
    parser_destroy(&p);
    arena_destroy(&parse_arena);

    return error ? 1 : 0;
}
