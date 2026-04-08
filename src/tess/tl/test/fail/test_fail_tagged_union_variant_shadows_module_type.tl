#module Json
#import <Array.tl>

// Variant `String` shadows the builtin `String` module/type — must be rejected
// even though we're not in #module main.
Value: | Object
       | Array_ { value: Array[Ptr[Value]] }
       | String { value: String }
       | Int_   { value: CInt64 }
       | Null

#module main
main() { 0 }
