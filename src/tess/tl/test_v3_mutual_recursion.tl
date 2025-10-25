is_even(n) { if (0 == n) { true  } else { is_odd (n - 1) } }
is_odd(n)  { if (0 == n) { false } else { is_even(n - 1) } }

main() { if (is_even(10)) { 0 } else { 1 } }
