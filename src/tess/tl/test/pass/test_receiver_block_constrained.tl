// Receiver block with trait constraints on inferred type parameters.
// Uses Hash trait constraint to verify constraint propagation.

#module KeyVal

#import <Hash.tl>

KeyVal[K, V] : { key: K, val: V }

// Constrained receiver: K must implement Hash.
kv: KeyVal[K: Hash, V] : {
    key_hash() -> CSize {
        kv.key.hash()
    }
}

#module main
#import <Hash.tl>

main() {
    error := 0

    kv := KeyVal[Int, Bool](key = 42, val = true)
    h  := kv.key_hash()
    error += if h != 0 {0} else {1}

    error
}
