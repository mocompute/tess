#module main
#import "../../test_pack_lib/lib.tl"

main() {
    result := pack_lib.lib_main()
    if result != 42 {
        return 1
    }
    0
}
