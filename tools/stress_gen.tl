// stress_gen.tl -- Generate compiler stress tests
//
// Usage:
//   tess run tools/stress_gen.tl -- --axis <axis> --scale <N> --out <dir>
//
// Axes:
//   volume      -- N independent functions with batched summation
//   specialize  -- N struct types x generic functions (identity, pair, wrap)
//   nesting     -- depth-N nested ifs + depth-N call chain
//   inference   -- depth-N type inference chain
//   all         -- generate all axes
//
// Examples:
//   mkdir -p /tmp/stress
//   tess run tools/stress_gen.tl -- -a all -s 100 -o /tmp/stress
//   tess run /tmp/stress/stress_volume.tl
//
// Each generated file is a self-checking program that returns 0 on success.
// Increase --scale to find compiler limits (memory, recursion depth, etc.).
// Known breaking points: nesting depth ~100 causes OOM.

#module main
#import <CommandLine.tl>
#import <File.tl>
#import <Print.tl>
#import <String.tl>
#import <cstdlib.tl>
#import <cstdio.tl>

println  = Print.println
fprintln = Print.fprintln

// -- Helpers --

write(f, s) {
    File.write_str(f, s)
    void
}

writeln(f, s) {
    write(f, s)
    write(f, "\n")
}

open_output(dir, name) {
    path := File.Path.join(dir, name)
    result: Ok := File.open(path, File.Write) else {
        fprintln(c_stderr, f"cannot open {path}")
        c_exit(1)
    }
    result.value
}

// -- Axis 1: Volume --

gen_volume(dir, scale) {
    f := open_output(dir, "stress_volume.tl")
    writeln(f, "#module main")
    writeln(f, "")

    // Generate N independent functions
    i := 0
    while i < scale {
        writeln(f, f"fun_{i}() {{ {i * 3 + 1} }}")
        i = i + 1
    }
    writeln(f, "")

    // Generate batch functions (100 per batch) to avoid huge expressions
    batch_size := 100
    num_batches := (scale + batch_size - 1) / batch_size
    b := 0
    while b < num_batches {
        start := b * batch_size
        end := start + batch_size
        if end > scale { end = scale }

        write(f, f"batch_{b}() {{ ")
        j := start
        while j < end {
            if j > start { write(f, " + ") }
            write(f, f"fun_{j}()")
            j = j + 1
        }
        writeln(f, " }")
        b = b + 1
    }
    writeln(f, "")

    // Generate main
    writeln(f, "main() {")
    write(f, "    total := ")
    b = 0
    while b < num_batches {
        if b > 0 { write(f, " + ") }
        write(f, f"batch_{b}()")
        b = b + 1
    }
    writeln(f, "")

    // Expected: sum of (i*3 + 1) for i in 0..N-1 = 3*N*(N-1)/2 + N
    expected := 3 * scale * (scale - 1) / 2 + scale
    writeln(f, f"    if total != {expected} {{ return 1 }}")
    writeln(f, "    0")
    writeln(f, "}")

    File.close(f)
    println(f"wrote stress_volume.tl (scale={scale}, {scale} functions, {num_batches} batches)")
}

// -- Axis 2: Specialization Explosion --

gen_specialize(dir, scale) {
    f := open_output(dir, "stress_specialize.tl")
    writeln(f, "#module main")
    writeln(f, "")

    // Generate N struct types
    i := 0
    while i < scale {
        writeln(f, f"S{i}: {{ val: Int }}")
        i = i + 1
    }
    writeln(f, "")

    // Generic functions
    writeln(f, "identity[T](x: T) -> T { x }")
    writeln(f, "pair[A, B](a: A, b: B) -> A { a }")
    writeln(f, "wrap[T](x: T) -> T { identity[T](x) }")
    writeln(f, "")

    writeln(f, "main() {")

    // N identity specializations
    i = 0
    while i < scale {
        writeln(f, f"    r{i} := identity(S{i}(val = {i}))")
        i = i + 1
    }
    writeln(f, "")

    // N*K pair specializations (K = min(10, N))
    k := scale
    if k > 10 { k = 10 }
    i = 0
    while i < scale {
        j := 0
        while j < k {
            if j != i {
                writeln(f, f"    p{i}_{j} := pair(S{i}(val = {i}), S{j}(val = {j}))")
            }
            j = j + 1
        }
        i = i + 1
    }
    writeln(f, "")

    // N wrap specializations
    i = 0
    while i < scale {
        writeln(f, f"    w{i} := wrap(S{i}(val = {i}))")
        i = i + 1
    }
    writeln(f, "")

    writeln(f, "    0")
    writeln(f, "}")

    File.close(f)
    println(f"wrote stress_specialize.tl (scale={scale}, {scale} types)")
}

// -- Axis 3: Deep Nesting --

gen_nesting(dir, scale) {
    f := open_output(dir, "stress_nesting.tl")
    writeln(f, "#module main")
    writeln(f, "")

    // Part A: nested ifs
    write(f, "deep_nest() { ")
    i := 0
    while i < scale {
        write(f, "if true { ")
        i = i + 1
    }
    write(f, "1")
    i = 0
    while i < scale {
        write(f, " } else { 0 }")
        i = i + 1
    }
    writeln(f, " }")
    writeln(f, "")

    // Part B: call chain
    writeln(f, "f_0() { 1 }")
    i = 1
    while i < scale {
        writeln(f, f"f_{i}() {{ f_{i - 1}() + 1 }}")
        i = i + 1
    }
    writeln(f, "")

    // Main
    last := scale - 1
    if last < 0 { last = 0 }
    writeln(f, "main() {")
    writeln(f, "    a := deep_nest()")
    writeln(f, f"    b := f_{last}()")
    writeln(f, "    if a != 1 { return 1 }")
    writeln(f, f"    if b != {scale} {{ return 2 }}")
    writeln(f, "    0")
    writeln(f, "}")

    File.close(f)
    println(f"wrote stress_nesting.tl (scale={scale}, depth={scale})")
}

// -- Axis 4: Inference Chain --

gen_inference(dir, scale) {
    f := open_output(dir, "stress_inference.tl")
    writeln(f, "#module main")
    writeln(f, "")

    writeln(f, "chain_0(x) { x + 1 }")
    i := 1
    while i < scale {
        writeln(f, f"chain_{i}(x) {{ chain_{i - 1}(x) + 1 }}")
        i = i + 1
    }
    writeln(f, "")

    last := scale - 1
    if last < 0 { last = 0 }
    expected := scale
    writeln(f, "main() {")
    writeln(f, f"    result := chain_{last}(0)")
    writeln(f, f"    if result != {expected} {{ return 1 }}")
    writeln(f, "    0")
    writeln(f, "}")

    File.close(f)
    println(f"wrote stress_inference.tl (scale={scale}, chain depth={scale})")
}

// -- Main --

main(argc, argv) {
    cl := CommandLine.create("stress_gen", "Generate compiler stress tests")
    cl.required("axis",  'a', "Test axis: volume|specialize|nesting|inference|all")
    cl.required("scale", 's', "Scale factor (integer)")
    cl.required("out",   'o', "Output directory")
    args := cl.parse_or_exit(argc, argv)

    axis_opt: Some := args.get_option("axis") else {
        fprintln(c_stderr, "error: --axis is required")
        return 1
    }
    axis := axis_opt.value

    scale_opt: Some := args.get_option("scale") else {
        fprintln(c_stderr, "error: --scale is required")
        return 1
    }
    scale := c_atoi(scale_opt.value.cstr())
    if scale <= 0 {
        fprintln(c_stderr, "error: --scale must be a positive integer")
        return 1
    }

    out_opt: Some := args.get_option("out") else {
        fprintln(c_stderr, "error: --out is required")
        return 1
    }
    dir := out_opt.value

    valid := axis == "volume" || axis == "specialize" || axis == "nesting" || axis == "inference" || axis == "all"
    if !valid {
        fprintln(c_stderr, f"error: unknown axis '{axis}'. Use: volume|specialize|nesting|inference|all")
        return 1
    }

    if axis == "volume" || axis == "all" {
        gen_volume(dir, scale)
    }
    if axis == "specialize" || axis == "all" {
        gen_specialize(dir, scale)
    }
    if axis == "nesting" || axis == "all" {
        gen_nesting(dir, scale)
    }
    if axis == "inference" || axis == "all" {
        gen_inference(dir, scale)
    }

    cl.destroy()
    0
}
