#include "types.h"

#include <assert.h>
#include <limits.h>

int main(void) {
    assert(8 == CHAR_BIT);
    assert(1 == sizeof(byte));
}
