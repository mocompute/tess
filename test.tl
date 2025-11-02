#include <time.h>

c_struct_timespec : {
  tv_sec : Int
}

main() {
  ts : c_struct_timespec := ()
  c_timespec_get(ts.&, TIME_UTC)
  c_printf("Current tv_sec = %i\n", ts.tv_sec)
  0
}
