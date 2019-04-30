#ifndef XAN_VM_H
#define XAN_VM_H

#include "chunk.h"

#include <stddef.h>

// #define BASE_STACK_SIZE 40
#define BASE_STACK_SIZE 100

#include "table.h"

struct sVM {
	Chunk *chunk;
	uint32_t *ip;
	Value *stack;
	Value *stackTop;
	Value *stackLast;
	size_t stackSize;

	Table strings;
	Table globals;
	Obj *objects;
};

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void initVM(VM *vm);
void freeVM(VM *vm);
InterpretResult interpret(VM *vm, const char *source);

#endif /* XAN_VM_H */
