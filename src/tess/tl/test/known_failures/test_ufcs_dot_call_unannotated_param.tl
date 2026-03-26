// UFCS dot-call on an unannotated function parameter should work.
// `content.char_at(i)` should desugar to `String.char_at(content, i)`,
// but currently fails with tagged_union_expected_tagged_union when
// the parameter has no type annotation.

#module main
#import <String.tl>

_count(content) {
    i: CSize := 0
    ch: Some := content.char_at(i) else { return 1 }
    0
}

main() {
    _count("hello")
}
