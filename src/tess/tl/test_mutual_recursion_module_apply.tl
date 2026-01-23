#module util

apply(f, n) { f(n) }

is_even(n) { if (0 == n) { true  } else { apply(is_odd/1, n - 1) } }
is_odd(n)  { if (0 == n) { false } else { is_even(n - 1) } }

#module main

main() { if (util.is_even(10)) { 0 } else { 1 } }
