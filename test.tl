#module main
#import <string.tl>

main() {
    buffer := CArray(CChar, 256)
    ptr : CString := buffer   // CArray decays to Ptr
    c_strcpy(buffer, "hello")    // Can pass CArray where Ptr expected
    0
}
