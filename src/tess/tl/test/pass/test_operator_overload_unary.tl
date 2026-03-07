// Test unary ! (not) operator overloading on user-defined types.

#module Logic

Flag : { val: Bool }

not(a: Flag) -> Bool { !a.val }
eq(a: Flag, b: Flag) -> Bool { a.val == b.val }

#module main

main() {
    error := 0

    t := Logic.Flag(val = 1 == 1)
    f := Logic.Flag(val = 1 == 0)

    // ! dispatches to Logic.not
    error += if !t { 1 } else { 0 }
    error += if !f { 0 } else { 1 }

    error
}
