#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <pthread.h>

#include "allocator.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#define HEAP_SIZE (128 * 1024 * 1024)

typedef struct Header {
    size_t size;
    struct Header* next;
} Header;

static Header* free_list_head = NULL;

int allocator_init(void) {
    void* region = mmap(NULL, HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) return -1;

    free_list_head = (Header*) region;
    free_list_head->size = HEAP_SIZE - sizeof(Header);
    free_list_head->next = NULL;
    return 0;
}

// general allocator
#define ALIGNMENT 16
#define ALIGN_UP(n) (((n) + (ALIGNMENT-1)) & ~(size_t)(ALIGNMENT-1))

static void* general_malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN_UP(size);

    // Search a memory block
    Header* prev = NULL;
    Header* curr = free_list_head;
    while (curr && curr->size < size) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) return NULL;

    // Split the block
    if (curr->size >= size +  sizeof(Header) + ALIGNMENT) {
        Header* rest = (Header*) ((char*) curr + sizeof(Header) + size);
        rest->size = curr->size - size - sizeof(Header);
        rest->next = curr->next;
        curr->size = size;

        if (prev) prev->next = rest;
        else free_list_head = rest;
    } else {
        if (prev)
            prev->next = curr->next;
        else 
        free_list_head = curr->next;
    }

    curr->next = NULL;
    return (char*) curr + sizeof(Header);
}

static void general_free(void* ptr) {
    if (!ptr) return;

    Header* block = (Header*) ((char*) ptr - sizeof(Header));

    Header* prev = NULL;
    Header* curr = free_list_head;
    while (curr && curr < block)
    {
        prev = curr;
        curr = curr->next;
    }

    block->next = curr;
    if (prev) prev->next = block;
    else free_list_head = block;
    
    if (curr && (char*)block + sizeof(Header) + block->size == (char*)curr) {
        block->size += sizeof(Header) + curr->size;
        block->next = curr->next;
    }

    if (prev && (char*)prev + sizeof(Header) + prev->size == (char*)block) {
        prev->size += sizeof(Header) + block->size;
        prev->next = block->next;
    }
}

// "small" allocator
#define NUM_CLASSES 8
#define MAX_SMALL 2048
#define BATCH 32

static const size_t class_sizes[NUM_CLASSES] = {16, 32, 64, 128, 256, 512, 1024, 2048};
static Header* buckets[NUM_CLASSES] = {NULL};

static int class_index(size_t size) {
    for (int i = 0; i < NUM_CLASSES; i++)
        if (size <= class_sizes[i]) return i;
    return -1;
}

static int refill(int idx) {
    size_t csize = class_sizes[idx];
    size_t stride = sizeof(Header) + csize;

    char* slab = general_malloc(stride * BATCH);
    if (!slab) return -1;

    for (int i = 0; i < BATCH; i++) {
        Header* b = (Header*)(slab + i * stride);
        b->size = csize;
        b->next = buckets[idx];
        buckets[idx] = b;
    }
    return 0;
}

#define CACHE_MAX 64

static __thread Header* tcache[NUM_CLASSES];
static __thread int tcache_len[NUM_CLASSES];

static void fetch_from_global(int idx) {
    pthread_mutex_lock(&g_lock);
    if (!buckets[idx]) refill(idx);
    for (int i = 0; i < BATCH && buckets[idx]; i++) {
        Header* b = buckets[idx];
        buckets[idx] = b->next;
        b->next = tcache[idx];
        tcache[idx] = b;
        tcache_len[idx]++;
    }
    pthread_mutex_unlock(&g_lock);
}

static void release_to_global(int idx) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < BATCH && tcache[idx]; i++) {
        Header* b = tcache[idx];
        tcache[idx] = b->next;
        tcache_len[idx]--;
        b->next = buckets[idx];
        buckets[idx] = b;
    }
    pthread_mutex_unlock(&g_lock);
}

void* my_malloc(size_t size) {
    if (size == 0 || size > HEAP_SIZE) return NULL;

    int idx = class_index(size);
    if (idx < 0) {
        pthread_mutex_lock(&g_lock);
        void* p = general_malloc(size);
        pthread_mutex_unlock(&g_lock);
        return p;
    }

    if (!tcache[idx]) {
        fetch_from_global(idx);
        if (!tcache[idx]) return NULL;
    }

    Header* b = tcache[idx];
    tcache[idx] = b->next;
    tcache_len[idx]--;
    b->next = NULL;
    return (char*)b + sizeof(Header);
}

void my_free(void* ptr) {
    if (!ptr) return;

    Header* block = (Header*)((char*)ptr - sizeof(Header));
    int idx = class_index(block->size);

    if (idx < 0) {
        pthread_mutex_lock(&g_lock);
        general_free(ptr);
        pthread_mutex_unlock(&g_lock);
        return;
    }

    block->next = tcache[idx];
    tcache[idx] = block;
    if (++tcache_len[idx] > CACHE_MAX)
        release_to_global(idx);
}