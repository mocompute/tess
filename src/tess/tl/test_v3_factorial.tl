#module main
fact(x) { if (x <= 1) { 1 } else { x * fact(x - 1) } }

main() {
  if (fact(20) == 2432902008176640000) { 0 } else { 1 }
}
