#include "type_registry.h"

#include "alloc.h"
#include "dbg.h"
#include "hashmap.h"
#include "type.h"

#include <assert.h>
#include <stdlib.h>

struct type_registry {
    allocator       *alloc;
    type_entry_array entries;
    hashmap         *map;
};

static void    register_basic_types(type_registry *);

type_registry *type_registry_create(allocator *alloc) {
    type_registry *self = alloc_struct(alloc, self);
    self->alloc         = alloc;
    self->entries       = (type_entry_array){.alloc = alloc};
    self->map           = map_create(alloc, sizeof(tl_type *));

    register_basic_types(self);

    return self;
}

void type_registry_destroy(type_registry **self) {

    map_destroy(&(*self)->map);

    array_free((*self)->entries);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int type_registry_add(type_registry *self, char const *name, tl_type *type) {
    if (type_registry_find(self, name)) return 1;

    map_set(&self->map, name, (u16)strlen(name), &type);

    // sorted_insert(&self->entries, entry);
    return 0;
}

tl_type **type_registry_find(type_registry *self, char const *name) {
    return map_get(self->map, name, (u16)strlen(name));
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

    error += type_registry_add(self, type_tag_to_string(type_nil), &nil_type);
    error += type_registry_add(self, type_tag_to_string(type_bool), &bool_type);
    error += type_registry_add(self, type_tag_to_string(type_int), &int_type);
    error += type_registry_add(self, type_tag_to_string(type_float), &float_type);
    error += type_registry_add(self, type_tag_to_string(type_string), &string_type);
    error += type_registry_add(self, type_tag_to_string(type_any), &any_type);

    if (error) fatal("register_basic_types: failed to add types to registry.");
}
