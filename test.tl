main() {
  p = c_malloc(16)
  res = if is_null(p) { 1 } else { 0 }
  c_free(p)
  res
}
