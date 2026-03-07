#module A
// intermediate

#module A.B
// intermediate

#module A.B.C
value() { 77 }

#module main
#alias X A.B.C

main() {
    if X.value() == 77 { 0 } else { 1 }
}
