Trait-Bound Verification Review

Entry point: verify_trait_bounds at src/tess/src/infer_specialize.c:1636, transitively covering
resolve_type_param_concrete, trait_name_from_bound, check_trait_bound_, check_trait_arrow,
derive_impl_type_args, and collect_quant_bindings.

High-priority issues

1. DONE: resolve_type_param_concrete fallback only matches bare-symbol annotations (line 1627)

if (ann && ast_node_is_symbol(ann) && str_eq(ann->symbol.name, tp_name)) {
    return arrow_args.v[j];
}
Any type parameter that appears only inside a compound annotation — Ptr[T], Array[T], Map[K, V], (T) -> U,
 Pair[T], etc. — will fail this match. Concrete resolution returns null, and verify_trait_bounds silently
continues (1649). The compound case is actually the common one: push[T: Clone](arr: Array[T], x: T) will
match via x: T, but pop[T: Clone](arr: Array[T]) -> T will not. This lets bounds be completely unchecked
whenever the caller passes empty resolved_type_args and the param only appears in compound positions.
collect_quant_bindings already solves this problem structurally and could be reused here.

2. DONE: Silent skip on unresolved concrete type (line 1649)

if (!concrete) continue;
Combined with issue (1), this means "can't figure out the concrete type" → "bound passes". A safer default
 is to leave enforcement to the call site only when we're sure the type parameter is call-site-supplied,
and otherwise error. Also — zero-arrow-occurrence type parameters (e.g. f[T: Numeric]() -> CInt) are
always silently accepted regardless of what the caller passes because the fallback can't find them either.

3. SKIPPED: TRAIT_BOUND_MAX_DEPTH silently returns success on overflow (line 1488)

if (depth >= TRAIT_BOUND_MAX_DEPTH) return 0;
Returning 0 (accept) on overflow is unsafe: a legitimately deep trait hierarchy, or a cyclic bound
introduced by user code, will be silently accepted rather than reported. Replacing the depth cap with a
visit set (hashmap *seen keyed on (concrete_type, trait_name)) would detect cycles explicitly and remove
the magic number, and should return 1 (with a diagnostic) on true cycles.

4. DONE: Unknown trait name treated as success (line 1491)

tl_trait_def *trait = str_map_get_ptr(self->traits, trait_name);
if (!trait) return 0; // Unknown trait — skip (may be a type, not a trait)
This is an intentional heuristic because bounds and types share syntactic space, but it means a typo like
T: Eqq compiles cleanly. At minimum this should attempt a "did-you-mean" lookup against the trait map and
warn on a plausible match. Without any such safeguard, the invariant "bounds are enforced" is conditional
on the user spelling everything correctly.

5. DONE: Non-inst concrete types silently accepted (line 1493)

if (!tl_monotype_is_inst(concrete_type)) return 0;
If resolve_type_param_concrete hits its non-substituted fallback and returns a tl_var or arrow, the bound
is skipped. specialize_arrow substitutes the arrow first (line 1667), but only if it is not already
concrete_no_weak, and the tl_var slipping through is still possible when the arrow contains a free
variable that was never resolved. This should be an error, not an acceptance.

Medium-priority issues

6. trait_name_from_bound discards bound type arguments (line 1265)

if (ast_node_is_nfa(bound)) return ast_node_str(bound->named_application.name);
A bound like T: Compare[U] reduces to just "Compare" — the type argument U is dropped. Today the parser
doesn't appear to accept this as a function-level bound (only trait inheritance uses parameterized
bounds), but the silent truncation is fragile: a future extension that enables parameterized bounds will
start passing "whatever conforms to Compare" instead of "whatever conforms to Compare[U]".

7. DONE: check_trait_arrow built-in T assumption (line 1397–1400)

// FIXME this is ridiculous: can't assume type argument naming
str type_param = S("T");
if (trait->source_node && trait->source_node->trait_def.n_type_arguments > 0)
    type_param = ast_node_str(trait->source_node->trait_def.type_arguments[0]);
The code's own FIXME flags this. Built-in traits (source_node == null) are hard-coded to substitute
against the literal name T. If any built-in sig uses a different name in its arrow spec, the substitution
becomes a no-op and the comparison silently passes because expected_arrow retains a free variable whose
hash happens to match — or worse, returns the default-failure path.

8. check_trait_arrow silent successes on parse failure (lines 1388, 1391, 1394, 1407)

Four separate early returns of 0 on "couldn't compute expected": no sig arrow, no poly lookup, actual not
an arrow, parsed expected not an arrow. Each of these indicates an internal inconsistency (the sig was
defined but can't be materialized) and deserves at least a debug assertion. As-is, any failure to
materialize the expected arrow is treated as "conformance passes," which is the unsafe default.

9. eq-from-cmp skips arrow verification entirely (line 1541–1545)

} else if (str_eq(sig->name, S("eq")) && sig->arity == 2) {
    // Skip arrow check — the compiler synthesizes the Bool wrapper.
    func_name = find_overload_func(self, concrete_type, "cmp", 2);
}

Intentional per comment, but a type with cmp of the wrong signature (cmp(T, U) -> Int instead of cmp(T, T)
 -> Int) will satisfy Eq anyway. A minimal check — that cmp is (T, T) -> Int — would cost nothing. Also:
if a direct eq exists with a wrong signature, check_trait_arrow fails and we never consult cmp, so the
fallback is asymmetric.

10. derive_impl_type_args uses first-match only (line 1296, 1371)

find_cons_inst_by_generic_name descends depth-first and returns the first inst whose generic_name matches
the target. If the impl's arrow contains multiple occurrences of the target (foo(a: Pair[T], b: Pair[U])),
 only the first occurrence drives collect_quant_bindings, and any quantifiers referenced only in later
positions remain null. The downstream conditional-conformance block silently skips them (line 1577: if
(!inner_concrete)), so bounds on those quantifiers go unchecked. Walking all matches would be more sound.

11. collect_quant_bindings only binds to tl_cons_inst (line 1337)

if (idx >= 0 && v[idx] == null && tl_monotype_is_inst(concrete_mono)) {
    v[idx] = concrete_mono;
}
When impl_mono has a quantifier slot and concrete_mono is an arrow, tuple, or primitive, the binding is
dropped. This propagates: the caller observes an unbound slot, the conditional bound is skipped, and the
enclosing verify_trait_bounds has no recourse. Given inst types are the common case this may rarely
matter, but combined with (10), the coverage gaps compound.

12. check_trait_arrow auto-address-of has no symmetric fallback (line 1431–1453)

Accepts actual Ptr[T] where expected has T, but not the reverse. A trait that expects Ptr[T] on a
self-mutating method will silently reject any impl that takes by value, even when the language's implicit
address-of would make it usable at call sites. Whether this is desired depends on the trait design, but
the asymmetry isn't documented.

Low-priority issues

13. Potential null deref in auto-address-of fallback (line 1442)

tl_monotype *target = tl_monotype_strip_const(tl_monotype_ptr_target(ap.v[j]));
tl_monotype_ptr_target on a malformed/partially-substituted Ptr inst can return null. No null check before
 strip_const. Likely never triggered in practice but worth a defensive guard.

14. verify_trait_bounds returns on first failure (line 1654)

Minor UX: a generic function with two unsatisfied bounds reports only the first. Collecting all failures
would give users a better first-pass diagnosis.

15. concrete_type->cons_inst->args.size access in nullary shim (line 1417)

The guard chain ensures cons_inst is non-null for tl_monotype_is_inst types, but the shim reads args.size
== 0 without asserting. Safe today; worth an assert for future refactors.

16. has_no_conform assumes cons_inst->def is non-null (line 1244)

str type_name = concrete_type->cons_inst->def->name;
Called only from within check_trait_bound_ after the tl_monotype_is_inst check, so currently safe. An
assert would document the precondition.

17. Cache lookup precedes bound check (line 1676 vs 1692)

specialize_arrow consults the instance cache before calling verify_trait_bounds. This is fine for
correctness (only successful specializations are cached), but if check_trait_bound_ ever becomes non-pure
(e.g. starts mutating self->subs), this ordering will become load-bearing. Add a comment noting the
assumption.

Summary

The overall architecture is sound — the split between concrete-type resolution, trait-def lookup,
capability tables, function-lookup checking, arrow-signature verification, and recursive bound propagation
 is clean. However, the verification pipeline's dominant failure mode is "silent success": at least seven
distinct code paths (1, 2, 3, 4, 5, 8, 11) return 0 on "can't determine" rather than raising a diagnostic.
 Combined, they mean a generic function can pass bound verification without any bound actually being
checked.

The most impactful fix is (1): replacing the naive symbol-match fallback in resolve_type_param_concrete
with a collect_quant_bindings-style structural walk of the toplevel's original parameter annotations
against the substituted arrow. That single change removes most of the silent-skip exposure. The next most
impactful is switching the recursion guard from a depth cap to a visit set (3), and turning the "unknown
trait" and "non-inst concrete" silent acceptances into hard errors (4, 5). After that, the FIXME on line
1397 (7) deserves a proper fix since built-in traits are the hot path.

No clearly incorrect logic was found in the happy path — the bugs are all in the "what if we can't tell"
branches, and they uniformly err on the side of letting the program compile. For a verification pass,
that's the wrong default.
