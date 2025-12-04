#module main

main() {
  val := 1
  if true {
    // Note: if this said `val = 2` it would overwrite the outer val and test would fail.
    val := 2
  }
  val - 1
}
