#module main
// This test exercises malloc, free, and null comparison.

#include <stdlib.h>

c_malloc (count: Int)  -> Ptr[Void]
c_free[T](ptr: Ptr[T]) -> Void

main() {
    p: Ptr[Void] := c_malloc(16)
    res          := if p == null { 1 } else { 0 }
    c_free(p)
    res
}
