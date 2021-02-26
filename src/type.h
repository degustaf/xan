#ifndef XAN_TYPE_H
#define XAN_TYPE_H

#include "common.h"
#include "scanner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define BASE_STACK_SIZE 1024
#define TRY_MAX 16

#define OBJ_BUILDER(X, SEP) \
	X(STRING)SEP \
	X(NATIVE)SEP \
	X(FUNCTION)SEP \
	X(CLOSURE)SEP \
	X(UPVALUE)SEP \
	X(CLASS)SEP \
	X(MODULE)SEP \
	X(ARRAY)SEP \
	X(INSTANCE)SEP \
	X(BOUND_METHOD)SEP \
	X(TABLE)SEP \
	X(EXCEPTION)

typedef enum {
#define ENUM_BUILDER(x) OBJ_##x
#define COMMA ,
	OBJ_BUILDER(ENUM_BUILDER, COMMA)
#undef ENUM_BUILDER
} ObjType;

typedef uint8_t Reg;			// a register, i.e. a stack offset.
typedef uint32_t OP_position;	// an index into a bytecode array.

static const char* const ObjTypeNames[] = {
#define STRING_BUILDER(x) "OBJ_" #x
	OBJ_BUILDER(STRING_BUILDER, COMMA)
#undef STRING_BUILDER
};

struct sObj {
	ObjType type;
	bool isBlack;
	bool isGrey;
	struct sObj *next;
};

#ifdef TAGGED_NAN
XAN_STATIC_ASSERT(sizeof(double) == sizeof(uint64_t));
XAN_STATIC_ASSERT(sizeof(Obj*) <= sizeof(uint64_t));
typedef union {
	uint64_t u;
	double number;
	Obj *obj;
	intptr_t ip;
} Value;
XAN_STATIC_ASSERT(sizeof(Value) == sizeof(uint64_t));
#else /* TAGGED_NAN */
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
		intptr_t ip;
	} as;
} Value;
#endif /* TAGGED_NAN */

typedef struct sObjClass ObjClass;
typedef struct sObjTable ObjTable;

#define INSTANCE_FIELDS \
	Obj obj; \
	ObjClass *klass; \
	ObjTable *fields

typedef struct sObjString ObjString;

typedef struct {
	INSTANCE_FIELDS;
	size_t capacity;
	size_t count;
	Value *values;
} ObjArray;

// TODO turn chars into a flexible array member to optimize by minimizing allocations.
struct sObjString {
	INSTANCE_FIELDS;
	size_t length;
	uint32_t hash;
	char *chars;
};

typedef struct {
	size_t count;
	size_t capacity;
	uint32_t *code;
	size_t *lines;
	ObjArray *constants;
	ObjTable *constantIndices;
} Chunk;

typedef struct {
	Obj obj;
	int minArity;
	int maxArity;
	size_t uvCount;
	Reg stackUsed;
	Chunk chunk;
	ObjString *name;
	size_t *code_offsets;
	uint16_t uv[];
} ObjFunction;

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
	uint32_t *ip;
	size_t uvCount;
} ObjClosure;

typedef struct {
	INSTANCE_FIELDS;
} ObjInstance;

typedef struct {
	Obj obj;
	Value receiver;
	Obj *method;
} ObjBoundMethod;

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef enum {
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
	struct Compiler *enclosing;
	ObjString *name;
	Chunk chunk;
	FunctionType type;
	Local locals[UINT8_COUNT];
	uint16_t upvalues[UINT8_COUNT];
	size_t code_offsets[255];
	size_t uvCount;
	int minArity;
	int maxArity;
	ObjArray *defaultArgs;
	int scopeDepth;
	OP_position pendingJumpList;
	OP_position pendingBreakList;
	OP_position pendingContinueList;
	OP_position last_target;
	bool inLoop;
	Reg nextReg;
	Reg actVar;
	Reg maxReg;
} Compiler;

typedef struct ClassCompiler {
	struct ClassCompiler *enclosing;
	Token name;
	bool hasSuperClass;
	ObjTable *methods;
} ClassCompiler;

// struct sCallFrame {
// 	// ObjClosure *c;
// 	intptr_t ip;
// };
// typedef struct sCallFrame CallFrame;

struct try_frame {
	size_t frame;
	uint32_t ip;
	Reg exception;
};

typedef struct {
	Obj *objects;
	Obj **grayStack;
	size_t bytesAllocated;
	size_t nextMinorGC;
	size_t nextMajorGC;
	size_t grayCount;
	size_t grayCapacity;
	bool nextGCisMajor;
} GarbageCollector;

struct sVM {
	GarbageCollector gc;
	struct try_frame _try[TRY_MAX];
	size_t tryCount;
	Value *stack;
	Value *stackTop;
	Value *stackLast;
	Value *base;
	size_t stackSize;

	Value exception;
	ObjTable *strings;
	ObjTable *globals;
	ObjString *initString;
	ObjString *newString;
	ObjUpvalue *openUpvalues;
	Compiler *currentCompiler;
	ClassCompiler *currentClassCompiler;
};

typedef bool (*NativeFn)(VM *vm, int argCount, Value *args);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

typedef struct {
	const char *const name;
	NativeFn method;
} NativeDef;

struct sObjClass {
	INSTANCE_FIELDS;
	const char *cname;
	NativeDef *methodsArray;
	Obj *newFn;
	ObjString *name;
	ObjTable *methods;
	bool isException;
};

#define CLASS_HEADER {OBJ_CLASS, false, true, NULL,}, &classDef, NULL
// These fields should be NULL for static class definitions, and are created by defineNativeClass.
#define RUNTIME_CLASSDEF_FIELDS NULL, NULL, NULL

typedef struct {
	Obj obj;
	ObjString *name;
	ObjTable *items;
} ObjModule;

typedef struct {
	const char *const name;
	ObjClass **classes;
	NativeDef *methods;
} ModuleDef;

#endif /* XAN_TYPE_H */
