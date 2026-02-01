#module string
#include <string.h>

c_memcpy   (dst: Ptr(any), src: Ptr(Const(any)), n: CSize)          -> Ptr(any)
c_memmove  (dst: Ptr(any), src: Ptr(Const(any)), n: CSize)          -> Ptr(any)
c_strcpy   (dst: CString, src: Ptr(Const(CChar)))                   -> CString
c_strncpy  (dst: CString, src: Ptr(Const(CChar)), n: CSize)         -> CString
c_strcat   (s1: CString, s2: Ptr(Const(CChar)))                     -> CString
c_strncat  (s1: CString, s2: Ptr(Const(CChar)), n: CSize)           -> CString
c_memcmp   (p1: Ptr(Const(any)), p2: Ptr(Const(any)), n: CSize)     -> CInt
c_strcmp   (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)))           -> CInt
c_strncmp  (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)), n: CSize) -> CInt
c_strcoll  (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)))           -> CInt
c_memchr   (p1: Ptr(any), c: CInt, n: CSize)                        -> Ptr(any)
c_strchr   (s: Ptr(Const(CChar)), c: CInt)                          -> CString
c_strcspn  (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)))           -> CSize
c_strpbrk  (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)))           -> CString
c_strrchr  (s: Ptr(Const(CChar)), c: CInt)                          -> CString
c_strspn   (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)))           -> CSize
c_strstr   (s1: Ptr(Const(CChar)), s2: Ptr(Const(CChar)))           -> CString
c_strtok   (s1: CString, s2: Ptr(Const(CChar)))                     -> CString
c_memset   (s: Ptr(any), c: CInt, n: CSize)                         -> Ptr(any)
c_strerror (errnum: CInt)                                           -> CString
c_strlen   (s: Ptr(Const(CChar)))                                   -> CSize
