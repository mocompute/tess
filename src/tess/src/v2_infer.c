#include "v2_infer.h"
#include "error.h"
#include "v2_type.h"

#include "ast.h"
#include "hashmap.h"

#include "types.h"

struct tl_infer {
    tl_type_context context;
    tl_type_env    *env;

    hashmap        *toplevels;
};

typedef struct {
    enum tl_error_tag tag;
    ast_node const   *node;
} tl_infer_error;

typedef struct {
    array_header;
    tl_infer_error *v;
} tl_infer_error_array;

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self = new (alloc, tl_infer);

    self->context  = tl_type_context_empty();
    self->env      = tl_type_env_create(alloc);

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    tl_type_env_destroy(alloc, &(*p)->env);
    alloc_free(alloc, *p);
    *p = null;
}

static hashmap *load_toplevel(allocator *alloc, ast_node_sized nodes, tl_infer_error_array *out_errors) {
    hashmap             *tops   = map_create(alloc, sizeof(ast_node *), 1024);
    tl_infer_error_array errors = {.alloc = alloc};

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        if (ast_symbol == node->tag) {
        } else if (ast_let == node->tag) {
            ast_node **p = map_get(tops, node->user_type_def.name, sizeof(ast_node *));
            if (p) {
                // merge type if the existing node is a symbol; otherwise error
                if (ast_symbol != (*p)->tag) {
                    array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                // ignore prior type annotation if the current symbol is annotated
                // FIXME continue
            }

        } else if (ast_user_type_definition == node->tag) {
            ast_node **p = map_get(tops, node->user_type_def.name, sizeof(ast_node *));
            if (p) {
                array_push(errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                continue;
            }

            *p = node;

        } else {
            array_push(errors, ((tl_infer_error){.tag = tl_err_invalid_toplevel, .node = node}));
            continue;
        }
    }

    *out_errors = errors;
    return tops;
}
