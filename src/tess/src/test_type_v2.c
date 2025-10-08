#include "alloc.h"
#include "dbg.h"
#include "str.h"
#include "v2_type.h"

#include <stdio.h>

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    (void)this_error;

    return error;
}
