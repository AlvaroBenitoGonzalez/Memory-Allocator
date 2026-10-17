# Memory-Allocator

A small, thread-aware memory allocator in C for Linux, built as a learning project. It exposes a `malloc`/`free`-style API and ships with a multithreaded benchmark that compares it against the system (glibc) allocator.

## Design

The allocator manages a single 128 MiB region obtained from `mmap` at start-up and combines two strategies:

**Small allocations (≤ 2048 bytes): size classes + per-thread caches**
- Requests are rounded up to one of 8 size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes.
- Each thread has a thread-local cache (`__thread`) with one free list per class. The fast path of `my_malloc` / `my_free` touches only this cache, so it needs **no locking**.
- When a cache is empty, it fetches a batch of 32 blocks from the global bucket for that class. When a cache grows beyond 64 blocks, a batch of 32 is returned to the global bucket. Both operations take a single global mutex.
- When a global bucket is empty, it is refilled by carving a slab of 32 blocks out of the general allocator.

**Large allocations (> 2048 bytes): general free-list allocator**
- First-fit search over an address-ordered singly linked free list.
- Blocks are split when the remainder is large enough to be useful.
- On free, blocks are inserted in address order and coalesced with adjacent free neighbours.
- Protected by the same global mutex.

All blocks carry a 16-byte header (`size`, `next`) and returned pointers are 16-byte aligned.

## API

```c
#include "allocator.h"

int   allocator_init(void);        // call once, before any other call; returns 0 on success
void *my_malloc(size_t size);      // returns NULL if size == 0, size is too large, or memory is exhausted
void  my_free(void *ptr);          // ptr must come from my_malloc (or be NULL)
```

Example:

```c
#include "allocator.h"

int main(void) {
    if (allocator_init() != 0) return 1;

    char *buf = my_malloc(100);
    // ... use buf ...
    my_free(buf);
    return 0;
}
```

## Building

Requires CMake ≥ 3.10, a C11 compiler (GCC/Clang) and pthreads. Linux only (uses `mmap` and `__thread`).

```sh
cmake -B build
cmake --build build
```

The default build type is `Release` (`-O2`). The benchmark binaries are written to `bench/`:

- `bench/b_myalloc`: benchmark linked against this allocator
- `bench/b_malloc`: the same benchmark using the system `malloc`/`free`

## Benchmark

```sh
./bench/b_myalloc <threads> <mode>
./bench/b_malloc  <threads> <mode>
```

- `threads`: number of threads, 1–64 (default 4)
- `mode`:
  - `0`: **simple**. Each thread repeatedly allocates a random size (1–512 bytes) and immediately frees it (2,000,000 iterations per thread).
  - `1`: **batch**. Each thread allocates 200 blocks, then frees all 200, repeated for the same total number of allocations.

Each configuration does one warm-up run followed by 5 timed runs, and reports the median, min and max time plus throughput in millions of operations per second (an alloc + a free count as 2 ops).

To run the full matrix (1/2/4/8 threads × both modes, for both allocators), use the script:

```sh
./bench/run.sh
```

### Sample results

Median of 5 runs, in M ops/s (higher is better), from one machine:

| Threads | Mode 0: myalloc | Mode 0: glibc | Mode 1: myalloc | Mode 1: glibc |
|--------:|----------------:|--------------:|----------------:|--------------:|
| 1       | 98.6            | 107.2         | 111.5           | 138.2         |
| 2       | 276.0           | 510.4         | 103.4           | 274.8         |
| 4       | 545.4           | 973.7         | 126.2           | 499.9         |
| 8       | 1010.2          | 1795.2        | 139.5           | 955.0         |

Takeaways:

- In the simple alloc/free-immediately pattern, the thread-local caches make this allocator **scale almost linearly** with thread count, at roughly 55–90% of glibc's throughput.
- In the batch pattern, throughput does **not scale**: holding 200 live blocks exceeds the cache limit (64), so threads constantly fall through to the global mutex, which becomes the bottleneck as threads are added. glibc's per-thread arenas handle this much better.

Numbers depend heavily on hardware, core count and compiler, so run the benchmark yourself to compare.

## Limitations

This is an educational allocator, not a drop-in replacement for `malloc`:

- Fixed 128 MiB heap; it never grows and never returns memory to the OS.
- No `realloc`, `calloc`, `aligned_alloc` or `posix_memalign`.
- `allocator_init()` is not thread-safe and must be called exactly once, before any threads use the allocator.
- Memory in per-thread caches is not returned when a thread exits.
- Slabs carved for the small size classes are never given back to the general allocator, so memory can fragment between classes.
- No double-free or invalid-pointer detection.

## License

Released under the [Apache License 2.0](LICENSE).