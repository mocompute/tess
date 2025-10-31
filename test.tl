is_null(p) {
  if p == null { true } else { false }
}

main() {
  p = c_malloc(16)
  if is_null(p) { 1 } else { 0 }
}
