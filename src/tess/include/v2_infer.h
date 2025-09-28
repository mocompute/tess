#ifndef TESS_INFER_H_V2
#define TESS_INFER_H_V2

#include "alloc.h"

typedef struct tl_infer tl_infer;

nodiscard tl_infer     *tl_infer_create(allocator *) mallocfun;
void                    tl_infer_destroy(allocator *, tl_infer **);

#endif
