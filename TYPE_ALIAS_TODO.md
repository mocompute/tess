# Type Alias Direct Usage - Implementation TODO

## Overview

This document tracks the implementation of direct type alias usage in the Tess language.
Branch: `wip-type-alias-direct-usage`

## Current Status

### ✅ COMPLETE: Type Annotations
Type aliases work perfectly when used in type annotations:
```tl
s : Bar.Chars := Foo.Span(v = null, sz = 0)  // Works!
```

### ✅ COMPLETE: Non-Generic Direct Usage
Non-generic struct aliases can be used directly as constructors:
```tl
// Local alias
Pt = Point
p := Foo.Pt(x = 10, y = 20)  // Works!

// Chained aliases
C.Alias2 = B.Alias1 = A.Base
v := C.Alias2(x = 42)  // Works!
```

### ❌ TODO: Generic Type Alias Direct Usage
Generic aliases with type arguments fail when used directly:

**Test**: `test_type_alias_module_simple_direct.tl`
```tl
#module Foo
Span(T) : { v: Ptr(T), sz: CSize }

#module Bar
Chars = Foo.Span(CChar)

#module main
s := Bar.Chars(v = null, sz = 0)  // FAILS
```

**Error**:
```
type_error: : (tl_s_v2 (s) : Foo_Span_0(Ptr(t30), Int))
```

**Issue**: When `Bar.Chars` is resolved to `Foo_Span`, the type argument `CChar` from the alias definition is lost. The system creates a new specialization with fresh type variables instead of using the concrete types from the alias.

**Implementation Location**: `src/tess/src/infer.c:2388-2397`

Current code extracts constructor name but doesn't preserve type arguments:
```c
if (tl_monotype_is_inst(type->type)) {
    str constructor_name = type->type->cons_inst->def->generic_name;
    ast_node_name_replace(node->named_application.name, constructor_name);
    type = tl_type_registry_get(self->registry, constructor_name);
}
```

**Required Fix**: Need to extract the type arguments from the alias's instantiation and inject them into the constructor call, transforming:
```
Bar.Chars(v=null, sz=0)
```
into effectively:
```
Foo.Span(CChar)(v=null, sz=0)
```

### ❌ TODO: Multi-Argument Generic Aliases

**Test**: `test_type_alias_module_multi_arg_direct.tl`
```tl
Map(K, V) : { keys: Ptr(K), values: Ptr(V), size: Int }
StringIntMap = Collections.Map(Ptr(CChar), Int)

m := App.StringIntMap(keys = null, values = null, size = 0)  // FAILS
```

**Issue**: Same as single-argument case but with multiple type parameters that need to be preserved.

### ❌ TODO: Enum Aliases

**Test**: `test_type_alias_module_enum_direct.tl`
```tl
#module Foo
Status : {Ok, Error, Pending}

#module Bar
Result = Foo.Status

#module main
r := Bar.Result.Ok  // FAILS
```

**Error**:
```
type_error: : (tl_r_v2 (r) : t21)
let ... = (binary_op . (Bar_Result (Result) : (literal Foo_Status)) (Ok (Ok) : t29))
```

**Issue**: `Bar.Result.Ok` is parsed as a binary operation (field access), but enum aliases need special handling. The system needs to recognize that:
1. `Bar.Result` is an alias to an enum type `Foo.Status`
2. `.Ok` should access the enum value, not a struct field
3. The resolution should be `Foo.Status.Ok`

**Implementation Strategy**: May need to handle this during parsing or add special case in type inference to recognize enum aliases and redirect field access to the underlying enum's values.

## Implementation Plan

### Phase 1: Generic Type Argument Propagation

1. **Extract type arguments from alias**
   - When resolving `Bar_Chars` to `Foo_Span`, extract `(Ptr(CChar), CSize)` from the alias's polytype
   - Store these arguments for use in specialization

2. **Inject type arguments into constructor**
   - Modify the AST node to include the type arguments as explicit type parameters
   - Or pass them through the specialization context

3. **Update specialization logic**
   - Ensure `specialize_user_type` uses the provided type arguments
   - Prevent creation of fresh type variables when concrete types are available

4. **Test and validate**
   - Move `test_type_alias_module_simple_direct.tl` out of FAIL_TESTS
   - Move `test_type_alias_module_multi_arg_direct.tl` out of FAIL_TESTS

### Phase 2: Enum Alias Support

1. **Detect enum aliases during parsing or inference**
   - Identify when an alias points to an enum type
   - Mark the alias or store metadata about the target type

2. **Handle field access on enum aliases**
   - Intercept `Alias.EnumValue` patterns
   - Resolve to `UnderlyingEnum.EnumValue`

3. **Test and validate**
   - Move `test_type_alias_module_enum_direct.tl` out of FAIL_TESTS

## Testing Strategy

All tests are in `src/tess/tl/test_type_alias_*_direct.tl`

- **Passing tests**: Integrated into main test suite in `TL_TESTS`
- **Failing tests**: Listed in `TL_FAIL_TESTS` as expected failures
- As features are implemented, move tests from `TL_FAIL_TESTS` to `TL_TESTS`

## Files to Modify

- `src/tess/src/infer.c` - Main implementation for type argument propagation
- `src/tess/src/parser.c` - Possibly for enum alias field access handling
- `src/tess/src/type.c` - Type registry utilities if needed
- `Makefile` - Move tests between TL_FAIL_TESTS and TL_TESTS as they pass
- `src/tess/CMakeLists.txt` - Same for CMake build system

## Success Criteria

All 5 `*_direct.tl` tests pass:
- ✅ test_type_alias_local_direct.tl
- ✅ test_type_alias_module_chained_direct.tl
- ❌ test_type_alias_module_enum_direct.tl
- ❌ test_type_alias_module_multi_arg_direct.tl
- ❌ test_type_alias_module_simple_direct.tl

When complete, merge `wip-type-alias-direct-usage` branch into main.
