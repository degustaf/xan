#ifndef XAN_MEMORY_H
#define XAN_MEMORY_H

#include <limits.h>
#include <stddef.h>

#include "object.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#define ALLOCATE(vm, type, count) \
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count)); \
	printf("ALLOCATE %ld\n", sizeof(type) * (count))
#else /* DEBUG_LOG_GC */
#define ALLOCATE(vm, type, count) \
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count))
#endif /* DEBUG_LOG_GC */

#define ALLOCATE_OBJ(vm, type, objectType) \
	(type*)allocateObject(sizeof(type), objectType, vm)

#define GROW_CAPACITY(capacity) \
	((capacity) < 8 ? 8 : (capacity) * 2)

#ifdef DEBUG_LOG_GC
#define GROW_ARRAY(vm, previous, type, oldCount, count) \
	reallocate(vm, previous, sizeof(type) * (oldCount), sizeof(type) * (count)); \
	printf("GROW_ARRAY from %ld to %ld\n", sizeof(type) * (oldCount), sizeof(type) * (count))
#else /* DEBUG_LOG_GC */
#define GROW_ARRAY(vm, previous, type, oldCount, count) \
	reallocate(vm, previous, sizeof(type) * (oldCount), sizeof(type) * (count))
#endif /* DEBUG_LOG_GC */

#define FREE_ARRAY(gc, type, pointer, oldCount) \
	_free(gc, pointer, sizeof(type) * (oldCount))

#define FREE(gc, type, pointer) \
	_free(gc, pointer, sizeof(type))

#define isWhite(o) (!((Obj*)(o))->isBlack)
#define isGrey(o) (((Obj*)(o))->isGrey)
#define writeBarrier(vm, o) if(!isGrey((o))) setGrey(&(vm)->gc, ((Obj*)(o)))

static inline size_t round_up_pow_2(size_t n) {
	n--;
	for(size_t i = 1; i < sizeof(size_t) * CHAR_BIT; i <<= 1)
		n |= n >> i;
	n++;
	return n;
}

Obj* allocateObject(size_t size, ObjType type, VM *vm);
void* reallocate(VM *vm, void* previous, size_t oldSize, size_t newSize);
void _free(GarbageCollector *gc, void* previous, size_t oldSize);
void markValue(GarbageCollector *gc, Value v);
void freeObjects(GarbageCollector *gc);
void freeChunk(GarbageCollector *gc, Chunk *chunk);
void setGrey(GarbageCollector *gc, Obj *o);

#endif /* XAN_MEMORY_H */
