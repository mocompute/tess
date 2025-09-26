#include "alloc.h"
#include "hash.h"

#include "hashmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <time.h>

#define ERR(FMT, ...) fprintf(stderr, FMT "\n", ##__VA_ARGS__)
#define OUT(FMT, ...) printf(FMT "\n", ##__VA_ARGS__)

// static noreturn void fatal(char const *msg) {
//     ERR("%s\n", msg);
//     exit(1);
// }

void test_hash32(int n, int word_size) {

    allocator *alloc = arena_create(default_allocator(), 4096);
    hashmap   *map   = map_create(alloc, sizeof(char *), 512);
    char      *buf   = alloc_malloc(alloc, (size_t)(n * (word_size + 1)));

    char      *ptr   = buf;
    int        i     = n;
    while (i--) {
        for (int j = 0; j < word_size; ++j) {
            *ptr++ = (char)(0x20 + (rand() % (0x7f - 0x20)));
        }
        *ptr++ = 0;
    }

    u32 collisions = 0;

    ptr            = buf;
    i              = n;
    while (i--) {
        u32   h = hash32((byte *)ptr, (size_t)word_size);

        char *found;
        if ((found = map_get(map, &h, sizeof h))) {
            // only consider a collision if the actual string is the
            // same, so we don't cound collisions caused by poor
            // random generation.
            if (0 != strcmp(ptr, found)) collisions++;
        }
        map_set(&map, &h, sizeof h, ptr);
        ptr += word_size + 1;
    }

    OUT("%i strings of size %i, collisions = %u", n, word_size, collisions);

    arena_destroy(default_allocator(), &alloc);
}

int main(void) {

    unsigned int seed = (unsigned int)time(0);

    fprintf(stderr, "Seed = %u\n\n", seed);

    srand(seed);

    test_hash32(1000, 1);
    test_hash32(1000, 3);
    test_hash32(1000, 5);
    test_hash32(1000, 8);
    test_hash32(1000, 16);

    test_hash32(100000, 1);
    test_hash32(100000, 3);
    test_hash32(100000, 5);
    test_hash32(100000, 8);
    test_hash32(100000, 16);

    test_hash32(1000000, 1);
    test_hash32(1000000, 3);
    test_hash32(1000000, 5);
    test_hash32(1000000, 8);
    test_hash32(1000000, 16);

    return 0;
}
