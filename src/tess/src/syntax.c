#include "syntax.h"

#include "alloc.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "error.h"
#include "hashmap.h"
#include "mos_string.h"
#include "type_registry.h"

#include <stdio.h>

// -- forwards --

static void syntax_error(struct syntax_checker *, ast_node *, enum tess_error_tag, char const *message);

nodiscard static int syntax_check_type_annotations(struct syntax_checker *);
nodiscard static int syntax_register_user_types(struct syntax_checker *);

// -- syntax_checker --

struct syntax_error {
    ast_node   *node;
    char const *message; // optional
    // error code is in ast_node
};

struct syntax_checker {
    allocator           *alloc;
    allocator           *arena;
    type_registry       *type_registry;

    ast_node           **nodes;
    u32                  n_nodes;

    struct syntax_error *errors;
    u32                  n_errors;
    u32                  cap_errors;
};

// -- allocation and deallocation --

syntax_checker *syntax_checker_create(allocator *alloc, ast_node **nodes, u32 count) {
    syntax_checker *self = alloc_calloc(alloc, 1, sizeof *self);

    self->alloc          = alloc;
    self->arena          = alloc_arena_create(alloc, 2048);
    self->type_registry  = type_registry_create(self->arena);

    self->nodes          = nodes;
    self->n_nodes        = count;

    return self;
}

void syntax_checker_destroy(syntax_checker **self) {

    alloc_arena_destroy((*self)->alloc, &(*self)->arena);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

// -- syntax_checker operation --

int syntax_checker_run(syntax_checker *self) {

    int res = 0;
    if ((res = syntax_register_user_types(self))) return res;
    if ((res = syntax_check_type_annotations(self))) return res;

    return (int)self->n_errors;
}

void syntax_checker_report_errors(syntax_checker *self) {
    if (self->n_errors == 0) return;

    for (u32 i = 0; i < self->n_errors; ++i) {

        char *str = ast_node_to_string_for_error(self->arena, self->errors[i].node);

        if (self->errors[i].message)
            fprintf(stderr, "error: %s: %s: %s\n", tess_error_tag_to_string(self->errors[i].node->error),
                    self->errors[i].message, str);

        else fprintf(stderr, "error: %s: %s\n", tess_error_tag_to_string(self->errors[i].node->error), str);

        alloc_free(self->arena, str);
    }
}

// -- register_user_types --

static void register_user_type(void *ctx, ast_node *node) {
    struct syntax_checker *self = ctx;

    if (ast_user_defined_type != node->tag) return;
    if (node->user_type.field_types) return;

    char const *type_name = mos_string_str(&node->user_type.name->symbol.name);

    if (type_registry_find(self->type_registry, type_name)) {

        int   len     = snprintf(null, 0, "%s", type_name) + 1;
        char *message = alloc_malloc(self->arena, (u32)len);
        snprintf(message, (u32)len, "%s", type_name);
        syntax_error(self, node, tess_err_type_exists, message);

        return;
    }

    u16 const    n_fields    = node->user_type.n_fields;
    char const **field_names = null;
    if (n_fields) {

        // convert symbol annotations to actual types

        node->user_type.field_types =
          alloc_calloc(self->arena, n_fields, sizeof node->user_type.field_types[0]);

        for (u32 i = 0; i < n_fields; ++i) {

            char const        *str = mos_string_str(&node->user_type.field_annotations[i]->symbol.name);
            struct type_entry *te  = type_registry_find(self->type_registry, str);
            if (!te) continue;

            node->user_type.field_types[i] = te->type;
        }

        // convert field names from ast symbols to simple strings

        field_names = alloc_calloc(self->arena, n_fields, sizeof field_names[0]);
        for (u16 i = 0; i < n_fields; ++i)
            field_names[i] = mos_string_str(&node->user_type.field_names[i]->symbol.name);
    }

    struct tess_type *user_type = tess_type_create_user_type(
      self->arena, type_name, node->user_type.field_types, field_names, n_fields);

    if (type_registry_add(self->type_registry, (struct type_entry){.name = type_name, .type = user_type}))
        fatal("syntax_register_user_types: unexpected failure");
}

nodiscard static int syntax_register_user_types(struct syntax_checker *self) {

    // ast user type nodes do not participate in depth first search
    for (u32 i = 0; i < self->n_nodes; ++i) register_user_type(self, self->nodes[i]);
    return 0;
}

static void check_annotation(void *ctx, ast_node *node) {
    struct syntax_checker *self = ctx;

    if (ast_symbol != node->tag) return;
    if (!node->symbol.annotation) return;

    char const        *str = mos_string_str(&node->symbol.annotation->symbol.name);
    struct type_entry *te  = type_registry_find(self->type_registry, str);
    if (!te) {

#define fmt "unknown type: %s"
        char *message = null;
        int   len     = snprintf(null, 0, fmt, str) + 1;
        if (len > 0) {
            message = alloc_malloc(self->arena, (size_t)len);
            snprintf(message, (size_t)len, fmt, str);
        }
#undef fmt

        syntax_error(self, node, tess_err_expected_type, message);
    }
}

static int syntax_check_type_annotations(struct syntax_checker *self) {
    for (u32 i = 0; i < self->n_nodes; ++i) ast_pool_dfs(self, self->nodes[i], check_annotation);
    return 0;
}

static void syntax_error(struct syntax_checker *self, ast_node *node, enum tess_error_tag tag,
                         char const *message) {

    node->error = tag;

    if (!self->errors) {
        self->cap_errors = 16;
        self->errors     = alloc_calloc(self->arena, self->cap_errors, sizeof self->errors[0]);
    } else if (self->n_errors == self->cap_errors) {
        alloc_resize(self->arena, &self->errors, &self->cap_errors, self->cap_errors * 2);
    }

    self->errors[self->n_errors++] = (struct syntax_error){node, message};
}
