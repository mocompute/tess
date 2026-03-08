# Stress Test Coverage Plan

## Compiler Phase Interaction Map

The most bug-prone code lives at these **phase boundaries**:

| Interaction Point | Phases Involved | Lines of Code | Existing Tests |
|---|---|---|---|
| **Specialization + tagged unions** | Parser desugaring → Inference → Specialization | ~6k | 13 generic TU tests |
| **Weak ints + specialization** | Inference → Specialization (double defaulting) | ~2.5k | 5-6 tests |
| **Alpha conversion + closures + generics** | Alpha → Inference → Specialization (clone+rename) | ~3.5k | ~3 tests |
| **Traits + operator rewriting + specialization** | Inference → Specialization (rewrite phase) | ~3k | 12 operator tests, but few combining with generics/traits |
| **Type aliases + specialization cache** | Registry → Specialization cache keying | ~2k | 12 alias tests, but few with generics |
| **Modules + tagged unions + name mangling** | Parser (3 constructor paths) → Mangling | ~4k | ~10 scoped variant tests |

## Highest-Value Stress Test Targets

Based on code complexity, shared data structures, and existing coverage gaps:

**Tier 1 — High complexity, low coverage of combinations:**
1. **Generic closures specialized at multiple types** — exercises alpha conversion scope preservation + closure capture + recursive specialization. Only ~3 tests touch this.
2. **Trait-bounded generics with operator overloading** — operator rewriting during specialization + trait bound verification on concrete types. Almost no combined tests.
3. **Nested generic calls with weak int literals** — weak int defaulting in `post_specialize()`. Known past bugs here, limited regression coverage.

**Tier 2 — Moderate complexity, moderate coverage:**
4. Generic tagged unions with type aliases (alias normalization + cache keying)
5. Higher-order generic functions returning tagged unions
6. Recursive generic types with closures

**Tier 3 — Well-covered but worth deeper stress:**
7. Module-scoped generic tagged unions (3 constructor paths)
8. Generic function pointers in structs

## Recommendation

Target the **Tier 1** items first — these hit the most complex compiler internals with the least existing coverage. For each, write 2-3 tests:
- A "clean" test that exercises the combination naturally
- An "adversarial" test that pushes edge cases (deep nesting, multiple specializations, shadowed names)
- A "fail" test ensuring the compiler rejects invalid uses
