// Adversarial test for f89aab46: HashMap with CSize keys.
// After removing the explicit CSize hash impl, CSize must dispatch to
// the UInt family's generic hash. This exercises that dispatch inside
// HashMap's generic specialization context (set, get, contains, remove).

#import <Hash.tl>
#import <HashMap.tl>

#module main

main() {
    error := 0

    m := HashMap.create[CSize, Int]()

    // Insert several CSize keys
    k0: CSize := 0
    k1: CSize := 1
    k2: CSize := 42
    k3: CSize := 9999

    m.set(k0, 100)
    m.set(k1, 200)
    m.set(k2, 300)
    m.set(k3, 400)

    if m.size() != 4 { error += 1 }

    // Lookup each key
    p := m.get(k0)
    if p == null { error += 1 }
    if p != null && p.* != 100 { error += 1 }

    p = m.get(k1)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    p = m.get(k2)
    if p == null { error += 1 }
    if p != null && p.* != 300 { error += 1 }

    p = m.get(k3)
    if p == null { error += 1 }
    if p != null && p.* != 400 { error += 1 }

    // contains
    if !m.contains(k2) { error += 1 }
    missing: CSize := 777
    if m.contains(missing) { error += 1 }

    // Overwrite
    m.set(k2, 999)
    if m.size() != 4 { error += 1 }
    p = m.get(k2)
    if p == null { error += 1 }
    if p != null && p.* != 999 { error += 1 }

    // Remove
    if !m.remove(k1) { error += 1 }
    if m.size() != 3 { error += 1 }
    if m.contains(k1) { error += 1 }

    // Remaining keys still accessible after removal
    p = m.get(k0)
    if p == null { error += 1 }
    if p != null && p.* != 100 { error += 1 }

    m.destroy()
    error
}
