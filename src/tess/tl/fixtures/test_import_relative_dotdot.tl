#module main
// Test ../ relative import
#import "../fixtures/helper.tl"

main() {
    if helper.helper_value() == 42 { 0 } else { 1 }
}
