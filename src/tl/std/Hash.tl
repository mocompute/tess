#module Hash

// Trait definition (replaces compiler builtin)
Hash[T]: {
    hash(x: T) -> CSize
}

// -- Synopsis --

fnv1a        (p: Ptr[any], n: CSize) -> CSize
fnv1a_cstring(s: CString)            -> CSize

// -- Implementation --


// C functions defined in <builtin.tl>
c_tl_hash_bytes  (p: Ptr[any], n: CSize) -> CSize
c_tl_hash_cstring(s: CString)            -> CSize


fnv1a        (p: Ptr[any], n: CSize) -> CSize { c_tl_hash_bytes(p, n)           }
fnv1a_cstring(s: CString)            -> CSize            { c_tl_hash_cstring(s) }

// Hash implementations for builtin type families

#module Int
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }

#module UInt
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }

#module Float
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }

#module Bool
hash[T](n: T) -> CSize { Hash.fnv1a(n.&, sizeof(n)) }
