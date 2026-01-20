#module string
#include <string.h>

c_memcpy   (dst: Ptr(T), src: Ptr(T), n: CSize) -> Ptr(T)
c_memmove  (dst: Ptr(T), src: Ptr(T), n: CSize) -> Ptr(T)
c_strcpy   (dst: Ptr(CChar), src: Ptr(CChar))           -> Ptr(CChar)
c_strncpy  (dst: Ptr(CChar), src: Ptr(CChar), n: CSize) -> Ptr(CChar)
c_strcat   (s1: Ptr(CChar), s2: Ptr(CChar))             -> Ptr(CChar)
c_strncat  (s1: Ptr(CChar), s2: Ptr(CChar), n: CSize)   -> Ptr(CChar)
c_memcmp   (p1: Ptr(T), p2: Ptr(T))             -> CInt
c_strcmp   (s1: Ptr(CChar), s2: Ptr(CChar))             -> CInt
c_strncmp  (s1: Ptr(CChar), s2: Ptr(CChar), n: CSize)   -> CInt
c_strcoll  (s1:Ptr(CChar), s2: Ptr(CChar))              -> CInt
c_memchr   (p1: Ptr(T), c: CInt, n: CSize)      -> Ptr(T)
c_strchr   (s: Ptr(CChar), c: CInt)                 -> Ptr(CChar)
c_strcspn  (s1: Ptr(CChar), s2: Ptr(CChar))             -> CSize
c_strpbrk  (s1: Ptr(CChar), s2: Ptr(CChar))             -> Ptr(CChar)
c_strrchr  (s: Ptr(CChar), c: CInt)                 -> Ptr(CChar)
c_strspn   (s1: Ptr(CChar), s2: Ptr(CChar))             -> CSize
c_strstr   (s1: Ptr(CChar), s2: Ptr(CChar))             -> Ptr(CChar)
c_strtok   (s1: Ptr(CChar), s2: Ptr(CChar))             -> Ptr(CChar)
c_memset   (s: Ptr(T), c: CInt, n: CSize)       -> Ptr(T)
c_strerror (errnum: CInt)                       -> Ptr(CChar)
c_strlen   (s: Ptr(CChar))                          -> CSize
