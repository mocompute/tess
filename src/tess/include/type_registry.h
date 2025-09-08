#ifndef TESS_TYPE_REGISTRY_H
#define TESS_TYPE_REGISTRY_H

#include "alloc.h"
#include "array.h"

#include "type.h"

typedef struct type_registry type_registry;

nodiscard type_registry     *type_registry_create(allocator *) mallocfun;
void                         type_registry_destroy(type_registry **);

nodiscard int                type_registry_add_named(type_registry *, char const *, tl_type *);
nodiscard int                type_registry_add_hashed(type_registry *, u64, tl_type *);

tl_type                    **type_registry_find_name(type_registry *, char const *);
tl_type                    **type_registry_find_hash(type_registry *self, u64);

#endif
