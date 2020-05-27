#ifndef XAN_VM_H
#define XAN_VM_H

#include "chunk.h"

#include <stddef.h>

#define FRAMES_MAX 64
#define BASE_STACK_SIZE 100

#include "scanner.h"
#include "table.h"

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef enum {
	TYPE_FUNCTION,
	TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
	struct Compiler *enclosing;
	ObjString *name;
	Chunk chunk;
	FunctionType type;
	Local locals[UINT8_COUNT];
	uint16_t upvalues[UINT8_COUNT];
	size_t uvCount;
	int arity;
	size_t localCount;
	int scopeDepth;
	OP_position pendingJumpList;
	OP_position last_target;
	Reg nextReg;
	Reg actVar;
	Reg maxReg;
} Compiler;

typedef struct {
	ObjClosure *c;
	uint32_t *ip;
	Value *slots;
} CallFrame;

struct sVM {
	CallFrame frames[FRAMES_MAX];
	size_t frameCount;
	Value *stack;
	Value *stackTop;
	Value *stackLast;
	size_t stackSize;

	Table strings;
	Table globals;
	ObjUpvalue *openUpvalues;
	Obj *objects;
	Compiler *currentCompiler;
	size_t bytesAllocated;
	size_t nextGC;
	Value temp4GC;
	size_t grayCount;
	size_t grayCapacity;
	Obj** grayStack;
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
