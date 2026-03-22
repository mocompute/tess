// Fully generic type alias for a multi-param type: Bar = Result

#module App
Bar = Result

#module main

main() {
    error := 0

    ok: App.Bar[Int, Int] := Ok(v = 42)
    r                     := when ok { o: Ok { o.v } e: Err { e.v } }
    error += if r == 42 { 0 } else { 1 }

    fail: App.Bar[Int, Int] := Err(v = -1)
    r2                      := when fail { o: Ok { o.v } e: Err { e.v } }
    error += if r2 == -1 { 0 } else { 1 }

    error
}
