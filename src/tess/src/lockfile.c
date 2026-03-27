#include "lockfile.h"
#include "array.h"
#include "ast.h"
#include "file.h"
#include "parser.h"
#include "platform.h"
#include "str.h"
#include "type.h"
#include "type_registry.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static str extract_string(allocator *alloc, ast_node *node, char const *func_name, int arg_index) {

    if (ast_node_is_nfa(node) && node->named_application.n_arguments == 1) {
        node = node->named_application.arguments[0];
    }

    if (!ast_node_is_string(node)) goto fail;
    str out = ast_node_str(node);
    if (str_is_empty(out)) goto fail;
    return str_copy(alloc, out);

fail:
    fprintf(stderr, "package.tl.lock: error: %s() argument %d is not a string literal\n", func_name,
            arg_index + 1);
    return str_empty();
}

// Like extract_string but allows empty strings (for optional base_url field).
// Returns 0 on success (including empty string), 1 on error (not a string node).
static int extract_string_or_empty(allocator *alloc, ast_node *node, char const *func_name, int arg_index,
                                   str *out) {
    if (ast_node_is_nfa(node) && node->named_application.n_arguments == 1) {
        node = node->named_application.arguments[0];
    }

    if (!ast_node_is_string(node)) {
        fprintf(stderr, "package.tl.lock: error: %s() argument %d is not a string literal\n", func_name,
                arg_index + 1);
        *out = str_empty();
        return 1;
    }
    str val = ast_node_str(node);
    *out    = str_is_empty(val) ? str_empty() : str_copy(alloc, val);
    return 0;
}

static str extract_identifier(allocator *alloc, ast_node *node, char const *func_name, int arg_index) {

    if (!node || node->tag != ast_symbol) goto fail;
    str out = node->symbol.name;
    if (str_is_empty(out)) goto fail;
    return str_copy(alloc, out);

fail:
    fprintf(stderr, "package.tl.lock: error: %s() argument %d is not an identifier\n", func_name,
            arg_index + 1);
    return str_empty();
}

static i64 extract_int(ast_node *node, char const *func_name, int arg_index) {
    if (node->tag != ast_i64) {
        fprintf(stderr, "package.tl.lock: error: %s() argument %d is not an integer literal\n", func_name,
                arg_index + 1);
        return -1;
    }
    return node->i64.val;
}

// ---------------------------------------------------------------------------
// Dynamic arrays
// ---------------------------------------------------------------------------

defarray(locked_dep_array, tl_locked_dep);
defarray(lock_edge_array, tl_lock_edge);

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

int tl_lockfile_parse_file(allocator *alloc, char const *path, tl_lockfile *out) {
    memset(out, 0, sizeof(*out));

    str path_str = str_init_static(path);
    if (!file_exists(path_str)) {
        fprintf(stderr, "error: '%s' not found\n", path);
        return 1;
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
        tl_type_registry_destroy(registry);
        arena_destroy(&parse_arena);
        return 1;
    }

    // Dynamic arrays for building results
    locked_dep_array deps        = {.alloc = alloc};
    lock_edge_array  edges       = {.alloc = alloc};

    int              error       = 0;
    int              format_seen = 0;

    // Walk AST nodes
    for (u32 i = 0; i < nodes.size; i++) {
        ast_node *node = nodes.v[i];

        if (node->tag != ast_named_function_application) {
            fprintf(stderr, "package.tl.lock: error: unexpected node type at top level\n");
            error = 1;
            continue;
        }

        struct ast_named_application *nfa       = &node->named_application;
        str                           func_name = nfa->name->symbol.name;
        u8                            argc      = nfa->n_arguments;

        if (str_eq(func_name, S("lock_format"))) {
            if (i != 0) {
                fprintf(stderr, "package.tl.lock: error: lock_format() must be the first declaration\n");
                error = 1;
                continue;
            }
            if (argc != 1) {
                fprintf(stderr,
                        "package.tl.lock: error: lock_format() expects 1 integer argument, got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            i64 val = extract_int(nfa->arguments[0], "lock_format", 0);
            if (val < 0) {
                error = 1;
                continue;
            }
            if (val != 1) {
                fprintf(stderr, "package.tl.lock: error: unsupported lock_format version %d (expected 1)\n",
                        (int)val);
                error = 1;
                continue;
            }
            out->format = (u32)val;
            format_seen = 1;

        } else if (str_eq(func_name, S("locked"))) {
            if (argc != 4) {
                fprintf(stderr, "package.tl.lock: error: locked() expects 4 arguments, got %d\n",
                        (int)argc);
                error = 1;
                continue;
            }
            tl_locked_dep dep = {0};
            dep.name          = extract_identifier(alloc, nfa->arguments[0], "locked", 0);
            if (str_is_empty(dep.name)) {
                error = 1;
                continue;
            }
            dep.version = extract_string(alloc, nfa->arguments[1], "locked", 1);
            if (str_is_empty(dep.version)) {
                error = 1;
                continue;
            }
            if (extract_string_or_empty(alloc, nfa->arguments[2], "locked", 2, &dep.base_url)) {
                error = 1;
                continue;
            }
            dep.hash = extract_string(alloc, nfa->arguments[3], "locked", 3);
            if (str_is_empty(dep.hash)) {
                error = 1;
                continue;
            }
            if (!str_starts_with(dep.hash, S("sha256:"))) {
                fprintf(stderr, "package.tl.lock: error: locked() hash must start with \"sha256:\"\n");
                error = 1;
                continue;
            }
            array_push(deps, dep);

        } else if (str_eq(func_name, S("needs"))) {
            if (argc != 4) {
                fprintf(stderr, "package.tl.lock: error: needs() expects 4 arguments, got %d\n", (int)argc);
                error = 1;
                continue;
            }
            tl_lock_edge edge = {0};
            edge.name         = extract_identifier(alloc, nfa->arguments[0], "needs", 0);
            if (str_is_empty(edge.name)) {
                error = 1;
                continue;
            }
            edge.version = extract_string(alloc, nfa->arguments[1], "needs", 1);
            if (str_is_empty(edge.version)) {
                error = 1;
                continue;
            }
            edge.dep_name = extract_identifier(alloc, nfa->arguments[2], "needs", 2);
            if (str_is_empty(edge.dep_name)) {
                error = 1;
                continue;
            }
            edge.dep_version = extract_string(alloc, nfa->arguments[3], "needs", 3);
            if (str_is_empty(edge.dep_version)) {
                error = 1;
                continue;
            }
            array_push(edges, edge);
        }
    }

    // Validate required fields
    if (!format_seen) {
        fprintf(stderr, "package.tl.lock: error: missing required lock_format() declaration\n");
        error = 1;
    }

    // Copy dynamic arrays to output
    if (deps.size > 0) {
        out->deps      = deps.v;
        out->dep_count = deps.size;
    }
    if (edges.size > 0) {
        out->edges      = edges.v;
        out->edge_count = edges.size;
    }

    // Cleanup parser
    parser_destroy(&p);
    tl_type_registry_destroy(registry);
    arena_destroy(&parse_arena);

    return error ? 1 : 0;
}

int tl_lockfile_parse(allocator *alloc, char const *content, u32 content_len, tl_lockfile *out) {
    platform_temp_file tf;
    if (platform_temp_file_create(&tf, ".tl")) {
        fprintf(stderr, "package.tl.lock: error: failed to create temp file\n");
        return 1;
    }

    if (file_write(tf.path, content, content_len)) {
        platform_temp_file_delete(&tf);
        fprintf(stderr, "package.tl.lock: error: failed to write temp file\n");
        return 1;
    }

    int rc = tl_lockfile_parse_file(alloc, tf.path, out);
    platform_temp_file_delete(&tf);
    return rc;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

int tl_lockfile_write(char const *path, tl_locked_dep const *deps, u32 dep_count, tl_lock_edge const *edges,
                      u32 edge_count) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("lockfile: failed to open output file");
        return 1;
    }

    fprintf(f, "lock_format(1)\n\n");

    for (u32 i = 0; i < dep_count; i++) {
        fprintf(f, "locked(%.*s, \"%.*s\", \"%.*s\", \"%.*s\")\n", (int)str_len(deps[i].name),
                str_buf(&deps[i].name), (int)str_len(deps[i].version), str_buf(&deps[i].version),
                (int)str_len(deps[i].base_url), str_buf(&deps[i].base_url), (int)str_len(deps[i].hash),
                str_buf(&deps[i].hash));
    }

    if (edge_count > 0) {
        fprintf(f, "\n");
        for (u32 i = 0; i < edge_count; i++) {
            fprintf(f, "needs(%.*s, \"%.*s\", %.*s, \"%.*s\")\n", (int)str_len(edges[i].name),
                    str_buf(&edges[i].name), (int)str_len(edges[i].version), str_buf(&edges[i].version),
                    (int)str_len(edges[i].dep_name), str_buf(&edges[i].dep_name),
                    (int)str_len(edges[i].dep_version), str_buf(&edges[i].dep_version));
        }
    }

    fclose(f);
    return 0;
}
