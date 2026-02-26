#module main
#import <Str.tl>

[[symbol_attribute]] sym := 123

[[function_attribute]] foo() { 456 }

[[attr1, attr2]] multiple() { 789 }

[[NFA(42)]] nfa_attr() { 0 }

[[struct_attr]] MyStruct: { x: Int }

[[enum_attr]] MyEnum: { A, B }

[[forward_attr]] forward_decl: () -> Int
forward_decl                   () { 42}

forward_decl2:                () -> Int
[[forward_attr]] forward_decl2() { 42 }

has_param_attr([[interesting]] x) { if x :: [[interesting]] {x} else {0} }

main() {
    error := 0

    error += if sym ::        [[symbol_attribute]] {0} else {1}
    error += if foo/0 ::      [[function_attribute]] {0} else {1}
    error += if multiple/0 :: [[attr1]] {0} else {1}
    error += if multiple/0 :: [[attr2]] {0} else {1}

    [[local_attr]] local := 123
    error += if local :: [[local_attr]] {0} else {1}
    error += if local :: [[bad]] {1} else {0}

    // exact match
    error += if nfa_attr/0 :: [[NFA(42)]] {0} else {1}

    // incorrect exact match
    error += if nfa_attr/0 :: [[NFA(99)]] {1} else {0}

    // general match of nfa, regardless of arguments. Note lack of arity mangling.
    error += if nfa_attr/0 :: [[NFA]] {0} else {1}

    // struct type attribute
    error += if MyStruct :: [[struct_attr]] {0} else {1}

    // enum type attribute
    error += if MyEnum :: [[enum_attr]] {0} else {1}

    // negative match on struct and enum
    error += if MyStruct :: [[bad]] {1} else {0}
    error += if MyEnum ::   [[bad]] {1} else {0}

    // forward decl: accept attributes on either (but not both) occurrences
    error += if forward_decl/0 ::  [[forward_attr]] {0} else {1}
    error += if forward_decl2/0 :: [[forward_attr]] {0} else {1}

    // lambda parameter
    l1 := ([[param_attr]] x) { x :: [[param_attr]] }
    error += if l1("hello") {0} else {1}

    error += if 123 == has_param_attr(123) {0} else {1}

    result : CInt := error
}
