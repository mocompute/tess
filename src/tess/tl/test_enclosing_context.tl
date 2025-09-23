let expect_true  f = if f () then true else false
let expect_false f = if f () then false else true
let to_int val     = if val  then 1 else 0

struct Ctx = sentinel : int end

let main () =
  let ctx = Ctx 0 in
  let x1  = 1 in
  let x2  = 2 in
  let res =
  to_int
  (_tl_and_
  (expect_true  (fun () -> _tl_or_ 0 0 x1 (ctx.sentinel := 1) end))
  (expect_true  (fun () -> eq ctx.sentinel 0 end))
  (expect_false (fun () -> _tl_or_ 0 0 0 0 end))
  (expect_true  (fun () -> _tl_and_ 1 2 3 0 (ctx.sentinel := 1) end))
  (expect_true  (fun () -> eq ctx.sentinel 23 end))) in
  let _ = std_dbg "result = %i" res in
  res
