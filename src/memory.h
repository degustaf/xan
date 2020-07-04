#ifndef XAN_MEMORY_H
#define XAN_MEMORY_H

#include <limits.h>
#include <stddef.h>

#include "object.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#define ALLOCATE(type, count) \
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count)); \
	printf("ALLOCATE %ld\n", sizeof(type) * (count))
#else /* DEBUG_LOG_GC */
#define ALLOCATE(type, count) \
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count))
#endif /* DEBUG_LOG_GC */

#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType, vm)

#define GROW_CAPACITY(capacity) \
	((capacity) < 8 ? 8 : (capacity) * 2)

#ifdef DEBUG_LOG_GC
#define GROW_ARRAY(previous, type, oldCount, count) \
	reallocate(vm, previous, sizeof(type) * (oldCount), sizeof(type) * (count)); \
	printf("GROW_ARRAY from %ld to %ld\n", sizeof(type) * (oldCount), sizeof(type) * (count))
#else /* DEBUG_LOG_GC */
#define GROW_ARRAY(previous, type, oldCount, count) \
	reallocate(vm, previous, sizeof(type) * (oldCount), sizeof(type) * (count))
#endif /* DEBUG_LOG_GC */

#define FREE_ARRAY(type, pointer, oldCount) \
	reallocate(vm, pointer, sizeof(type) * (oldCount), 0)

#define FREE(type, pointer) \
	reallocate(vm, pointer, sizeof(type), 0)

#define isWhite(o) (!((Obj*)(o))->isMarked)
#define fwdWriteBarrier(vm, v) markValue(vm, v)

static inline size_t round_up_pow_2(size_t n) {
	n--;
	for(size_t i = 1; i < sizeof(size_t) * CHAR_BIT; i <<= 1)
		n |= n >> i;
	n++;
	return n;
}

Obj* allocateObject(size_t size, ObjType type, VM *vm);
void* reallocate(VM *vm, void* previous, size_t oldSize, size_t newSize);
void markValue(VM *vm, Value v);
void freeObjects(VM *vm);
void freeChunk(VM *vm, Chunk *chunk);

#endif /* XAN_MEMORY_H */
