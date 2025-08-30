#include "type_registry.h"

#include "alloc.h"
#include "dbg.h"
#include "tess_type.h"

#include <assert.h>
#include <stdlib.h>

struct type_registry {
    allocator         *alloc;
    struct type_entry *entries;
    u32                n_entries;
    u32                cap_entries;
};

// static int     compare_types(const void *, const void *);
// static int     compare_type_entries(const void *, const void *);
static int     compare_type_entry_names(const void *, const void *);
static void    sorted_insert(allocator *, struct type_entry **ptr, u32 *n, u32 *cap, struct type_entry);
static void    register_basic_types(type_registry *);

type_registry *type_registry_create(allocator *alloc) {
    type_registry *self = alloc_struct(alloc, self);
    self->alloc         = alloc;
    self->cap_entries   = 16;
    self->entries       = alloc_calloc(alloc, self->cap_entries, sizeof self->entries[0]);
    self->n_entries     = 0;

    register_basic_types(self);

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
    dbg("type_registry_add: added %s\n", entry.name);
    return 0;
}

struct type_entry *type_registry_find(type_registry *self, char const *name) {
    struct type_entry  tmp = {.name = name};

    struct type_entry *out =
      bsearch(&tmp, self->entries, self->n_entries, sizeof self->entries[0], compare_type_entry_names);

    return out;
}

//

static void register_basic_types(type_registry *self) {

    int                     error       = 0;

    static struct tess_type nil_type    = {.tag = type_nil};
    static struct tess_type bool_type   = {.tag = type_bool};
    static struct tess_type int_type    = {.tag = type_int};
    static struct tess_type float_type  = {.tag = type_float};
    static struct tess_type string_type = {.tag = type_string};
    struct type_entry       entry       = {0};

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

    if (error) fatal("register_basic_types: failed to add types to registry.");
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

        if (compare_type_entry_names(&entry, &entries[i]) <= 0) {

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

// static int compare_types(void const *a, void const *b) {
//     struct tess_type const *left  = a;
//     struct tess_type const *right = b;

//     return tess_type_compare(left, right);
// }

// static int compare_type_entries(void const *a, void const *b) {
//     struct type_entry const *left  = a;
//     struct type_entry const *right = b;

//     int                      res;
//     if ((res = strcmp(left->name, right->name)) != 0) return res;
//     return compare_types(left->type, right->type);
// }

static int compare_type_entry_names(void const *a, void const *b) {
    struct type_entry const *left  = a;
    struct type_entry const *right = b;

    return strcmp(left->name, right->name);
}
