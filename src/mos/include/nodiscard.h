#ifndef MOS_NODISCARD_H
#define MOS_NODISCARD_H

#include "platform.h"

#ifndef MOS_WINDOWS
#ifndef nodiscard
#define nodiscard __attribute__((warn_unused_result))
#endif

#define constfun  __attribute__((const))
#define mallocfun __attribute__((malloc))
#define purefun   __attribute__((pure))

#else

#ifndef nodiscard
#define nodiscard
#endif

#define constfun
#define mallocfun
#define purefun

#endif /* MOS_WINDOWS */

#endif
