f(g) { if (g()) { true } else { false } }

main() {
  x1 = true
  x2 = false
  f1 = () { x1 }
  f2 = () { x2 }
  ff1 = f(f1)
  ff2 = f(f2)
  res = ff1 && !ff2
  if (res == true) { 0 } else { 1 }
}
