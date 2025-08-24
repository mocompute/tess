#include "syntax.h"

#include "alloc.h"
#include "ast.h"
#include "map.h"
#include "mos_string.h"

#include <stdio.h>

// -- forwards --

typedef struct rename_variable_ctx rename_variable_ctx;
static nodiscard int               rename_variable_ctx_init(rename_variable_ctx *, allocator *, ast_pool *);
nodiscard static int               syntax_rename_variables(allocator *, ast_pool *, ast_node_h *, size_t);

// -- syntax_checker --

struct syntax_checker {
    allocator *alloc;
    ast_pool  *pool;
};

// -- allocation and deallocation --

syntax_checker *syntax_checker_create(allocator *alloc, ast_pool *pool) {
    syntax_checker *self = alloc_calloc(alloc, 1, sizeof *self);
    if (!self) return self;

    self->alloc = alloc;
    self->pool  = pool;
    return self;
}

void syntax_checker_destroy(syntax_checker **self) {
    alloc_free((*self)->alloc, *self);
    *self = null;
}

// -- syntax_checker operation --

int syntax_checker_run(syntax_checker *self, ast_node_h *nodes, size_t count) {

    int res = 0;
    if ((res = syntax_rename_variables(self->alloc, self->pool, nodes, count))) return res;

    // TODO more to come...

    return res;
}

// -- rename_variable --

struct rename_variable_ctx {
    allocator *alloc;
    ast_pool  *pool;
    map_t     *map;
    size_t     next;
};

static nodiscard int rename_variable_ctx_init(rename_variable_ctx *self, allocator *alloc, ast_pool *pool) {

    self->alloc = alloc;
    self->pool  = pool;
    self->next  = 1;

    self->map   = map_create(alloc, sizeof(string_t), 1024, 0);
    if (!self->map) return 1;

    return 0;
}

static void rename_variable_ctx_deinit(rename_variable_ctx *self) {
    map_destroy(self->alloc, &self->map);
    alloc_invalidate(self);
}

static nodiscard int next_variable_name(rename_variable_ctx *self, string_t *out) {
    char buf[64];
    snprintf(buf, sizeof buf, "__v%zu", self->next++);
    return mos_string_init(self->alloc, out, buf);
}

static nodiscard int rename_if_match(allocator *alloc, string_t *string, map_t *map) {
    u32             hash  = mos_string_hash32(string);
    string_t const *found = map_get(map, hash);

    if (found) return mos_string_copy(alloc, string, found);
    return 0;
}

static nodiscard int rename_variables(rename_variable_ctx *self, ast_node_h handle) {
    ast_node *node = ast_pool_at(self->pool, handle);
    if (!node) return 1;

    switch (node->tag) {
    case ast_symbol: return rename_if_match(self->alloc, &node->symbol.name, self->map);

    case ast_infix:
        if (rename_variables(self, node->infix.left)) return 1;
        if (rename_variables(self, node->infix.right)) return 1;
        break;

    case ast_tuple: {
        ast_node_h       *it  = vec_begin(&node->tuple.elements);
        ast_node_h const *end = vec_end(&node->tuple.elements);
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

        ast_node const *name = ast_pool_at(self->pool, node->let_in.name);
        assert(ast_symbol == name->tag);
        u32       name_hash = mos_string_hash32(&name->symbol.name);
        string_t *save      = map_get(self->map, name_hash);
        string_t  save_data;
        if (save) mos_string_move(&save_data, save); // zero-inits the struct at *save

        string_t var_name;
        if (next_variable_name(self, &var_name)) return 1;
        if (map_set(self->alloc, &self->map, name_hash, &var_name)) return 1;

        if (rename_variables(self, node->let_in.name)) return 1;
        if (rename_variables(self, node->let_in.body)) return 1;

        if (save) {
            // could have moved by map_set
            string_t *found = map_get(self->map, name_hash);
            assert(found);
            mos_string_move(found, save);
        }

    } break;

    case ast_let: {
        // make new variables for all function parameters. save existing
        // map in case any of them shadow.

        map_t *save = map_copy(self->alloc, self->map);
        if (!save) return 1;

        ast_node_h       *it  = vec_begin(&node->let.parameters);
        ast_node_h const *end = vec_end(&node->let.parameters);

        while (it != end) {
            ast_node const *name = ast_pool_at(self->pool, *it);
            // parameter may be a symbol or nil
            if (ast_symbol != name->tag) break; // nil can only be sole param
            u32      name_hash = mos_string_hash32(&name->symbol.name);

            string_t var_name;
            if (next_variable_name(self, &var_name)) return 1;
            if (map_set(self->alloc, &self->map, name_hash, &var_name)) return 1;

            // rename the actual parameter symbol
            if (rename_variables(self, *it)) return 1;

            ++it;
        }

        if (rename_variables(self, node->let.body)) return 1;

        map_destroy(self->alloc, &self->map);
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

        map_t *save = map_copy(self->alloc, self->map);
        if (!save) return 1;

        ast_node_h       *it  = vec_begin(&node->lambda_function.parameters);
        ast_node_h const *end = vec_end(&node->lambda_function.parameters);

        while (it != end) {
            ast_node const *name = ast_pool_at(self->pool, *it);
            // parameter may be a symbol or nil
            if (ast_symbol != name->tag) break; // nil can only be sole param
            u32      name_hash = mos_string_hash32(&name->symbol.name);

            string_t var_name;
            if (next_variable_name(self, &var_name)) return 1;
            if (map_set(self->alloc, &self->map, name_hash, &var_name)) return 1;

            // rename the actual parameter symbol
            if (rename_variables(self, *it)) return 1;

            ++it;
        }

        if (rename_variables(self, node->lambda_function.body)) return 1;

        map_destroy(self->alloc, &self->map);
        self->map = save;

    } break;

    case ast_lambda_function_application: {
        ast_node_h       *it  = vec_begin(&node->lambda_application.arguments);
        ast_node_h const *end = vec_end(&node->lambda_application.arguments);
        while (it != end)
            if (rename_variables(self, *it++)) return 1;

    } break;

    case ast_named_function_application: {
        ast_node_h       *it  = vec_begin(&node->named_application.arguments);
        ast_node_h const *end = vec_end(&node->named_application.arguments);
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

int syntax_rename_variables(allocator *alloc, ast_pool *pool, ast_node_h *nodes, size_t count) {

    rename_variable_ctx ctx;
    if (rename_variable_ctx_init(&ctx, alloc, pool)) return 1;

    map_t *map = map_create(alloc, sizeof(string_t), 1024, 0);
    if (!map) return 1;

    ast_node_h *handle = nodes;

    while (count--) {
        if (rename_variables(&ctx, *handle++)) goto cleanup;
    }

cleanup:
    rename_variable_ctx_deinit(&ctx);

    return 0;
}
