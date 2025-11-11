#module main
#import <stdio.tl>
#include <time.h>

// c_struct_ prefix indicates the type must be rendered in C as "struct <name>"
c_struct_timespec : {

  // must provide concrete type for any field that will be referenced, but the struct need not include all fields
  tv_sec : Int
}

main() {
  ts : c_struct_timespec := ()
  c_timespec_get(ts.&, c_TIME_UTC)
  c_printf("Current tv_sec = %i\n", ts.tv_sec)
  0
}
