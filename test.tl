c_INT_MAX := 123 // FIXME this just acts as a type annotation. The value doesn't matter.

main() {
  c_printf("INT_MAX = %i", c_INT_MAX)
  c_INT_MAX - c_INT_MAX
}
