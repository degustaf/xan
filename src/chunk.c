#include "chunk.h"

#include "array.h"
#include "memory.h"
#include "table.h"

void initChunk(VM *vm, thread *currentThread, Chunk *chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	chunk->constants = NULL;		// For GC
	chunk->constantIndices = NULL;	// For GC
	incCFrame(vm, currentThread, 1, 3);
	ObjArray *a = newArray(vm, currentThread, 0);
	writeBarrier(vm, a);
	chunk->constants = a;
	ObjTable *t = newTable(vm, currentThread, 0);
	writeBarrier(vm, t);
	chunk->constantIndices = t;
	decCFrame(currentThread);
}

void finalizeChunk(Chunk *chunk) {
	chunk->constantIndices = NULL;
}

size_t writeChunk(VM *vm, Chunk *chunk, uint32_t opcode, size_t line) {
	size_t count = chunk->count;
	if(chunk->capacity < count + 1) {
		size_t oldCapacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY(vm, chunk->code, uint32_t, oldCapacity, chunk->capacity);
		chunk->lines = GROW_ARRAY(vm, chunk->lines, size_t, oldCapacity, chunk->capacity);
	}

	chunk->code[count] = opcode;
	chunk->lines[count] = line;
	chunk->count = count + 1;
	return count;
}

size_t addConstant(VM *vm, Chunk *chunk, Value value) {
	if(IS_STRING(value) || IS_NUMBER(value)) {
		Value ret;
		assert(chunk->constantIndices);
		if(tableGet(chunk->constantIndices, value, &ret))
			return (size_t)AS_NUMBER(ret);
		tableSet(vm, chunk->constantIndices, value, NUMBER_VAL(chunk->constants->count));
	}
	writeValueArray(vm, chunk->constants, value);
	return chunk->constants->count - 1;
}
