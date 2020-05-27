#include "chunk.h"

#include "memory.h"

void initChunk(Chunk *chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	initValueArray(&chunk->constants);
}

void freeChunk(VM *vm, Chunk *chunk) {
	FREE_ARRAY(uint32_t, chunk->code, chunk->capacity);
	FREE_ARRAY(size_t, chunk->lines, chunk->capacity);
	freeValueArray(vm, &chunk->constants);
	initChunk(chunk);
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
	writeValueArray(vm, &chunk->constants, value);
	return chunk->constants.count - 1;
}
