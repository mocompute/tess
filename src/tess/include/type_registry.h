#ifndef TESS_TYPE_REGISTRY_H
#define TESS_TYPE_REGISTRY_H

#include "alloc.h"
#include "array.h"

#include "type.h"

typedef struct type_registry type_registry;

typedef struct {
    array_header;
    struct type_entry *v;
} type_entry_array;

nodiscard type_registry *type_registry_create(allocator *) mallocfun;
void                     type_registry_destroy(type_registry **);

nodiscard int            type_registry_add(type_registry *, char const *, tl_type *);
tl_type                **type_registry_find(type_registry *, char const *);

#endif
