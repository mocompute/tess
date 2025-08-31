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

nodiscard static int syntax_rename_variables(allocator *, ast_node **, u32);
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

int syntax_checker_run(syntax_checker *self, ast_node **nodes, u32 count) {

    int res = 0;
    if ((res = syntax_register_user_types(self))) return res;
    if ((res = syntax_check_type_annotations(self))) return res;
    if ((res = syntax_rename_variables(self->arena, nodes, count))) return res;

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
        char *message;
        int   len = snprintf(null, 0, fmt, str) + 1;
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

// -- rename_variable --

struct rename_variable_ctx {
    allocator *alloc;
    allocator *strings;
    hashmap   *map;
    size_t     next;
};

static void rename_variable_ctx_init(struct rename_variable_ctx *self, allocator *alloc) {

    self->alloc   = alloc;
    self->strings = alloc_arena_create(alloc, 2048);
    self->next    = 1;

    self->map     = map_create(alloc, sizeof(string_t));
}

static void rename_variable_ctx_deinit(struct rename_variable_ctx *self) {
    map_destroy(&self->map);
    alloc_arena_destroy(self->alloc, &self->strings);
    alloc_invalidate(self);
}

static nodiscard int next_variable_name(struct rename_variable_ctx *self, string_t *out) {
    char buf[64];
    snprintf(buf, sizeof buf, "__v%zu", self->next++);
    *out = mos_string_init(self->strings, buf);
    return 0;
}

static nodiscard int rename_if_match(allocator *alloc, string_t *string, hashmap *map, string_t *copy_to) {

    string_t const *found = map_get(map, mos_string_str(string), (u16)mos_string_size(string));

    if (found) {
        mos_string_copy(alloc, copy_to, string); // preserve original name for errors
        mos_string_copy(alloc, string, found);
    }
    return 0;
}

static nodiscard int rename_variables(struct rename_variable_ctx *self, ast_node *node);

static nodiscard int rename_array_elements(struct rename_variable_ctx *self, ast_node **elements, u16 n) {

    for (size_t i = 0; i < n; ++i) {
        ast_node const *name = elements[i];
        // parameter may be a symbol or nil
        if (ast_symbol != name->tag) break; // nil can only be sole param

        string_t var_name;
        if (next_variable_name(self, &var_name)) return 1;

        map_set(&self->map, mos_string_str(&name->symbol.name), (u16)mos_string_size(&name->symbol.name),
                &var_name);

        // rename the actual parameter symbol
        if (rename_variables(self, elements[i])) return 1;
    }

    return 0;
}

static nodiscard int rename_variables(struct rename_variable_ctx *self, ast_node *node) {
    if (!node) return 1;

    switch (node->tag) {
    case ast_symbol:
        return rename_if_match(self->strings, &node->symbol.name, self->map, &node->symbol.original);

    case ast_infix:
        if (rename_variables(self, node->infix.left)) return 1;
        if (rename_variables(self, node->infix.right)) return 1;
        break;

    case ast_tuple: {
        for (size_t i = 0; i < node->array.n; ++i)
            if (rename_variables(self, node->array.nodes[i])) return 1;
    } break;

    case ast_let_in: {
        // make a new variable for this let-in subexpression and recurse,
        // but save prior value in case this is a shadowing binding.

        // first apply rename to the value portion of the expression,
        // since it is not allowed to refer to the symbol being defined.
        // But it may refer to an outer let-in binding of the same name.
        if (rename_variables(self, node->let_in.value)) return 1;

        string_t var_name;
        if (next_variable_name(self, &var_name)) return 1;

        hashmap *save = map_copy(self->map);
        assert(save);

        ast_node const *name = node->let_in.name;
        assert(ast_symbol == name->tag);

        map_set(&self->map, mos_string_str(&name->symbol.name), (u16)mos_string_size(&name->symbol.name),
                &var_name);

        if (rename_variables(self, node->let_in.name)) return 1;
        if (rename_variables(self, node->let_in.body)) return 1;

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_let: {
        // make new variables for all function parameters. save existing
        // map in case any of them shadow.

        hashmap *save = map_copy(self->map);
        assert(save);

        if (rename_array_elements(self, node->array.nodes, node->array.n)) return 1;
        if (rename_variables(self, node->let.body)) return 1;

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_if_then_else:
        if (rename_variables(self, node->if_then_else.condition)) return 1;
        if (rename_variables(self, node->if_then_else.yes)) return 1;
        if (rename_variables(self, node->if_then_else.no)) return 1;
        break;

    case ast_lambda_function: {
        // make new variable for function parameters, saving map in case of
        // shadowing.

        hashmap *save = map_copy(self->map);
        if (!save) return 1;

        if (rename_array_elements(self, node->array.nodes, node->array.n)) return 1;
        if (rename_variables(self, node->lambda_function.body)) return 1;

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_lambda_function_application:
    case ast_named_function_application:  {
        for (size_t i = 0; i < node->array.n; ++i)
            if (rename_variables(self, node->array.nodes[i])) return 1;

    } break;

    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_user_defined_type:    break;
    }

    return 0;
}

int syntax_rename_variables(allocator *alloc, ast_node **nodes, u32 count) {

    struct rename_variable_ctx ctx;
    rename_variable_ctx_init(&ctx, alloc);

    while (count--) {
        if (rename_variables(&ctx, *nodes++)) {
            rename_variable_ctx_deinit(&ctx);
            return 1;
        }
    }

    rename_variable_ctx_deinit(&ctx);
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
