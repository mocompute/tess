#module Widget
Widget: { value: Int }

validate(self: Widget) -> Result[Int, Int] {
    if self.value > 0 { Ok(self.value) }
    else { Err(-1) }
}

#module main

main() {
    w := Widget(42)
    w.validate()                        // discarded Result from UFCS method — must fail
    0
}
