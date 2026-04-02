#include <math.h>
#link m

#module main

// Forward declaration for the C function
c_sqrt(x: CDouble) -> CDouble

main() {
    result := c_sqrt(4.0)
    if result > 1.99 && result < 2.01 { 0 } else { 1 }
}
