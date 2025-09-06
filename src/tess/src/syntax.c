#include "syntax.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "error.h"
#include "hashmap.h"
#include "mos_string.h"
#include "type.h"
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

typedef struct {
    array_header;
    struct syntax_error *v;
} syntax_error_array;

struct syntax_checker {
    allocator         *alloc;
    allocator         *arena;
    type_registry     *type_registry;

    ast_node_slice     nodes;

    syntax_error_array errors;
};

// -- allocation and deallocation --

syntax_checker *syntax_checker_create(allocator *alloc, ast_node_slice nodes) {
    syntax_checker *self = alloc_calloc(alloc, 1, sizeof *self);

    self->alloc          = alloc;
    self->arena          = arena_create(alloc, 2048);
    self->type_registry  = type_registry_create(self->arena);

    self->errors         = (syntax_error_array){.alloc = self->arena};

    self->nodes          = nodes;

    return self;
}

void syntax_checker_destroy(syntax_checker **self) {

    arena_destroy((*self)->alloc, &(*self)->arena);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

type_registry *syntax_checker_type_registry(syntax_checker *self) {
    return self->type_registry;
}

// -- syntax_checker operation --

int syntax_checker_run(syntax_checker *self) {

    int res = 0;
    if ((res = syntax_register_user_types(self))) return res;
    if ((res = syntax_check_type_annotations(self))) return res;

    return (int)self->errors.size;
}

void syntax_checker_report_errors(syntax_checker *self) {
    if (self->errors.size == 0) return;

    for (u32 i = 0; i < self->errors.size; ++i) {

        char *str = ast_node_to_string_for_error(self->arena, self->errors.v[i].node);

        if (self->errors.v[i].message)
            fprintf(stderr, "error: %s: %s: %s\n", tess_error_tag_to_string(self->errors.v[i].node->error),
                    self->errors.v[i].message, str);

        else
            fprintf(stderr, "error: %s: %s\n", tess_error_tag_to_string(self->errors.v[i].node->error),
                    str);

        alloc_free(self->arena, str);
    }
}

// -- register_user_types --

static void register_user_type(void *ctx, ast_node *node) {
    // adds user types to the type registry
    struct syntax_checker *self = ctx;

    if (ast_user_type_definition != node->tag) return;
    if (node->user_type_def.field_types) return;
    struct ast_user_type_def *v         = ast_node_utd(node);

    char const               *type_name = ast_node_name_string(v->name);

    if (type_registry_find(self->type_registry, type_name)) {
        syntax_error(self, node, tess_err_type_exists, alloc_strdup(self->arena, type_name));
        return;
    }

    u16 const       n_fields    = v->n_fields;
    c_string_csized field_names = {0};

    if (n_fields) {

        // convert symbol annotations to actual types

        node->user_type_def.field_types = alloc_calloc(self->arena, n_fields, sizeof v->field_types[0]);

        tl_type  **field_types          = v->field_types;
        ast_node **annotations          = v->field_annotations;

        for (u32 i = 0; i < n_fields; ++i) {
            char const *str  = ast_node_name_string(annotations[i]);
            tl_type   **type = type_registry_find(self->type_registry, str);

            if (!type) fatal("register_user_types: couldn't find type '%s'", str);

            field_types[i] = *type;
        }

        field_names =
          ast_nodes_get_names(self->arena, (ast_node_slice){.v = v->field_names, .end = n_fields});
    }

    // make the user type and register it
    tl_type *lt =
      tl_type_create_labelled_tuple(self->arena, (tl_type_sized){.v = v->field_types, .size = v->n_fields},
                                    (c_string_csized)sized_all(field_names));

    tl_type *user_type = tl_type_create_user_type(self->arena, type_name, lt);

    if (type_registry_add(self->type_registry, type_name, user_type))
        fatal("syntax_register_user_types: unexpected failure");
}

nodiscard static int syntax_register_user_types(struct syntax_checker *self) {

    // note: ast user type nodes are not reachable via depth first search
    for (u32 i = self->nodes.begin; i < self->nodes.end; ++i) register_user_type(self, self->nodes.v[i]);
    return 0;
}

static void check_annotation(void *ctx, ast_node *node) {
    // checks if annotations are for a known type
    struct syntax_checker *self = ctx;

    if (ast_symbol != node->tag) return;
    if (!node->symbol.annotation) return;

    char const *str  = ast_node_name_string(node->symbol.annotation);
    tl_type   **type = type_registry_find(self->type_registry, str);
    if (!type) {

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
    for (u32 i = self->nodes.begin; i < self->nodes.end; ++i)
        ast_node_dfs(self, self->nodes.v[i], check_annotation);
    return 0;
}

static void syntax_error(struct syntax_checker *self, ast_node *node, enum tess_error_tag tag,
                         char const *message) {

    node->error = tag;
    array_push_val(self->errors, ((struct syntax_error){node, message}));
}
