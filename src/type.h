#ifndef XAN_TYPE_H
#define XAN_TYPE_H

#include "common.h"
#include "scanner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAMES_MAX 64
#define BASE_STACK_SIZE 100

#define OBJ_BUILDER(X) \
	X(STRING), \
	X(NATIVE), \
	X(FUNCTION), \
	X(CLOSURE), \
	X(UPVALUE), \
	X(CLASS), \
	X(INSTANCE),

typedef enum {
#define ENUM_BUILDER(x) OBJ_##x
	OBJ_BUILDER(ENUM_BUILDER)
#undef ENUM_BUILDER
} ObjType;

typedef uint8_t Reg;			// a register, i.e. a stack offset.
typedef uint32_t OP_position;	// an index into a bytecode array.

static const char* const ObjTypeNames[] = {
#define STRING_BUILDER(x) "OBJ_" #x
	OBJ_BUILDER(STRING_BUILDER)
#undef STRING_BUILDER
};

typedef struct sObj {
	ObjType type;
	bool isMarked;
	struct sObj *next;
} Obj;

typedef enum {
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
	VAL_OBJ,
} ValueType;

typedef struct {
	ValueType type;
	union {
		bool boolean;
		double number;
		Obj *obj;
	} as;
} Value;

typedef struct {
	size_t capacity;
	size_t count;
	Value *values;
} ValueArray;

// TODO turn chars into a flexible array member to optimize by minimizing allocations.
typedef struct sObjString {
	Obj obj;
	size_t length;
	uint32_t hash;
	char *chars;
} ObjString;

typedef struct {
	size_t count;
	size_t capacity;
	uint32_t *code;
	size_t *lines;
	ValueArray constants;
} Chunk;

typedef struct {
	Obj obj;
	int arity;
	size_t uvCount;
	Reg stackUsed;
	Chunk chunk;
	ObjString *name;
	uint16_t uv[];
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

#define UV_IS_LOCAL 0x100

typedef struct sUpvalue {
	Obj obj;
	Value *location;
	Value closed;
	struct sUpvalue *next;
} ObjUpvalue;

typedef struct {
	Obj obj;
	ObjFunction *f;
	ObjUpvalue **upvalues;
	size_t uvCount;
} ObjClosure;

typedef struct {
	ObjString *key;
	Value value;
} Entry;

typedef struct {
	size_t count;
	size_t capacity;
	Entry *entries;
} Table;

typedef struct sObjClass {
	Obj obj;
	ObjString *name;
	Table methods;
} ObjClass;

typedef struct {
	Obj obj;
	ObjClass *klass;
	Table fields;
} ObjInstance;

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

typedef struct sVM {
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
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
} InterpretResult;

// typedef struct sVM VM;

#endif /* XAN_TYPE_H */
