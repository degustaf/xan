#ifndef XAN_VM_H
#define XAN_VM_H

#include "chunk.h"

#include <stddef.h>

#define FRAMES_MAX 64
#define BASE_STACK_SIZE 100

#include "table.h"

typedef struct {
	ObjClosure *c;
	uint32_t *ip;
	Value *slots;
} CallFrame;

struct sVM {
	CallFrame frames[FRAMES_MAX];
	int frameCount;
	Value *stack;
	Value *stackTop;
	Value *stackLast;
	size_t stackSize;

	Table strings;
	Table globals;
	ObjUpvalue *openUpvalues;
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
