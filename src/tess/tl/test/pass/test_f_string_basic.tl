#module main
#import <Print.tl>

main() {
    name := "world"
    age  := 42
    error := 0

    s1 := f"hello {name}"
    if s1 != "hello world" { error += 1 }

    s2 := f"hello {name}, age {age}"
    if s2 != "hello world, age 42" { error += 1 }

    s3 := f"{name}"
    if s3 != "world" { error += 1 }

    s4 := f"start {name} middle {age} end"
    if s4 != "start world middle 42 end" { error += 1 }

    error
}
