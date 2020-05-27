#ifndef XAN_MEMORY_H
#define XAN_MEMORY_H

#include <stddef.h>

#include "vm.h"

#define ALLOCATE(type, count) \
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) \
	reallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
	((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(previous, type, oldCount, count) \
	reallocate(vm, previous, sizeof(type) * (oldCount), sizeof(type) * (count))

#define FREE_ARRAY(type, pointer, oldCount) \
	reallocate(vm, pointer, sizeof(type) * (oldCount), 0)

#define isWhite(o) (!((Obj*)(o))->isMarked)

void* reallocate(VM *vm, void* previous, size_t oldSize, size_t newSize);
void collectGarbage(VM *vm);
void freeObjects(VM *vm);

#endif /* XAN_MEMORY_H */
