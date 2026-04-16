// Synthesized cmp/eq dispatch directly on numeric receivers.
#module main

main() {
    if (42).cmp(13) <= 0 { return 1 }
    if (13).cmp(42) >= 0 { return 2 }
    if (7).cmp(7) != 0 { return 3 }
    if !(1.5).eq(1.5) { return 4 }
    if (3.14).cmp(2.71) <= 0 { return 5 }
    s: CSize := 10zu
    t: CSize := 20zu
    if s.cmp(t) >= 0 { return 6 }
    0
}
