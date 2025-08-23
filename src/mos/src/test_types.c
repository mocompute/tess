#include "types.h"

#include <assert.h>
#include <limits.h>

int main(void) {

    assert(8 == CHAR_BIT);
    assert(1 == sizeof(byte));
    assert(4 == sizeof(float));
    assert(8 == sizeof(double));

    return 0;
}
