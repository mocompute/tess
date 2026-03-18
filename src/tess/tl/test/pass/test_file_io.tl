#module main
#import <File.tl>


// Build a temp file path from a filename, platform-independently.
_tmp(name) { File.Path.join(File.temp_dir(), String.from_cstr(name)) }

test_write_and_read() {
    fails := 0
    path  := _tmp("tess_test_file_io.txt")

    content := s"hello, file!\nsecond line\n"
    _wr     := File.write_file(path, content)

    rd: Ok := File.read_file(path) else {
        c_printf("FAIL read_file\n")
        return 1
    }

    if !String.eq(rd.v, content) {
        c_printf("FAIL content mismatch\n")
        fails += 1
    }

    fails
}

test_read_lines() {
    fails := 0
    path  := _tmp("tess_test_file_lines.txt")

    _wr := File.write_file(path, s"alpha\nbeta\ngamma\n")

    lr: Ok := File.read_lines(path) else {
        c_printf("FAIL read_lines\n")
        return 1
    }

    if lr.v.size != 3 {
        c_printf("FAIL expected 3 lines, got %d\n", lr.v.size)
        return 1
    }

    if !String.eq(lr.v.v.[0], s"alpha") {
        c_printf("FAIL line 0\n")
        fails += 1
    }
    if !String.eq(lr.v.v.[1], s"beta") {
        c_printf("FAIL line 1\n")
        fails += 1
    }
    if !String.eq(lr.v.v.[2], s"gamma") {
        c_printf("FAIL line 2\n")
        fails += 1
    }

    fails
}

test_streaming_read_line() {
    fails := 0
    path  := _tmp("tess_test_file_readline.txt")

    _wr := File.write_file(path, s"one\ntwo\nthree")

    f: Ok := File.open(path, File.Read) else {
        c_printf("FAIL open for readline\n")
        return 1
    }
    defer File.close(f.v)

    // Line 1
    r1: Ok := File.read_line(f.v) else {
        c_printf("FAIL readline err 1\n")
        return 1
    }
    l1: Some := r1.v else {
        c_printf("FAIL readline eof 1\n")
        return 1
    }
    if !String.eq(l1.v, s"one") {
        c_printf("FAIL line 1: %s\n", String.cstr(l1.v.&))
        fails += 1
    }

    // Line 2
    r2: Ok := File.read_line(f.v) else {
        c_printf("FAIL readline err 2\n")
        return 1
    }
    l2: Some := r2.v else {
        c_printf("FAIL readline eof 2\n")
        return 1
    }
    if !String.eq(l2.v, s"two") {
        c_printf("FAIL line 2: %s\n", String.cstr(l2.v.&))
        fails += 1
    }

    // Line 3 (no trailing newline)
    r3: Ok := File.read_line(f.v) else {
        c_printf("FAIL readline err 3\n")
        return 1
    }
    l3: Some := r3.v else {
        c_printf("FAIL readline eof 3\n")
        return 1
    }
    if !String.eq(l3.v, s"three") {
        c_printf("FAIL line 3: %s\n", String.cstr(l3.v.&))
        fails += 1
    }

    // Should be EOF now
    r4: Ok := File.read_line(f.v) else {
        c_printf("FAIL readline err 4\n")
        return 1
    }
    when r4.v {
        s: Some {
            c_printf("FAIL expected EOF\n")
            fails += 1
        }
        n: None { fails += 0 }
    }

    fails
}

test_streaming_write_read() {
    fails := 0
    path  := _tmp("tess_test_file_stream.txt")

    wf: Ok := File.open(path, File.Write) else {
        c_printf("FAIL open for write\n")
        return 1
    }
    _we := File.write_str(wf.v, s"stream test")
    File.close(wf.v)

    rf: Ok := File.open(path, File.Read) else {
        c_printf("FAIL open for read\n")
        return 1
    }
    defer File.close(rf.v)

    content: Ok := File.read_all(rf.v) else {
        c_printf("FAIL read_all\n")
        return 1
    }

    if !String.eq(content.v, s"stream test") {
        c_printf("FAIL stream content: %s\n", String.cstr(content.v.&))
        fails += 1
    }

    fails
}

test_read_write_bytes() {
    fails := 0
    path  := _tmp("tess_test_file_bytes.bin")

    data := Array.empty[Byte]()
    Array.push(data.&, 0x48)            // 'H'
    Array.push(data.&, 0x69)            // 'i'
    Array.push(data.&, 0x21)            // '!'

    _we := File.write_file_bytes(path, data)

    rb: Ok := File.read_file_bytes(path) else {
        c_printf("FAIL read_file_bytes\n")
        return 1
    }

    if rb.v.size != 3 {
        c_printf("FAIL bytes size: %d\n", rb.v.size)
        return 1
    }

    b0: Byte := rb.v.v.[0]
    b1: Byte := rb.v.v.[1]
    b2: Byte := rb.v.v.[2]
    if b0 != 0x48 {
        c_printf("FAIL byte 0\n")
        fails += 1
    }
    if b1 != 0x69 {
        c_printf("FAIL byte 1\n")
        fails += 1
    }
    if b2 != 0x21 {
        c_printf("FAIL byte 2\n")
        fails += 1
    }

    fails
}

test_append() {
    fails := 0
    path  := _tmp("tess_test_file_append.txt")

    _w1 := File.write_file(path, s"first")
    _w2 := File.append_file(path, s" second")

    rd: Ok := File.read_file(path) else {
        c_printf("FAIL read after append\n")
        return 1
    }

    if !String.eq(rd.v, s"first second") {
        c_printf("FAIL append content: %s\n", String.cstr(rd.v.&))
        fails += 1
    }

    fails
}

test_exists_and_metadata() {
    fails := 0
    path  := _tmp("tess_test_file_meta.txt")
    tmp   := File.temp_dir()

    _wr := File.write_file(path, s"metadata test")

    if !File.exists(path) {
        c_printf("FAIL exists\n")
        fails += 1
    }
    if File.exists(_tmp("nonexistent_tess_file_xyz.txt")) {
        c_printf("FAIL exists nonexistent\n")
        fails += 1
    }

    if File.is_directory(path) {
        c_printf("FAIL is_directory on file\n")
        fails += 1
    }
    if !File.is_directory(tmp) {
        c_printf("FAIL is_directory on temp dir\n")
        fails += 1
    }

    sz: Ok := File.size(path) else {
        c_printf("FAIL size\n")
        return 1
    }
    if sz.v != 13 {
        c_printf("FAIL size value: %d\n", sz.v)
        fails += 1
    }

    fails
}

test_open_nonexistent() {
    fails := 0

    result := File.open(_tmp("this_does_not_exist_tess.txt"), File.Read)
    when result {
        ok: Ok {
            c_printf("FAIL expected error opening nonexistent\n")
            File.close(ok.v)
            fails += 1
        }
        err: Err {
            when err.v {
                nf: NotFound { fails += 0 }
                else {
                    c_printf("FAIL expected NotFound error\n")
                    fails += 1
                }
            }
        }
    }

    fails
}

test_seek_tell() {
    fails := 0
    path  := _tmp("tess_test_file_seek.txt")

    _wr := File.write_file(path, s"abcdefghij")

    f: Ok := File.open(path, File.Read) else {
        c_printf("FAIL open for seek\n")
        return 1
    }
    defer File.close(f.v)

    _se := File.seek(f.v, 5, File.Start)

    pos: Ok := File.tell(f.v) else {
        c_printf("FAIL tell\n")
        return 1
    }
    if pos.v != 5 {
        c_printf("FAIL tell value: %d\n", pos.v)
        fails += 1
    }

    rest: Ok := File.read_all(f.v) else {
        c_printf("FAIL read after seek\n")
        return 1
    }
    if !String.eq(rest.v, s"fghij") {
        c_printf("FAIL read after seek: %s\n", String.cstr(rest.v.&))
        fails += 1
    }

    fails
}

main() {
    fails := 0

    fails += test_write_and_read()
    fails += test_read_lines()
    fails += test_streaming_read_line()
    fails += test_streaming_write_read()
    fails += test_read_write_bytes()
    fails += test_append()
    fails += test_exists_and_metadata()
    fails += test_open_nonexistent()
    fails += test_seek_tell()

    if fails == 0 { c_printf("All file IO tests passed.\n") }
    else { c_printf("%d file IO test(s) failed.\n", fails) }

    fails
}
