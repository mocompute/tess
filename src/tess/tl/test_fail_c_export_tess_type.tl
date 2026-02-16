#module main

// c_export with a Tess struct type should fail: user structs are not C-exportable
MyData: { x: CInt, y: CInt }

[[c_export]] bad(d: MyData) -> CInt { d.x }

main() { bad(MyData(1, 2)) }
