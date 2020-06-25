#include "chunk.h"

#include "memory.h"
#include "object.h"

void initChunk(VM *vm, Chunk *chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	chunk->constants = NULL;	// For GC
	chunk->constants = newArray(vm, 0);
}

size_t writeChunk(VM *vm, Chunk *chunk, uint32_t opcode, size_t line) {
	size_t count = chunk->count;
	if(chunk->capacity < count + 1) {
		size_t oldCapacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY(chunk->code, uint32_t, oldCapacity, chunk->capacity);
		chunk->lines = GROW_ARRAY(chunk->lines, size_t, oldCapacity, chunk->capacity);
	}

	chunk->code[count] = opcode;
	chunk->lines[count] = line;
	chunk->count = count + 1;
	return count;
}

size_t addConstant(VM *vm, Chunk *chunk, Value value) {
	vm->temp4GC = value;
	writeValueArray(vm, chunk->constants, value);
	assert(IS_NIL(value) || !IS_NIL(vm->temp4GC));
	vm->temp4GC = NIL_VAL;
	return chunk->constants->count - 1;
}
