#module string
#include <string.h>

c_memcpy   (dst: Ptr(T), src: Ptr(T), n: CSize) -> Ptr(T)
c_memmove  (dst: Ptr(T), src: Ptr(T), n: CSize) -> Ptr(T)
c_strcpy   (dst: String, src: String)           -> String
c_strncpy  (dst: String, src: String, n: CSize) -> String
c_strcat   (s1: String, s2: String)             -> String
c_strncat  (s1: String, s2: String, n: CSize)   -> String
c_memcmp   (p1: Ptr(T), p2: Ptr(T))             -> CInt
c_strcmp   (s1: String, s2: String)             -> CInt
c_strncmp  (s1: String, s2: String, n: CSize)   -> CInt
c_strcoll  (s1:String, s2: String)              -> CInt
c_memchr   (p1: Ptr(T), c: CInt, n: CSize)      -> Ptr(T)
c_strchr   (s: String, c: CInt)                 -> String
c_strcspn  (s1: String, s2: String)             -> CSize
c_strpbrk  (s1: String, s2: String)             -> String
c_strrchr  (s: String, c: CInt)                 -> String
c_strspn   (s1: String, s2: String)             -> CSize
c_strstr   (s1: String, s2: String)             -> String
c_strtok   (s1: String, s2: String)             -> String
c_memset   (s: Ptr(T), c: CInt, n: CSize)       -> Ptr(T)
c_strerror (errnum: CInt)                       -> String
c_strlen   (s: String)                          -> CSize
