#module main
main() {
  // Note: this is hard to read but it is sensible:
  // lambda    arg    lambda   arg
  // vvvvvvvvv vvvv   vvvvvvvv vv
     (x) { x } (-1) + () { 1 } ()
}
