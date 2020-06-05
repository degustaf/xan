#include "vm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memory.h"
#include "parse.h"

#if defined(DEBUG_TRACE_EXECUTION) || defined(DEBUG_STACK_USAGE)
#include "debug.h"
#endif

static void resetStack(VM *vm) {
	vm->stackTop = vm->stack;
	vm->frameCount = 0;
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

static Value clockNative(__attribute__((unused))int argCount, __attribute__((unused))Value *args) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value printNative(__attribute__((unused))int argCount, Value *args) {
	// if(argCount != 1) {		// TODO add error handling to native functions.
	// 	runtimeError(vm, "Expected 1 argument but got %d.", argCount);
	// 	return NIL_VAL;
	// }
	printValue(*args);
	printf("\n");
	return NIL_VAL;
}

static void defineNative(VM *vm, Reg base, const char *name, NativeFn function) {
	vm->stack[base] = OBJ_VAL(copyString(name, strlen(name), vm));
	vm->stack[base+1] = OBJ_VAL(newNative(vm, function));
	tableSet(vm, &vm->globals, AS_STRING(vm->stack[base]), vm->stack[base+1]);
}

void initVM(VM *vm) {
	vm->objects = NULL;
	vm->grayCount = 0;
	vm->grayCapacity = 0;
	vm->grayStack = NULL;
	vm->openUpvalues = NULL;
	vm->currentCompiler = NULL;
	vm->bytesAllocated = 0;
	vm->nextGC = 1024 * 1024;
	vm->temp4GC = NIL_VAL;

	initTable(&vm->strings);
	initTable(&vm->globals);

	vm->stackSize = BASE_STACK_SIZE;
	vm->stack = NULL;
	vm->stackLast = NULL;
	resetStack(vm);
	vm->stack = GROW_ARRAY(NULL, Value, 0, vm->stackSize);
	vm->stackLast = vm->stack + vm->stackSize - 1;
	for(size_t i = 0; i <vm->stackSize; i++)
		vm->stack[i] = NIL_VAL;
	resetStack(vm);

	vm->stackTop = vm->stack + 2;
	defineNative(vm, 0, "clock", clockNative);
	defineNative(vm, 0, "print", printNative);
}

void freeVM(VM *vm) {
	FREE_ARRAY(Value, vm->stack, vm->stackSize);
	vm->stackSize = 0;
	vm->stackTop = vm->stackLast = NULL;
	freeTable(vm, &vm->strings);
	freeTable(vm, &vm->globals);
	freeObjects(vm);
}

static void runtimeError(VM *vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for(size_t i = vm->frameCount; i > 0; i--) {
		CallFrame *frame = &vm->frames[i-1];
		ObjFunction *f = frame->c->f;
		size_t instruction = frame->ip - f->chunk.code - 1;	// We have already advanced ip.
		fprintf(stderr, "[line %zu] in ", frame->c->f->chunk.lines[instruction]);
		if(f->name == NULL) {
			fprintf(stderr, "script\n");
		} else {
			fprintf(stderr, "%s()\n", f->name->chars);
		}
	}

	resetStack(vm);
}

static bool call(VM *vm, ObjClosure *function, Value *base, Reg argCount, Reg retCount) {
	if(argCount != function->f->arity) {	// TODO make variadic functions.
		runtimeError(vm, "Expected %d arguments but got %d.", function->f->arity, argCount);
		return false;
	}
	if(vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "Stack overflow.");
		return false;
	}
	CallFrame *frame = &vm->frames[vm->frameCount++];
	if(base + function->f->stackUsed > vm->stackLast) {
		vm->frameCount--;
		runtimeError(vm, "Stack overflow.");
		return false;
	}
	if(base + function->f->stackUsed >= vm->stackTop)
		vm->stackTop = base + function->f->stackUsed + 1;

	frame->c = function;
	frame->ip = function->f->chunk.code;

	frame->slots = base + 1;
	return true;
}

static bool callValue(VM *vm, Value *callee, Reg retCount, Reg argCount) {
	if(IS_OBJ(*callee)) {
		switch(OBJ_TYPE(*callee)) {
			case OBJ_BOUND_METHOD: {
				ObjBoundMethod *bound = AS_BOUND_METHOD(*callee);
				return call(vm, bound->method, callee, argCount, retCount);
			}
			case OBJ_CLASS: {
				ObjClass *klass = AS_CLASS(*callee);
				*callee = OBJ_VAL(newInstance(vm, klass));
				return true;
			}
			case OBJ_CLOSURE:
				return call(vm, AS_CLOSURE(*callee), callee, argCount, retCount);
			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(*callee);
				Value result = native(argCount, callee+1);
				*callee = result;
				return true;
			}
			default:
				break;
		}
	}

	runtimeError(vm, "Can only call functions and classes.");
	return false;
}

static bool bindMethod(VM *vm, ObjInstance *instance, ObjString *name, Value *slot) {
	Value method;
	if(!tableGet(&instance->klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod *bound = newBoundMethod(vm, OBJ_VAL(instance), AS_CLOSURE(method));
	*slot = (OBJ_VAL(bound));
	return true;
}

static ObjUpvalue *captureUpvalue(VM *vm, Value *local) {
	ObjUpvalue **uv = &vm->openUpvalues;

	while(*uv && (*uv)->location > local)
		uv = &(*uv)->next;

	if(*uv && ((*uv)->location == local))
		return *uv;

	ObjUpvalue *ret = newUpvalue(vm, local);
	ret->next = *uv;
	*uv = ret;
	return ret;
}

static void closeUpvalues(VM *vm, Value *last) {
	while(vm->openUpvalues && vm->openUpvalues->location >= last) {
		ObjUpvalue *uv = vm->openUpvalues;
		uv->closed = *uv->location;
		uv->location = &uv->closed;
		vm->openUpvalues = uv->next;
	}
}

static void defineMethod(VM *vm, Value ra, Value rb, Value rc) {
	assert(IS_CLASS(ra));
	assert(IS_STRING(rb));
	ObjClass *klass = AS_CLASS(ra);
	ObjString *name = AS_STRING(rb);
	tableSet(vm, &klass->methods, name, rc);
}

#define READ_BYTECODE() (*frame->ip++)
#define BINARY_OPVV(valueType, op) \
	do { \
		Value b = frame->slots[RB(bytecode)]; \
		Value c = frame->slots[RC(bytecode)]; \
		if(!IS_NUMBER(b) || !IS_NUMBER(c)) { \
			runtimeError(vm, "Operands must be numbers."); \
			return INTERPRET_RUNTIME_ERROR; \
		} \
		frame->slots[RA(bytecode)] = valueType(AS_NUMBER(b) op AS_NUMBER(c)); \
	} while(false)
#define READ_STRING() AS_STRING(frame->c->f->chunk.constants.values[RD(bytecode)])
static InterpretResult run(VM *vm) {
	CallFrame *frame = &vm->frames[vm->frameCount - 1];
	while(true) {
#ifdef DEBUG_STACK_USAGE
		dumpStack(vm, 5);
#endif /* DEBUG_STACK_USAGE */
#ifdef DEBUG_TRACE_EXECUTION
		disassembleInstruction(&frame->c->f->chunk, frame->ip - frame->c->f->chunk.code);
#endif
#ifdef DEBUG_STACK_USAGE
		printf("\n");
#endif /* DEBUG_STACK_USAGE */
		uint32_t bytecode = READ_BYTECODE();
		switch(OP(bytecode)) {
			case OP_CONST_NUM:
				frame->slots[RA(bytecode)] = frame->c->f->chunk.constants.values[RD(bytecode)];
				break;
			case OP_PRIMITIVE:
				frame->slots[RA(bytecode)] = getPrimitive(RD(bytecode)); break;
			case OP_NEGATE: {
				Value vRD = frame->slots[RD(bytecode)];
				if(!IS_NUMBER(vRD)) {
					runtimeError(vm, "Operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				frame->slots[RA(bytecode)] = NUMBER_VAL(-AS_NUMBER(vRD));
				break;
			}
			case OP_NOT: {
				Value vRD = frame->slots[RD(bytecode)];
				frame->slots[RA(bytecode)] = BOOL_VAL(isFalsey(vRD));
				break;
			}
			case OP_GET_GLOBAL: {
				ObjString *name = READ_STRING();
				Value value;
				if(!tableGet(&vm->globals, name, &value)) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				frame->slots[RA(bytecode)] = value;
				break;
			}
			case OP_DEFINE_GLOBAL: {
				ObjString *name = READ_STRING();
				tableSet(vm, &vm->globals, name, frame->slots[RA(bytecode)]);
				break;
			}
			case OP_SET_GLOBAL: {
				ObjString *name = READ_STRING();
				if(tableSet(vm, &vm->globals, name, frame->slots[RA(bytecode)])) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_EQUAL: {
				Value b = frame->slots[RB(bytecode)];
				Value c = frame->slots[RC(bytecode)];
				frame->slots[RA(bytecode)] = BOOL_VAL(valuesEqual(b, c));
				break;
			}
			case OP_NEQ: {
				Value b = frame->slots[RB(bytecode)];
				Value c = frame->slots[RC(bytecode)];
				frame->slots[RA(bytecode)] = BOOL_VAL(!valuesEqual(b, c));
				break;
			}
			case OP_GREATER:  BINARY_OPVV(BOOL_VAL, >); break;
			case OP_GEQ:      BINARY_OPVV(BOOL_VAL, >=); break;
			case OP_LESS:     BINARY_OPVV(BOOL_VAL, <); break;
			case OP_LEQ:      BINARY_OPVV(BOOL_VAL, <=); break;
			case OP_ADDVV: {
				Value b = frame->slots[RB(bytecode)];
				Value c = frame->slots[RC(bytecode)];
				if(IS_STRING(b) && IS_STRING(c)) {
					frame->slots[RA(bytecode)] = concatenate(vm, AS_STRING(b), AS_STRING(c));
				} else if(IS_NUMBER(b) && IS_NUMBER(c)) {
					frame->slots[RA(bytecode)] = NUMBER_VAL(AS_NUMBER(b) + AS_NUMBER(c));
				} else {
					runtimeError(vm, "Operands must be two numbers or two strings.");
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_SUBVV:	  BINARY_OPVV(NUMBER_VAL, -); break;
			case OP_MULVV:	  BINARY_OPVV(NUMBER_VAL, *); break;
			case OP_DIVVV:	  BINARY_OPVV(NUMBER_VAL, /); break;
			case OP_RETURN: {
				if(vm->frameCount == 1)
					return INTERPRET_OK;

				// uint16_t d = RD(bytecode);
				frame->slots[-1] = frame->slots[RA(bytecode)];
				closeUpvalues(vm, frame->slots);
				vm->frameCount--;

				frame = &vm->frames[vm->frameCount - 1];
				frame->slots = frame->slots;
				break;
			}
			case OP_JUMP:
OP_JUMP:
				assert(RJump(bytecode) != -1);
				frame->ip += RJump(bytecode);
				break;
			case OP_COPY_JUMP_IF_FALSE:
				frame->slots[RA(bytecode)] = frame->slots[RD(bytecode)];
				// intentional fallthrough
			case OP_JUMP_IF_FALSE:
				if(isFalsey(frame->slots[RD(bytecode)])) {
					bytecode = READ_BYTECODE();
					goto OP_JUMP;
				}
				frame->ip++;
				break;
			case OP_COPY_JUMP_IF_TRUE:
				frame->slots[RA(bytecode)] = frame->slots[RD(bytecode)];
				// intentional fallthrough
			case OP_JUMP_IF_TRUE:
				if(!isFalsey(frame->slots[RD(bytecode)])) {
					bytecode = READ_BYTECODE();
					goto OP_JUMP;
				}
				frame->ip++;
				break;
			case OP_MOV:
				frame->slots[RA(bytecode)] = frame->slots[RD(bytecode)];
				break;
			case OP_CALL:
				if(!callValue(vm, &frame->slots[RA(bytecode)], RB(bytecode), RC(bytecode)))
					return INTERPRET_RUNTIME_ERROR;
				frame = &vm->frames[vm->frameCount-1];
				break;
			case OP_GET_UPVAL: {
				frame->slots[RA(bytecode)] = *frame->c->upvalues[RD(bytecode)]->location;
				break;
			}
			case OP_SET_UPVAL: {
				*frame->c->upvalues[RD(bytecode)]->location = frame->slots[RA(bytecode)];
				break;
			}
			case OP_CLOSURE: {
				ObjFunction *f = AS_FUNCTION(frame->c->f->chunk.constants.values[RD(bytecode)]);
				ObjClosure *cl = newClosure(vm, f);
				frame->slots[RA(bytecode)] = OBJ_VAL(cl);	// Can be found by GC
				for(size_t i=0; i<f->uvCount; i++) {
					if((f->uv[i] & UV_IS_LOCAL) == UV_IS_LOCAL)
						cl->upvalues[i] = captureUpvalue(vm, frame->slots + (f->uv[i] & 0xff));
					else
						cl->upvalues[i] = frame->c->upvalues[f->uv[i] & 0xff];
				}
				break;
			}
			case OP_CLOSE_UPVALUES:
				closeUpvalues(vm, frame->slots + RA(bytecode));
				break;
			case OP_CLASS: {
				ObjClass *klass = newClass(vm, AS_STRING(frame->c->f->chunk.constants.values[RD(bytecode)]));
				frame->slots[RA(bytecode)] = OBJ_VAL(klass);
				break;
			}
			case OP_GET_PROPERTY: {
				Value *v = &frame->slots[RB(bytecode)];
				if(!IS_INSTANCE(*v)) {
					runtimeError(vm, "Only instances have properties.");
					return INTERPRET_RUNTIME_ERROR;
				}
				ObjInstance *instance = AS_INSTANCE(*v);
				ObjString *name = AS_STRING(frame->slots[RC(bytecode)]);
				if(tableGet(&instance->fields, name, &frame->slots[RA(bytecode)])) {
					break;
				} else if(bindMethod(vm, instance, name, &frame->slots[RA(bytecode)])) {
					break;
				}
				return INTERPRET_RUNTIME_ERROR;
			}
			case OP_SET_PROPERTY: {
				Value *v = &frame->slots[RB(bytecode)];
				if(!IS_INSTANCE(*v)) {
					runtimeError(vm, "Only instances have fields.");
					return INTERPRET_RUNTIME_ERROR;
				}
				ObjInstance *instance = AS_INSTANCE(*v);
				assert(IS_STRING(frame->slots[RC(bytecode)]));
				tableSet(vm, &instance->fields, AS_STRING(frame->slots[RC(bytecode)]), frame->slots[RA(bytecode)]);
				*v = frame->slots[RA(bytecode)];
				break;
			}
			case OP_METHOD:
				defineMethod(vm, frame->slots[RA(bytecode)], frame->slots[RB(bytecode)], frame->slots[RC(bytecode)]);
				break;
			default:
				fprintf(stderr, "Unimplemented opcode %d.\n", OP(bytecode));
				exit(1);
		}
	}
}
#undef BINARY_OPVV
#undef READ_BYTECODE

InterpretResult interpret(VM *vm, const char *source) {
	ObjFunction *script = parse(vm, source);
	if(script == NULL)
		return INTERPRET_COMPILE_ERROR;

	assert(vm->stackSize > 0);
	vm->stack[0] = OBJ_VAL(script);
	ObjClosure *cl = newClosure(vm, script);
	vm->stack[0] = OBJ_VAL(cl);
	call(vm, cl, vm->stack, 0, 1);

	return run(vm);
}
