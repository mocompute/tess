#include "v2_infer.h"
#include "alloc.h"
#include "dbg.h"
#include "error.h"
#include "str.h"
#include "v2_type.h"

#include "ast.h"
#include "hashmap.h"

#include "types.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct {
    enum tl_error_tag tag;
    ast_node const   *node;
} tl_infer_error;

typedef struct {
    array_header;
    tl_infer_error *v;
} tl_infer_error_array;

struct tl_infer {
    allocator           *transient;
    allocator           *arena;

    tl_type_context      context;
    tl_type_env         *env;

    hashmap             *toplevels;
    tl_infer_error_array errors;

    int                  verbose;
    int                  indent_level;
};

//

static void log(tl_infer const *self, char const *restrict fmt, ...);
static void log_toplevels(tl_infer const *);

//

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self     = new (alloc, tl_infer);

    self->transient    = arena_create(alloc, 4096);
    self->arena        = arena_create(alloc, 16 * 1024);

    self->toplevels    = null;
    self->context      = tl_type_context_empty();
    self->env          = tl_type_env_create(alloc);

    self->errors       = (tl_infer_error_array){.alloc = self->arena};

    self->verbose      = 0;
    self->indent_level = 0;

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    tl_type_env_destroy(alloc, &(*p)->env);
    map_destroy(&(*p)->toplevels);

    arena_destroy(alloc, &(*p)->transient);
    arena_destroy(alloc, &(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose = verbose;
}

static hashmap *load_toplevel(allocator *alloc, ast_node_sized nodes, tl_infer_error_array *out_errors) {
    hashmap             *tops   = map_create(alloc, sizeof(ast_node *), 1024);
    tl_infer_error_array errors = {.alloc = alloc};

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        if (ast_symbol == node->tag) {
            str        name_str = node->symbol.name;
            ast_node **p        = str_map_get(tops, name_str);
            if (p) {
                // merge annotation if existing node is a let node; otherwise error
                if (ast_let != (*p)->tag) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                if (node->symbol.annotation) (*p)->let.name->symbol.annotation = node->symbol.annotation;
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&tops, name_str, &node);
                }
            }
        }

        else if (ast_let == node->tag) {
            str        name_str = ast_node_str(node->let.name);
            ast_node **p        = str_map_get(tops, name_str);
            if (p) {
                // merge type if the existing node is a symbol; otherwise error
                if (ast_symbol != (*p)->tag) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                // ignore prior type annotation if the current symbol is annotated: later declaration
                // overrides
                if (node->let.name->symbol.annotation) continue;

                // apply annotation
                node->let.name->symbol.annotation = (*p)->symbol.annotation;

                // replace prior symbol entry with let node
                *p = node;
            } else {
                str_map_set(&tops, name_str, &node);
            }
        }

        else if (ast_user_type_definition == node->tag) {
            str        name_str = ast_node_str(node->user_type_def.name);
            ast_node **p        = str_map_get(tops, name_str);

            if (p) {
                array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            } else {
                str_map_set(&tops, name_str, &node);
            }
        }

        else {
            array_push(errors, ((tl_infer_error){.tag = tl_err_invalid_toplevel, .node = node}));
            continue;
        }
    }

    *out_errors = errors;
    return tops;
}

int tl_infer_run(tl_infer *self, ast_node_sized nodes) {

    log(self, "-- start inference --");

    self->toplevels = load_toplevel(self->arena, nodes, &self->errors);

    if (self->errors.size) {
        return 1;
    }

    log_toplevels(self);

    return 0;
}

void tl_infer_report_errors(tl_infer *self) {
    if (self->errors.size) {
        forall(i, self->errors) {
            tl_infer_error *err  = &self->errors.v[i];
            ast_node const *node = err->node;

            if (node)
                fprintf(stderr, "%s:%u: %s: %s\n", node->file, node->line, tl_error_tag_to_string(err->tag),
                        ast_node_to_string_for_error(self->transient, node));

            else fprintf(stderr, "error: %s\n", tl_error_tag_to_string(err->tag));
        }
    }
}

//

static void log(tl_infer const *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "tl_infer: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}

static void log_toplevels(tl_infer const *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->toplevels, &iter)) {
        ast_node const *node = *(ast_node **)iter.data;
        char           *cstr = ast_node_to_string(self->transient, node);
        log(self, cstr);
        alloc_free(self->transient, cstr);
    }
}
