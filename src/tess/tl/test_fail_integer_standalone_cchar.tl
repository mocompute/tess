#module main

// CChar is a standalone sub-chain (length 1). A weak signed integer literal
// should not resolve to CChar — it's not in the signed sub-chain.

take_cchar(x: CChar) -> CChar { x }

main() {
    take_cchar(65)
    0
}
