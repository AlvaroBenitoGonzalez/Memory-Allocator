#ifndef ALLOCATOR_H
#define ALLOCATOR_H
#include <stddef.h>

int   allocator_init(void);
void *my_malloc(size_t size);
void  my_free(void *ptr);

#endif