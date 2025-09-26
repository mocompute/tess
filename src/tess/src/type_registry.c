#include "type_registry.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "dbg.h"
#include "hashmap.h"
#include "type.h"

#include <assert.h>
#include <stdlib.h>

struct type_registry {
    allocator *alloc;
    hashmap   *named_types;
    hashmap   *hashed_types; // key: u64 hash
};

static void    register_basic_types(type_registry *);

type_registry *type_registry_create(allocator *alloc) {
    type_registry *self = new (alloc, type_registry);
    self->alloc         = alloc;
    self->named_types   = map_create(alloc, sizeof(tl_type *));
    self->hashed_types  = map_create(alloc, sizeof(tl_type *));

    register_basic_types(self);

    return self;
}

void type_registry_destroy(type_registry **self) {

    map_destroy(&(*self)->hashed_types);
    map_destroy(&(*self)->named_types);

    alloc_free((*self)->alloc, *self);
    *self = null;
}

int type_registry_add_named(type_registry *self, char const *name, tl_type *type) {
    if (type_registry_find_name(self, name)) return 1;
    map_set(&self->named_types, name, (u16)strlen(name), &type);
    return 0;
}

int type_registry_add_hashed(type_registry *self, u64 hash, tl_type *type) {
    if (type_registry_find_hash(self, hash)) return 1;
    map_set(&self->hashed_types, &hash, sizeof hash, &type);
    return 0;
}

tl_type **type_registry_find_name(type_registry *self, char const *name) {
    return map_get(self->named_types, name, (u16)strlen(name));
}

tl_type *type_registry_must_find_name(type_registry *self, char const *name) {
    tl_type **out = map_get(self->named_types, name, (u16)strlen(name));
    if (!out) fatal("failed to find type '%s'", name);
    return *out;
}

tl_type **type_registry_find_hash(type_registry *self, u64 hash) {
    return map_get(self->hashed_types, &hash, sizeof hash);
}

//

static void register_basic_types(type_registry *self) {

    int            error         = 0;

    static tl_type nil_type      = {.tag = type_nil};
    static tl_type bool_type     = {.tag = type_bool};
    static tl_type int_type      = {.tag = type_int};
    static tl_type float_type    = {.tag = type_float};
    static tl_type string_type   = {.tag = type_string};
    static tl_type any_type      = {.tag = type_any};
    static tl_type ellipsis_type = {.tag = type_ellipsis};

    error += type_registry_add_named(self, tl_type_tag_to_string(type_nil), &nil_type);
    error += type_registry_add_named(self, tl_type_tag_to_string(type_bool), &bool_type);
    error += type_registry_add_named(self, tl_type_tag_to_string(type_int), &int_type);
    error += type_registry_add_named(self, tl_type_tag_to_string(type_float), &float_type);
    error += type_registry_add_named(self, tl_type_tag_to_string(type_string), &string_type);
    error += type_registry_add_named(self, tl_type_tag_to_string(type_any), &any_type);
    error += type_registry_add_named(self, tl_type_tag_to_string(type_ellipsis), &ellipsis_type);

    if (error) fatal("register_basic_types: failed to add types to registry.");
}

//

tl_type *type_registry_ast_node_tuple(type_registry *self, ast_node const *node) {
    struct ast_tuple const *v        = ast_node_tuple((ast_node *)node);
    tl_type_array           elements = {.alloc = self->alloc};
    array_reserve(elements, v->n_elements);

    for (u32 i = 0; i < v->n_elements; ++i) {
        assert(v->elements[i]->type);
        array_push(elements, &v->elements[i]->type);
    }

    tl_type  *out      = tl_type_create_tuple(self->alloc, (tl_type_sized)sized_all(elements));
    u64       hash     = tl_type_hash(out);

    tl_type **existing = type_registry_find_hash(self, hash);
    if (existing) {
        alloc_free(self->alloc, out);
        array_free(elements);
        return *existing;
    }

    if (type_registry_add_hashed(self, hash, out)) fatal("failed to add type");
    return out;
}

tl_type *type_registry_ast_node_labelled_tuple(type_registry *self, ast_node const *node) {
    struct ast_labelled_tuple const *v        = ast_node_lt((ast_node *)node);
    tl_type_array                    elements = {.alloc = self->alloc};
    c_string_carray                  names    = {.alloc = self->alloc};
    array_reserve(elements, v->n_assignments);
    array_reserve(names, v->n_assignments);

    for (u32 i = 0; i < v->n_assignments; ++i) {
        assert(v->assignments[i]->assignment.value->type);
        array_push(elements, &v->assignments[i]->assignment.value->type);

        char const *name = ast_node_name_string(v->assignments[i]->assignment.name);
        array_push(names, &name);
    }

    tl_type  *out      = tl_type_create_labelled_tuple(self->alloc, (tl_type_sized)sized_all(elements),
                                                       (c_string_csized)sized_all(names));
    u64       hash     = tl_type_hash(out);
    tl_type **existing = type_registry_find_hash(self, hash);
    if (existing) {
        alloc_free(self->alloc, out);
        array_free(elements);
        array_free(names);
        return *existing;
    }

    if (type_registry_add_hashed(self, hash, out)) fatal("failed to add type");
    return out;
}
