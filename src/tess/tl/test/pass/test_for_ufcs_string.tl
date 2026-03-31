#module main
#import <String.tl>

main() {
    s := "hello"
    count := 0
    for c in s {
        count = count + 1
    }
    if count != 5 { 1 } else { 0 }
}
