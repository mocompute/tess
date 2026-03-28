#module main
#import <File.tl>


// Build a temp file path from a filename, platform-independently.
_tmp(name) { File.Path.join(File.temp_dir(), String.from_cstr(name)) }

test_write_and_read() {
    fails := 0
    path  := _tmp(c"tess_test_file_io.txt")

    content := "hello, file!\nsecond line\n"
    _wr     := File.write_file(path, content)

    rd: Ok := File.read_file(path) else {
        c_printf(c"FAIL read_file\n")
        return 1
    }

    if !String.eq(rd.value, content) {
        c_printf(c"FAIL content mismatch\n")
        fails += 1
    }

    fails
}

test_read_lines() {
    fails := 0
    path  := _tmp(c"tess_test_file_lines.txt")

    _wr := File.write_file(path, "alpha\nbeta\ngamma\n")

    lr: Ok := File.read_lines(path) else {
        c_printf(c"FAIL read_lines\n")
        return 1
    }

    if lr.value.size != 3 {
        c_printf(c"FAIL expected 3 lines, got %d\n", lr.value.size)
        return 1
    }

    if !String.eq(lr.value.data.[0], "alpha") {
        c_printf(c"FAIL line 0\n")
        fails += 1
    }
    if !String.eq(lr.value.data.[1], "beta") {
        c_printf(c"FAIL line 1\n")
        fails += 1
    }
    if !String.eq(lr.value.data.[2], "gamma") {
        c_printf(c"FAIL line 2\n")
        fails += 1
    }

    fails
}

test_streaming_read_line() {
    fails := 0
    path  := _tmp(c"tess_test_file_readline.txt")

    _wr := File.write_file(path, "one\ntwo\nthree")

    f: Ok := File.open(path, File.Read) else {
        c_printf(c"FAIL open for readline\n")
        return 1
    }
    defer File.close(f.value)

    // Line 1
    r1: Ok := File.read_line(f.value) else {
        c_printf(c"FAIL readline err 1\n")
        return 1
    }
    l1: Some := r1.value else {
        c_printf(c"FAIL readline eof 1\n")
        return 1
    }
    if !String.eq(l1.value, "one") {
        c_printf(c"FAIL line 1: %s\n", String.cstr(l1.value.&))
        fails += 1
    }

    // Line 2
    r2: Ok := File.read_line(f.value) else {
        c_printf(c"FAIL readline err 2\n")
        return 1
    }
    l2: Some := r2.value else {
        c_printf(c"FAIL readline eof 2\n")
        return 1
    }
    if !String.eq(l2.value, "two") {
        c_printf(c"FAIL line 2: %s\n", String.cstr(l2.value.&))
        fails += 1
    }

    // Line 3 (no trailing newline)
    r3: Ok := File.read_line(f.value) else {
        c_printf(c"FAIL readline err 3\n")
        return 1
    }
    l3: Some := r3.value else {
        c_printf(c"FAIL readline eof 3\n")
        return 1
    }
    if !String.eq(l3.value, "three") {
        c_printf(c"FAIL line 3: %s\n", String.cstr(l3.value.&))
        fails += 1
    }

    // Should be EOF now
    r4: Ok := File.read_line(f.value) else {
        c_printf(c"FAIL readline err 4\n")
        return 1
    }
    when r4.value {
        s: Some {
            c_printf(c"FAIL expected EOF\n")
            fails += 1
        }
        n: None { fails += 0 }
    }

    fails
}

test_streaming_write_read() {
    fails := 0
    path  := _tmp(c"tess_test_file_stream.txt")

    wf: Ok := File.open(path, File.Write) else {
        c_printf(c"FAIL open for write\n")
        return 1
    }
    _we := File.write_str(wf.value, "stream test")
    File.close(wf.value)

    rf: Ok := File.open(path, File.Read) else {
        c_printf(c"FAIL open for read\n")
        return 1
    }
    defer File.close(rf.value)

    content: Ok := File.read_all(rf.value) else {
        c_printf(c"FAIL read_all\n")
        return 1
    }

    if !String.eq(content.value, "stream test") {
        c_printf(c"FAIL stream content: %s\n", String.cstr(content.value.&))
        fails += 1
    }

    fails
}

test_read_write_bytes() {
    fails := 0
    path  := _tmp(c"tess_test_file_bytes.bin")

    data := Array.empty[Byte]()
    Array.push(data.&, 0x48)            // 'H'
    Array.push(data.&, 0x69)            // 'i'
    Array.push(data.&, 0x21)            // '!'

    _we := File.write_file_bytes(path, data)

    rb: Ok := File.read_file_bytes(path) else {
        c_printf(c"FAIL read_file_bytes\n")
        return 1
    }

    if rb.value.size != 3 {
        c_printf(c"FAIL bytes size: %d\n", rb.value.size)
        return 1
    }

    b0: Byte := rb.value.data.[0]
    b1: Byte := rb.value.data.[1]
    b2: Byte := rb.value.data.[2]
    if b0 != 0x48 {
        c_printf(c"FAIL byte 0\n")
        fails += 1
    }
    if b1 != 0x69 {
        c_printf(c"FAIL byte 1\n")
        fails += 1
    }
    if b2 != 0x21 {
        c_printf(c"FAIL byte 2\n")
        fails += 1
    }

    fails
}

test_append() {
    fails := 0
    path  := _tmp(c"tess_test_file_append.txt")

    _w1 := File.write_file(path, "first")
    _w2 := File.append_file(path, " second")

    rd: Ok := File.read_file(path) else {
        c_printf(c"FAIL read after append\n")
        return 1
    }

    if !String.eq(rd.value, "first second") {
        c_printf(c"FAIL append content: %s\n", String.cstr(rd.value.&))
        fails += 1
    }

    fails
}

test_exists_and_metadata() {
    fails := 0
    path  := _tmp(c"tess_test_file_meta.txt")
    tmp   := File.temp_dir()

    _wr := File.write_file(path, "metadata test")

    if !File.exists(path) {
        c_printf(c"FAIL exists\n")
        fails += 1
    }
    if File.exists(_tmp(c"nonexistent_tess_file_xyz.txt")) {
        c_printf(c"FAIL exists nonexistent\n")
        fails += 1
    }

    if File.is_directory(path) {
        c_printf(c"FAIL is_directory on file\n")
        fails += 1
    }
    if !File.is_directory(tmp) {
        c_printf(c"FAIL is_directory on temp dir\n")
        fails += 1
    }

    sz: Ok := File.size(path) else {
        c_printf(c"FAIL size\n")
        return 1
    }
    if sz.value != 13 {
        c_printf(c"FAIL size value: %d\n", sz.value)
        fails += 1
    }

    fails
}

test_open_nonexistent() {
    fails := 0

    result := File.open(_tmp(c"this_does_not_exist_tess.txt"), File.Read)
    when result {
        ok: Ok {
            c_printf(c"FAIL expected error opening nonexistent\n")
            File.close(ok.value)
            fails += 1
        }
        err: Err {
            when err.error {
                nf: NotFound { fails += 0 }
                else {
                    c_printf(c"FAIL expected NotFound error\n")
                    fails += 1
                }
            }
        }
    }

    fails
}

test_seek_tell() {
    fails := 0
    path  := _tmp(c"tess_test_file_seek.txt")

    _wr := File.write_file(path, "abcdefghij")

    f: Ok := File.open(path, File.Read) else {
        c_printf(c"FAIL open for seek\n")
        return 1
    }
    defer File.close(f.value)

    _se := File.seek(f.value, 5, File.Start)

    pos: Ok := File.tell(f.value) else {
        c_printf(c"FAIL tell\n")
        return 1
    }
    if pos.value != 5 {
        c_printf(c"FAIL tell value: %d\n", pos.value)
        fails += 1
    }

    rest: Ok := File.read_all(f.value) else {
        c_printf(c"FAIL read after seek\n")
        return 1
    }
    if !String.eq(rest.value, "fghij") {
        c_printf(c"FAIL read after seek: %s\n", String.cstr(rest.value.&))
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

    if fails == 0 { c_printf(c"All file IO tests passed.\n") }
    else { c_printf(c"%d file IO test(s) failed.\n", fails) }

    fails
}
