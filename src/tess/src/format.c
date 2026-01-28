#include "format.h"

str tl_format(allocator *alloc, char const *data, u32 size, char const *filename) {
    (void)filename;
    return str_init_n(alloc, data, size);
}
