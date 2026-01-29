#module main

good() { true && true  }
bad () { true && false }

main() {
    error := 0

    error += if good() {0} else {1}
    error += if bad() {1} else {0}

    error
}
