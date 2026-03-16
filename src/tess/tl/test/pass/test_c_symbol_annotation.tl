#module main
#include <limits.h>
#import <cstdio.tl>

c_INT_MAX: Int

main() {
    c_printf("INT_MAX = %i", c_INT_MAX)
    result: CInt := c_INT_MAX - c_INT_MAX
    result
}
