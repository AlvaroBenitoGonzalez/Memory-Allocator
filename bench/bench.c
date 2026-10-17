#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#ifdef USE_SYSTEM
    #define ALLOC(n) malloc(n)
    #define FREE(p) free(p)
    #define INIT() 0
#else
    #include "allocator.h"
    #define ALLOC(n) my_malloc(n)
    #define FREE(p) my_free(p)
    #define INIT() allocator_init()
#endif

#define ITERS 2000000
#define HOLD 200
#define RUNS 5

// mode 0
static void *worker_simple(void* arg) {
    unsigned seed = (unsigned)(size_t)arg;

    for (int i = 0; i < ITERS; i++) {
        char* p = ALLOC(1 + rand_r(&seed) % 512);
        if (!p) { fprintf(stderr, "malloc failed\n"); exit(1); }
        p[0] = 1;
        asm volatile("" : : "r"(p) : "memory");
        FREE(p);
    }

    return NULL;
}

// mode 1
static void* worker_batch(void* arg) {
    unsigned seed = (unsigned)(size_t)arg;
    char* ptrs[HOLD];

    for (int i = 0; i < ITERS / HOLD; i++) {
        for (int j = 0; j < HOLD; j++) {
            ptrs[j] = ALLOC(1 + rand_r(&seed) % 512);
            if (!ptrs[j]) { fprintf(stderr, "malloc failed\n"); exit(1); }
            ptrs[j][0] = 1;
            asm volatile("" : : "r"(ptrs) : "memory");
        }
        for (int i = 0; i < HOLD; i++) FREE(ptrs[i]);
    }

    return NULL;
}

static double run(int nthreads, void* (*fn)(void*)) {
    pthread_t t[64];
    struct timespec s, e;

    clock_gettime(CLOCK_MONOTONIC, &s);

    for (size_t i = 0; i < (size_t)nthreads; i++)
        pthread_create(&t[i], NULL, fn, (void *)(i+1));

    for (int i = 0; i < nthreads; i++)
        pthread_join(t[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &e);

    return (e.tv_sec - s.tv_sec) + (e.tv_nsec - s.tv_nsec) / 1e9;
}

static int cmp(const void* x, const void* y) {
    double s = *(const double*)x, e = *(const double*)y;
    return (s > e) - (s < e);
}

int main (int argc, char **argv) {
    int nthreads = argc > 1 ? atoi(argv[1]) : 4;
    int mode = argc > 2 ? atoi(argv[2]) : 0;
    if (nthreads < 1 || nthreads > 64) {
        fprintf(stderr, "Yo can only print between 1 and 64 threads\n");
        return 1;
    }

    if (INIT() != 0) { perror("init"); return 1; }

    void  *(*fn)(void*) = mode ? worker_batch : worker_simple;
    run(nthreads, fn);

    double times[RUNS];
    for (int i = 0; i < RUNS; i++) times[i] = run(nthreads, fn);
    qsort(times, RUNS, sizeof(double), cmp);

    double med = times[RUNS / 2];
    double ops = (double)nthreads * ITERS * 2;
    printf("%2d threads, mode %d: average %.3f s (%.1f M ops/s) min %.3f max %.3f\n",
        nthreads, mode, med, ops / med / 1e6, times[0], times[RUNS - 1]);
    return 0;
}