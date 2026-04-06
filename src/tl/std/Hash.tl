#module Hash

// Trait definition (replaces compiler builtin)
Hash[T]: {
    hash(x: T) -> CSize
}

// -- Synopsis --

fnv1a        (p: Ptr[Const[any]], n: CSize) -> CSize
fnv1a_cstring(s: CString)                   -> CSize

// -- Implementation --


#ifc
// Hash functions (fnv1a / fnv1a_cstring wrappers).

// Murmurhash3 fmix64 finalizer — mixes all bits so low bits (used for
// bucket selection) reflect the full key, preventing clustering.
static inline size_t tl_fmix64(size_t h) {
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}
static inline size_t tl_hash_bytes(const void *p, size_t n) {
    size_t h = 0xcbf29ce484222325ULL;
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return tl_fmix64(h);
}
static inline size_t tl_hash_cstring(const char *s) {
    size_t h = 0xcbf29ce484222325ULL;
    if (s) for (; *s; s++) { h ^= (unsigned char)*s; h *= 0x100000001b3ULL; }
    return tl_fmix64(h);
}
#endc

c_tl_hash_bytes  (p: Ptr[Const[any]], n: CSize) -> CSize
c_tl_hash_cstring(s: CString)                   -> CSize


fnv1a        (p: Ptr[Const[any]], n: CSize) -> CSize { c_tl_hash_bytes(p, n) }
fnv1a_cstring(s: CString)                   -> CSize { c_tl_hash_cstring(s)  }

// Hash implementations for builtin type families

#module Int
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }

#module UInt
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }

#module Float
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }

#module Bool
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }
