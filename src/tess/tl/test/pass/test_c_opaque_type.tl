#module main
#import <cstdio.tl>

main() {
    stream := c_tmpfile()
    _      := c_fclose(stream)
    0
}
