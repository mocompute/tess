// -- Print functions --
#module Print
#import <ToString.tl>
#import <String.tl>
#import <Slice.tl>
#import <cstdio.tl>

_fprint_all(stream: Ptr[c_FILE], args: Slice[String]) -> Void
print      (args: ...ToString)                        -> Void
println    (args: ...ToString)                        -> Void
fprint     (stream: Ptr[c_FILE], args: ...ToString)   -> Void
fprintln   (stream: Ptr[c_FILE], args: ...ToString)   -> Void

_fprint_all(stream, args) {
    for s in Slice args {
        c_fprintf(stream, c"%s", s.&.cstr())
    }
    void
}

print(args) { _fprint_all(c_stdout, args) }

println(args) {
    _fprint_all(c_stdout, args)
    c_fprintf(c_stdout, c"\n")
    void
}

fprint(stream, args) { _fprint_all(stream, args) }

fprintln(stream, args) {
    _fprint_all(stream, args)
    c_fprintf(stream, c"\n")
    void
}
