#include "type_registry.h"

#include "alloc.h"

#include <stdlib.h>

struct type_registry {
    allocator         *alloc;
    struct type_entry *entries;
    u32                n_entries;
    u32                cap_entries;
};

static int     compare_types(const void *, const void *);
static int     compare_type_entries(const void *, const void *);
static int     compare_type_entry_names(const void *, const void *);
static void    sorted_insert(allocator *, struct type_entry **ptr, u32 *n, u32 *cap, struct type_entry);

type_registry *type_registry_create(allocator *alloc) {
    type_registry *self = alloc_struct(alloc, self);
    self->alloc         = alloc;
    self->cap_entries   = 16;
    self->entries       = alloc_calloc(alloc, self->cap_entries, sizeof self->entries[0]);

    return self;
}

void type_registry_destroy(type_registry **self) {

    alloc_free((*self)->alloc, (*self)->entries);
    alloc_free((*self)->alloc, *self);
    *self = null;
}

int type_registry_add(type_registry *self, struct type_entry entry) {
    if (type_registry_find(self, entry.name)) return 1;
    sorted_insert(self->alloc, &self->entries, &self->n_entries, &self->cap_entries, entry);
    return 0;
}

struct type_entry *type_registry_find(type_registry *self, char const *name) {
    return bsearch(name, self->entries, self->n_entries, sizeof self->entries[0], compare_type_entry_names);
}

//

static void sorted_insert(allocator *alloc, struct type_entry **parray, u32 *pn, u32 *pcap,
                          struct type_entry entry) {

    struct type_entry *entries = *parray;
    u32                n       = *pn;
    u32                cap     = *pcap;

    if (n == cap) {
        alloc_resize(alloc, parray, pcap, cap * 2);
        entries = *parray;
    }

    for (u32 i = 0; i < n; ++i) {

        if (compare_type_entry_names(&entry, &entries[i]) > 0) {

            memmove(&entries[i + 1], &entries[i], (n - i) * sizeof entries[0]);

            entries[i] = entry;
            *pn        = ++n;
            return;
        }
    }

    entries[n++] = entry;
    *pn          = n;
    return;
}

static int compare_types(void const *a, void const *b) {
    struct tess_type const *left  = a;
    struct tess_type const *right = b;

    if (left->tag != right->tag) return left->tag < right->tag ? -1 : 1;

    switch (left->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: return 0;

    case type_tuple:
        if (left->n_elements != right->n_elements) return left->n_elements < right->n_elements ? -1 : 1;
        for (u16 i = 0; i < left->n_elements; i++) {
            int res;
            if ((res = compare_types(left->elements[i], right->elements[i])) != 0) return res;
        }
        return 0;

    case type_arrow: {
        int res;
        if ((res = compare_types(left->arrow.left, right->arrow.left)) != 0) return res;
        if ((res = compare_types(left->arrow.right, right->arrow.right)) != 0) return res;
        return 0;
    }

    case type_type_var:
        if (left->type_var == right->type_var) return 0;
        return left->type_var < right->type_var ? -1 : 1;
    }
}

static int compare_type_entries(void const *a, void const *b) {
    struct type_entry const *left  = a;
    struct type_entry const *right = b;

    int                      res;
    if ((res = strcmp(left->name, right->name)) != 0) return res;
    return compare_types(left->type, right->type);
}

static int compare_type_entry_names(void const *a, void const *b) {
    struct type_entry const *left  = a;
    struct type_entry const *right = b;

    return strcmp(left->name, right->name);
}
