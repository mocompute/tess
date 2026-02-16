#module A
// intermediate

#module A.B
// intermediate

#module A.B.C
value() { 77 }

#module main
#alias A.B.C X

main() {
    if X.value() == 77 { 0 } else { 1 }
}
