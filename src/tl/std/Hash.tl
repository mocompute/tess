#module Hash


// -- Synopsis --

fnv1a        (p: Ptr[any], n: CSize) -> CSize
fnv1a_cstring(s: CString)            -> CSize

// -- Implementation --


// C functions defined in <builtin.tl>
c_tl_hash_bytes  (p: Ptr[any], n: CSize) -> CSize
c_tl_hash_cstring(s: CString)            -> CSize


fnv1a        (p: Ptr[any], n: CSize) -> CSize { c_tl_hash_bytes(p, n)           }
fnv1a_cstring(s: CString)            -> CSize            { c_tl_hash_cstring(s) }
