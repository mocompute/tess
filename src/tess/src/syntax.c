#include "syntax.h"

#include "alloc.h"
#include "alloc_string.h"
#include "ast.h"
#include "dbg.h"
#include "hashmap.h"
#include "mos_string.h"

#include <stdio.h>

// -- forwards --

typedef struct rename_variable_ctx rename_variable_ctx;
static void                        rename_variable_ctx_init(rename_variable_ctx *, allocator *);
nodiscard static int               syntax_rename_variables(allocator *, ast_node **, size_t);

// -- syntax_checker --

struct syntax_checker {
    allocator *alloc;
};

// -- allocation and deallocation --

syntax_checker *syntax_checker_create(allocator *alloc) {
    syntax_checker *self = alloc_calloc(alloc, 1, sizeof *self);
    if (!self) return self;

    self->alloc = alloc;
    return self;
}

void syntax_checker_destroy(syntax_checker **self) {
    alloc_free((*self)->alloc, *self);
    *self = null;
}

// -- syntax_checker operation --

int syntax_checker_run(syntax_checker *self, ast_node **nodes, u32 count) {

    int res = 0;
    if ((res = syntax_rename_variables(self->alloc, nodes, count))) return res;

    // TODO more to come...

    return res;
}

// -- rename_variable --

struct rename_variable_ctx {
    allocator *alloc;
    allocator *strings;
    hashmap   *map;
    size_t     next;
};

static void rename_variable_ctx_init(rename_variable_ctx *self, allocator *alloc) {

    self->alloc   = alloc;
    self->strings = alloc_string_arena_create(alloc, 2048);
    self->next    = 1;

    self->map     = map_create(alloc, sizeof(string_t));
}

static void rename_variable_ctx_deinit(rename_variable_ctx *self) {
    map_destroy(&self->map);
    alloc_string_arena_destroy(self->alloc, &self->strings);
    alloc_invalidate(self);
}

static nodiscard int next_variable_name(rename_variable_ctx *self, string_t *out) {
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

static nodiscard int rename_variables(rename_variable_ctx *self, ast_node *node) {
    if (!node) return 1;

    switch (node->tag) {
    case ast_symbol:
        return rename_if_match(self->strings, &node->symbol.name, self->map, &node->symbol.original);

    case ast_infix:
        if (rename_variables(self, node->infix.left)) return 1;
        if (rename_variables(self, node->infix.right)) return 1;
        break;

    case ast_tuple: {
        ast_node **it  = vec_begin(&node->tuple.elements);
        ast_node **end = vec_end(&node->tuple.elements);
        while (it != end)
            if (rename_variables(self, *it++)) return 1;
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

        ast_node **it  = (ast_node **)vec_begin(&node->let.parameters);
        ast_node **end = (ast_node **)vec_end(&node->let.parameters);

        while (it != end) {
            ast_node const *name = *it;
            // parameter may be a symbol or nil
            if (ast_symbol != name->tag) break; // nil can only be sole param

            string_t var_name;
            if (next_variable_name(self, &var_name)) return 1;
            map_set(&self->map, mos_string_str(&name->symbol.name),
                    (u16)mos_string_size(&name->symbol.name), &var_name);

            // rename the actual parameter symbol
            if (rename_variables(self, *it)) return 1;

            ++it;
        }

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

        ast_node **it  = (ast_node **)vec_begin(&node->lambda_function.parameters);
        ast_node **end = (ast_node **)vec_end(&node->lambda_function.parameters);

        while (it != end) {
            ast_node const *name = *it;
            // parameter may be a symbol or nil
            if (ast_symbol != name->tag) break; // nil can only be sole param

            string_t var_name;
            if (next_variable_name(self, &var_name)) return 1;
            map_set(&self->map, mos_string_str(&name->symbol.name),
                    (u16)mos_string_size(&name->symbol.name), &var_name);

            // rename the actual parameter symbol
            if (rename_variables(self, *it)) return 1;

            ++it;
        }

        if (rename_variables(self, node->lambda_function.body)) return 1;

        map_destroy(&self->map);
        self->map = save;

    } break;

    case ast_lambda_function_application: {
        ast_node **it  = (ast_node **)vec_begin(&node->lambda_application.arguments);
        ast_node **end = (ast_node **)vec_end(&node->lambda_application.arguments);
        while (it != end)
            if (rename_variables(self, *it++)) return 1;

    } break;

    case ast_named_function_application: {
        ast_node **it  = (ast_node **)vec_begin(&node->named_application.arguments);
        ast_node **end = (ast_node **)vec_end(&node->named_application.arguments);
        while (it != end)
            if (rename_variables(self, *it++)) return 1;
    } break;

    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_function_declaration:
    case ast_lambda_declaration:   break;
    }

    return 0;
}

int syntax_rename_variables(allocator *alloc, ast_node **nodes, size_t count) {

    rename_variable_ctx ctx;
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
