#include "type_registry.h"

#include "alloc.h"
#include "dbg.h"
#include "type.h"

#include <assert.h>
#include <stdlib.h>

struct type_registry {
    allocator       *alloc;
    type_entry_array entries;
};

// static int     compare_types(const void *, const void *);
// static int     compare_type_entries(const void *, const void *);

static int     compare_type_entry_names(const void *, const void *);
static void    sorted_insert(type_entry_array *, type_entry);
static void    register_basic_types(type_registry *);

type_registry *type_registry_create(allocator *alloc) {
    type_registry *self = alloc_struct(alloc, self);
    self->alloc         = alloc;
    self->entries       = (type_entry_array){.alloc = alloc};

    register_basic_types(self);

    return self;
}

void type_registry_destroy(type_registry **self) {

    array_free((*self)->entries);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int type_registry_add(type_registry *self, type_entry entry) {
    if (type_registry_find(self, entry.name)) return 1;
    sorted_insert(&self->entries, entry);
    return 0;
}

type_entry *type_registry_find(type_registry *self, char const *name) {
    type_entry  tmp = {.name = name};

    type_entry *out = bsearch(&tmp, self->entries.v, self->entries.size, sizeof self->entries.v[0],
                              compare_type_entry_names);

    return out;
}

//

static void register_basic_types(type_registry *self) {

    int            error       = 0;

    static tl_type nil_type    = {.tag = type_nil};
    static tl_type bool_type   = {.tag = type_bool};
    static tl_type int_type    = {.tag = type_int};
    static tl_type float_type  = {.tag = type_float};
    static tl_type string_type = {.tag = type_string};
    static tl_type any_type    = {.tag = type_any};
    type_entry     entry       = {0};

    //
    entry.name = type_tag_to_string(type_nil);
    entry.type = &nil_type;
    error += type_registry_add(self, entry);

    entry.name = type_tag_to_string(type_bool);
    entry.type = &bool_type;
    error += type_registry_add(self, entry);

    entry.name = type_tag_to_string(type_int);
    entry.type = &int_type;
    error += type_registry_add(self, entry);

    entry.name = type_tag_to_string(type_float);
    entry.type = &float_type;
    error += type_registry_add(self, entry);

    entry.name = type_tag_to_string(type_string);
    entry.type = &string_type;
    error += type_registry_add(self, entry);

    entry.name = type_tag_to_string(type_any);
    entry.type = &any_type;
    error += type_registry_add(self, entry);

    if (error) fatal("register_basic_types: failed to add types to registry.");
}

//

static void sorted_insert(type_entry_array *entries, type_entry entry) {

    if (entries->size == entries->capacity) array_reserve(*entries, entries->capacity * 2);

    for (u32 i = 0; i < entries->size; ++i) {

        if (compare_type_entry_names(&entry, &entries->v[i]) <= 0) {

            array_insert(*entries, i, &entry, 1);

            return;
        }
    }

    array_push(*entries, &entry);
    return;
}

// static int compare_types(void const *a, void const *b) {
//     tl_type const *left  = a;
//     tl_type const *right = b;

//     return tl_type_compare(left, right);
// }

// static int compare_type_entries(void const *a, void const *b) {
//     type_entry const *left  = a;
//     type_entry const *right = b;

//     int                      res;
//     if ((res = strcmp(left->name, right->name)) != 0) return res;
//     return compare_types(left->type, right->type);
// }

static int compare_type_entry_names(void const *a, void const *b) {
    type_entry const *left  = a;
    type_entry const *right = b;

    return strcmp(left->name, right->name);
}
