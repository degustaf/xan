#include "vm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "parse.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

static void resetStack(VM *vm) {
	vm->stackTop = vm->stack;
}

static bool isFalsey(Value v) {
	return IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v));
}

static Value concatenate(VM *vm, ObjString *b, ObjString *c) {
	size_t length = b->length + c->length;
	char *chars = ALLOCATE(char, length+1);
	memcpy(chars, b->chars, b->length);
	memcpy(chars + b->length, c->chars, c->length);
	chars[length] = '\0';

	ObjString *result = takeString(chars, length, vm);
	return OBJ_VAL(result);
}

void initVM(VM *vm) {
	vm->stackSize = BASE_STACK_SIZE;
	vm->stack = GROW_ARRAY(NULL, Value, 0, vm->stackSize);
	resetStack(vm);
	vm->stackLast = vm->stack + vm->stackSize - 1;

	vm->objects = NULL;
	initTable(&vm->strings);
	initTable(&vm->globals);
}

void freeVM(VM *vm) {
	FREE_ARRAY(Value, vm->stack, vm->stackSize);
	vm->stackSize = 0;
	vm->stackTop = vm->stackLast = NULL;
	freeTable(&vm->strings);
	freeTable(&vm->globals);
	freeObjects(vm);
}

static void runtimeError(VM *vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	size_t instruction = vm->ip - vm->chunk->code - 1;	// We have  already advanced ip.
	fprintf(stderr, "[line %zu] in script\n", vm->chunk->lines[instruction]);

	resetStack(vm);
}

#define READ_BYTECODE() (*vm->ip++)
#define BINARY_OPVV(valueType, op) \
	do { \
		Value b = vm->stackTop[RB(bytecode)]; \
		Value c = vm->stackTop[RC(bytecode)]; \
		if(!IS_NUMBER(b) || !IS_NUMBER(c)) { \
			runtimeError(vm, "Operands must be numbers."); \
			return INTERPRET_RUNTIME_ERROR; \
		} \
		vm->stackTop[RA(bytecode)] = valueType(AS_NUMBER(b) op AS_NUMBER(c)); \
	} while(false)
#define READ_STRING() AS_STRING(vm->chunk->constants.values[RD(bytecode)])
static InterpretResult run(VM *vm) {
	while(true) {
#ifdef DEBUG_TRACE_EXECUTION
		disassembleInstruction(vm->chunk, vm->ip - vm->chunk->code);
#endif
		uint32_t bytecode = READ_BYTECODE();
		switch(OP(bytecode)) {
			case OP_CONST_NUM:
				vm->stackTop[RA(bytecode)] = vm->chunk->constants.values[RD(bytecode)];
				break;
			case OP_PRIMITIVE:
				vm->stackTop[RA(bytecode)] = getPrimitive(RD(bytecode)); break;
			case OP_NEGATE: {
				Value vRD = vm->stackTop[RD(bytecode)];
				if(!IS_NUMBER(vRD)) {
					runtimeError(vm, "Operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				vm->stackTop[RA(bytecode)] = NUMBER_VAL(-AS_NUMBER(vRD));
				break;
			}
			case OP_NOT: {
				Value vRD = vm->stackTop[RD(bytecode)];
				vm->stackTop[RA(bytecode)] = BOOL_VAL(isFalsey(vRD));
				break;
			}
			case OP_GET_GLOBAL: {
				ObjString *name = READ_STRING();
				Value value;
				if(!tableGet(&vm->globals, name, &value)) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				vm->stackTop[RA(bytecode)] = value;
				break;
			}
			case OP_DEFINE_GLOBAL: {
				ObjString *name = AS_STRING(vm->chunk->constants.values[RD(bytecode)]);
				tableSet(&vm->globals, name, vm->stackTop[RA(bytecode)]);
				break;
			}
			case OP_SET_GLOBAL: {
				ObjString *name = READ_STRING();
				if(tableSet(&vm->globals, name, vm->stackTop[RA(bytecode)])) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_EQUAL: {
				Value b = vm->stackTop[RB(bytecode)];
				Value c = vm->stackTop[RC(bytecode)];
				vm->stackTop[RA(bytecode)] = BOOL_VAL(valuesEqual(b, c));
				break;
			}
			case OP_NEQ: {
				Value b = vm->stackTop[RB(bytecode)];
				Value c = vm->stackTop[RC(bytecode)];
				vm->stackTop[RA(bytecode)] = BOOL_VAL(!valuesEqual(b, c));
				break;
			}
			case OP_GREATER:  BINARY_OPVV(BOOL_VAL, >); break;
			case OP_GEQ:      BINARY_OPVV(BOOL_VAL, >=); break;
			case OP_LESS:     BINARY_OPVV(BOOL_VAL, <); break;
			case OP_LEQ:      BINARY_OPVV(BOOL_VAL, <=); break;
			case OP_ADDVV: {
				Value b = vm->stackTop[RB(bytecode)];
				Value c = vm->stackTop[RC(bytecode)];
				if(IS_STRING(b) && IS_STRING(c)) {
					vm->stackTop[RA(bytecode)] = concatenate(vm, AS_STRING(b), AS_STRING(c));
				} else if(IS_NUMBER(b) && IS_NUMBER(c)) {
					vm->stackTop[RA(bytecode)] = NUMBER_VAL(AS_NUMBER(b) + AS_NUMBER(c));
				} else {
					runtimeError(vm, "Operands must be two numbers or two strings.");
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_SUBVV:	  BINARY_OPVV(NUMBER_VAL, -); break;
			case OP_MULVV:	  BINARY_OPVV(NUMBER_VAL, *); break;
			case OP_DIVVV:	  BINARY_OPVV(NUMBER_VAL, /); break;
			case OP_PRINT:
				printValue(vm->stackTop[RA(bytecode)]);
				printf("\n");
				break;
			case OP_RETURN:
				return INTERPRET_OK;
		}
	}
}
#undef BINARY_OPVV
#undef READ_BYTECODE

InterpretResult interpret(VM *vm, const char *source) {
	Chunk chunk;
	initChunk(&chunk);

	if(!parse(vm, source, &chunk)) {
		freeChunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm->chunk = &chunk;
	vm->ip = vm->chunk->code;

	InterpretResult result = run(vm);

	freeChunk(&chunk);
	return result;
}
