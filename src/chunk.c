#include "chunk.h"

#include "memory.h"

void initChunk(Chunk *chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;
	initValueArray(&chunk->constants);
}

void freeChunk(Chunk *chunk) {
	FREE_ARRAY(uint32_t, chunk->code, chunk->capacity);
	FREE_ARRAY(size_t, chunk->lines, chunk->capacity);
	freeValueArray(&chunk->constants);
	initChunk(chunk);
}

void writeChunk(Chunk *chunk, uint32_t opcode, size_t line) {
	if(chunk->capacity < chunk->count + 1) {
		size_t oldCapacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY(chunk->code, uint32_t, oldCapacity, chunk->capacity);
		chunk->lines = GROW_ARRAY(chunk->lines, size_t, oldCapacity, chunk->capacity);
	}

	chunk->code[chunk->count] = opcode;
	chunk->lines[chunk->count] = line;
	chunk->count++;
}

size_t addConstant(Chunk *chunk, Value value) {
	writeValueArray(&chunk->constants, value);
	return chunk->constants.count - 1;
}
