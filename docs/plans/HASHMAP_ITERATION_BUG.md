# Hashmap Iteration Bug in `update_specialized_types`

## Summary

`test_struct_field_ptr_cast_multi.tl` crashed with `fatal: unreachable` at `infer.c:3942` during the `update_specialized_types` phase. The assertion expected that any polytype returned by `specialize_type_constructor` would have a `tl_cons_inst` monotype, but it had a `tl_arrow` instead.

## Root Cause

The bug was caused by **hashmap corruption during concurrent iteration and insertion**.

### The hashmap implementation

The env hashmap (`tl_type_env`) uses **Robin Hood hashing** (open addressing with probe distance tracking). On insertion, Robin Hood hashing can **relocate existing entries** to maintain its probe distance invariant. Specifically, in `set_one` (hashmap.c), when a new entry's probe distance exceeds an existing entry's, the existing entry is evicted from its cell and re-inserted further down the probe chain. This means that inserting a single new key can move other keys to different cell indices.

### The iteration pattern

`update_specialized_types` iterated the env hashmap directly:

```c
hashmap_iterator iter = {0};
while (map_iter(self->env->map, &iter)) {
    tl_polytype **poly = iter.data;   // pointer into hashmap cell storage
    update_types_one_type(self, &ctx, poly);
}
```

`iter.data` is a pointer directly into the hashmap's internal cell array. The code used this pointer both to read the current value and to write back updated values (`*poly = ...` in `update_types_one_type`).

### The corruption sequence

1. The iterator advances to cell index N, which holds an entry for some key `X` with an arrow type (e.g., `c_ldiv : ((CLong, CLong) -> c_ldiv_t(CLong, CLong))`).

2. `update_types_one_type` processes `X`. This calls `tl_infer_update_specialized_type_`, which discovers that the arrow's return type `c_ldiv_t(CLong, CLong)` needs specialization.

3. `specialize_type_constructor_` is called, which creates a new specialized type `c_ldiv_t_12` and **inserts it into the env hashmap** (`tl_type_env_insert` at line 2495).

4. The Robin Hood insertion of `c_ldiv_t_12` evicts entries along the probe chain. Through the cascade of swaps, the cell at index N now holds the newly inserted `c_ldiv_t_12` entry (with its correct `c_ldiv_t_12(CLong, CLong)` cons_inst value).

5. Back in `update_types_one_type`, the processing of `X` completes with a replacement arrow type. The code writes: `*poly = tl_polytype_absorb_mono(self->arena, replace)`. But `poly` still points to cell index N, which now holds `c_ldiv_t_12`. This overwrites `c_ldiv_t_12`'s value with `X`'s updated arrow type.

6. Later, when `specialize_type_constructor` is called again for `c_ldiv_t` and finds `c_ldiv_t_12` via the existing-instance path, `tl_type_env_lookup("c_ldiv_t_12")` returns the corrupted arrow type, triggering the assertion.

### Why this only manifested with multiple generic instantiations

The test uses `Arr(Int)` and `Arr(Float)` — two instantiations of the same generic type. The `update_specialized_types` phase processes all env entries, and with more entries in the env (from the additional instantiation and its dependencies), the hashmap's load factor increases, making Robin Hood relocations more likely during new insertions. A single instantiation didn't produce enough entries to trigger the relocation pattern.

## Fix

Instead of iterating the hashmap directly and writing through `iter.data`, snapshot the env keys before iterating. Process each key with fresh lookups and explicit re-inserts:

```c
str_array env_keys = str_map_keys(self->transient, self->env->map);
forall(ki, env_keys) {
    tl_polytype *poly = tl_type_env_lookup(self->env, env_keys.v[ki]);
    if (!poly) continue;
    tl_polytype *orig = poly;
    update_types_one_type(self, &ctx, &poly);
    if (poly != orig) tl_type_env_insert(self->env, env_keys.v[ki], poly);
}
array_free(env_keys);
```

This decouples iteration from mutation, making it safe against Robin Hood relocations and potential hashmap resizes.

## Broader lesson

Any code that iterates a Robin Hood hashmap while calling functions that may insert into the same hashmap is vulnerable to this class of bug. Unlike chaining-based hashmaps where insertion only affects the target bucket, Robin Hood hashing can relocate entries in unrelated cells. The `iter.data` pointer must not be used for write-back if the hashmap may have been mutated since the `map_iter` call that produced it.
