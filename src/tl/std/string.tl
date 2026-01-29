#module string
#include <string.h>

c_memcpy   (dst: Ptr(T), src: Ptr(T), n: CSize)   -> Ptr(T)
c_memmove  (dst: Ptr(T), src: Ptr(T), n: CSize)   -> Ptr(T)
c_strcpy   (dst: CString, src: CString)           -> CString
c_strncpy  (dst: CString, src: CString, n: CSize) -> CString
c_strcat   (s1: CString, s2: CString)             -> CString
c_strncat  (s1: CString, s2: CString, n: CSize)   -> CString
c_memcmp   (p1: Ptr(T), p2: Ptr(T))               -> CInt
c_strcmp   (s1: CString, s2: CString)             -> CInt
c_strncmp  (s1: CString, s2: CString, n: CSize)   -> CInt
c_strcoll  (s1: CString, s2: CString)             -> CInt
c_memchr   (p1: Ptr(T), c: CInt, n: CSize)        -> Ptr(T)
c_strchr   (s: CString, c: CInt)                  -> CString
c_strcspn  (s1: CString, s2: CString)             -> CSize
c_strpbrk  (s1: CString, s2: CString)             -> CString
c_strrchr  (s: CString, c: CInt)                  -> CString
c_strspn   (s1: CString, s2: CString)             -> CSize
c_strstr   (s1: CString, s2: CString)             -> CString
c_strtok   (s1: CString, s2: CString)             -> CString
c_memset   (s: Ptr(T), c: CInt, n: CSize)         -> Ptr(T)
c_strerror (errnum: CInt)                         -> CString
c_strlen   (s: CString)                           -> CSize
