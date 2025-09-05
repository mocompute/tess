#ifndef TESS_TYPE_REGISTRY_H
#define TESS_TYPE_REGISTRY_H

#include "alloc.h"
#include "array.h"

#include "type.h"

typedef struct type_registry type_registry;

typedef struct type_entry {
    char const *name;
    tl_type    *type;
} type_entry;

typedef struct {
    array_header;
    struct type_entry *v;
} type_entry_array;

nodiscard type_registry *type_registry_create(allocator *) mallocfun;
void                     type_registry_destroy(type_registry **);

nodiscard int            type_registry_add(type_registry *, struct type_entry);
struct type_entry       *type_registry_find(type_registry *, char const *);

#endif
