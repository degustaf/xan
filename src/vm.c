#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "builtin.h"
#include "chunk.h"
#include "exception.h"
#include "memory.h"
#include "parse.h"
#include "sysmod.h"
#include "table.h"
#include "xanString.h"

#define CURRENT_CLOSURE (AS_CLOSURE(currentThread->base[-3]))
#define CURRENT_FUNCTION (CURRENT_CLOSURE->f)

#if defined(DEBUG_TRACE_EXECUTION) || defined(DEBUG_STACK_USAGE)
#include "debug.h"
#endif

static bool isFalsey(Value v) {
	return IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v))
					 || (IS_ARRAY(v) && AS_ARRAY(v)->count == 0)
					 || (IS_TABLE(v) && count(AS_TABLE(v)) == 0);
}

static Value concatenate(VM *vm, thread *currentThread, ObjString *b, ObjString *c) {
	size_t length = b->length + c->length;
	char *chars = ALLOCATE(vm, char, length+1);
	memcpy(chars, b->chars, b->length);
	memcpy(chars + b->length, c->chars, c->length);
	chars[length] = '\0';

	ObjString *result = takeString(vm, currentThread, chars, length);
	return OBJ_VAL(result);
}

#define runtimeError(vm, currentThread, ...) do { \
	ExceptionFormattedStr(vm, currentThread, __VA_ARGS__); \
} while(false)

void growStack(VM *vm, thread *currentThread, size_t space_needed) {
	// pointers into stack: stackTop, stackLast, openUpvalues, base
	size_t stackTopIndex = currentThread->stackTop - currentThread->stack;

	Value *oldStack = currentThread->stack;
	size_t oldStackSize = currentThread->stackLast - currentThread->stack + 1;
	size_t currentStackSize = currentThread->stackLast - currentThread->stack + 1;
	while(currentStackSize < space_needed)
		currentStackSize = GROW_CAPACITY(currentStackSize);
	currentThread->stack = GROW_ARRAY(vm, currentThread->stack, Value, oldStackSize, currentStackSize);
	currentThread->stackTop = currentThread->stack + stackTopIndex;
	currentThread->stackLast = currentThread->stack + currentStackSize - 1;
	currentThread->base += currentThread->stack - oldStack;
	for(ObjUpvalue **uv = &currentThread->openUpvalues; *uv; uv = &(*uv)->next)
		(*uv)->location += currentThread->stack - oldStack;
}

static void incFrame(VM *vm, thread *currentThread, Reg stackUsed, size_t shift, ObjClosure *function, uint32_t *ip) {
	if(currentThread->base + shift + stackUsed + 2 > currentThread->stackLast) {
		size_t base_index = currentThread->base + shift + 1 - currentThread->stack;
		growStack(vm, currentThread, base_index + stackUsed + 2);
	}
	if(currentThread->base + shift + stackUsed + 2 > currentThread->stackTop)
		currentThread->stackTop = currentThread->base + shift + stackUsed + 2;

	assert(function);
	assert(ip);
	currentThread->base += shift + 1;
	currentThread->base[-2] = IP_VAL((intptr_t)ip);
	currentThread->base[-3] = OBJ_VAL(function);
}

uint32_t* decFrame(thread *currentThread) {
	assert(!IS_NIL(currentThread->base[-3]));
	uint32_t *ip = (uint32_t*)(AS_IP(currentThread->base[-2]));
	uint32_t bytecode = *(ip - 1);
	Reg ra = RA(bytecode) + 1;
	currentThread->base -= ra + 2;
	return ip;
}

void initVM(VM *vm, int argc, char** argv, int start) {
	// Initialize VM without calling allocator.

	vm->strings = NULL;
	vm->globals = NULL;
	vm->builtinMods = NULL;
	vm->initString = NULL;
	vm->newString = NULL;

	GarbageCollector *gc = &vm->gc;
	gc->objects = NULL;
	gc->grayStack = NULL;
	gc->bytesAllocated = 0;
	gc->nextMinorGC = 256 * 1024;
	gc->nextMajorGC = 1024 * 1024;
	gc->grayCount = 0;
	gc->grayCapacity = 0;
	gc->nextGCisMajor = false;

	vm->baseThread = ALLOCATE_OBJ(vm, thread, OBJ_THREAD);

	vm->baseThread->tryCount = 0;
	vm->baseThread->stack = NULL;
	vm->baseThread->stackTop = vm->baseThread->stack + 1;
	vm->baseThread->stackLast = NULL;
	vm->baseThread->base = 0;
	vm->baseThread->exception = NIL_VAL;
	vm->baseThread->openUpvalues = NULL;
	vm->baseThread->currentCompiler = NULL;
	vm->baseThread->currentClassCompiler = NULL;
	vm->baseThread->stack = GROW_ARRAY(vm, NULL, Value, 0, BASE_STACK_SIZE);
	vm->baseThread->stackLast = vm->baseThread->stack + BASE_STACK_SIZE - 1;
	for(size_t i = 0; i < BASE_STACK_SIZE; i++)
		vm->baseThread->stack[i] = NIL_VAL;
	vm->baseThread->base = vm->baseThread->stack;
	vm->baseThread->stackTop = vm->baseThread->base;

	incCFrame(vm, vm->baseThread, 2, 3);
	vm->strings = newTable(vm, vm->baseThread, 0);
	vm->builtinMods = newTable(vm, vm->baseThread, 0);
	vm->initString = copyString(vm, vm->baseThread, "init", 4);
	vm->newString = copyString(vm, vm->baseThread, "new", 3);

	ObjModule *builtinM = defineNativeModule(vm, vm->baseThread, &builtinDef);
	vm->globals = newTable(vm, vm->baseThread, 0);
	tableSet(vm, vm->globals, OBJ_VAL(copyString(vm, vm->baseThread, "_G", 2)), OBJ_VAL(vm->globals));
	tableAddAll(vm, builtinM->fields, vm->globals);
	tableSet(vm, vm->builtinMods, OBJ_VAL(builtinM->name), OBJ_VAL(builtinM));

	ObjModule *SysM = defineNativeModule(vm, vm->baseThread, &SysDef);
	tableSet(vm, vm->builtinMods, OBJ_VAL(SysM->name), OBJ_VAL(SysM));
	SysInit(vm, vm->baseThread, SysM, argc, argv, start);

	decCFrame(vm->baseThread);
	assert(vm->baseThread->base == vm->baseThread->stack);
}

void freeVM(VM *vm) {
	vm->baseThread = NULL;
	vm->strings = NULL;
	vm->globals = NULL;
	vm->initString = NULL;
	vm->newString = NULL;
	freeObjects(&vm->gc);
	if(vm->gc.bytesAllocated > 0)
		fprintf(stderr, "Memory manager lost %zu bytes.\n", vm->gc.bytesAllocated);
}

uint32_t* call(VM *vm, thread *currentThread, ObjClosure *function, Reg calleeReg, Reg argCount, uint32_t *ip) {
	if(argCount < function->f->minArity) {
		runtimeError(vm, currentThread, "Expected at least %d arguments but got %d.", function->f->minArity, argCount);
		return NULL;
	}
	if(argCount > function->f->maxArity) {
		runtimeError(vm, currentThread, "Expected at most %d arguments but got %d.", function->f->maxArity, argCount);
		return false;
	}

	incFrame(vm, currentThread, function->f->stackUsed, calleeReg + 2, function, ip);

	size_t codeOffset = argCount - function->f->minArity;
	return function->f->chunk.code + function->f->code_offsets[codeOffset];
}

static uint32_t* callValue(VM *vm, thread *currentThread, Reg calleeReg, Reg argCount, uint32_t *ip) {
	Value callee = currentThread->base[calleeReg];
	if(IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
			case OBJ_BOUND_METHOD: {
				ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
				currentThread->base[calleeReg + 2] = bound->receiver;
				if(bound->method->type == OBJ_CLOSURE)
					return call(vm, currentThread, (ObjClosure*)bound->method, calleeReg, argCount, ip);
				assert(bound->method->type == OBJ_NATIVE);
				NativeFn native = ((ObjNative*)bound->method)->function;
				assert(false);	// Do we have a test that reaches this point?
				incCFrame(vm, currentThread, argCount, calleeReg);
				bool ret = native(vm, currentThread, argCount);
				decCFrame(currentThread);
				return ret ? ip : NULL;
			}
			case OBJ_CLASS: {
				ObjClass *klass = AS_CLASS(callee);
				Value initializer;
				assert(klass->methods);
				if(tableGet(klass->methods, OBJ_VAL(vm->initString), &initializer)) {
					if(IS_NATIVE(initializer)) {
						callee = initializer;
						goto native;
					}
					assert(IS_CLOSURE(initializer));
#ifndef NDEBUG
					ptrdiff_t v = currentThread->base - currentThread->stack;
#endif
					incCFrame(vm, currentThread, 1, AS_CLOSURE(currentThread->base[-3])->f->stackUsed + 3);	// don't overwrite arguments.
					ObjInstance *ret = newInstance(vm, currentThread, klass);
					decCFrame(currentThread);
					assert(currentThread->stack + v == currentThread->base);
					currentThread->base[calleeReg + 2] = OBJ_VAL(ret);	// currentThread->base[-1] is this after frame is incremented.
					return call(vm, currentThread, AS_CLOSURE(initializer), calleeReg, argCount, ip);
				}
				if(argCount) {
					runtimeError(vm, currentThread, "Expected 0 arguments but got %d.", argCount);
					return NULL;
				}
#ifndef NDEBUG
				ptrdiff_t v = currentThread->base - currentThread->stack;
#endif
				assert(argCount == 0);
				incCFrame(vm, currentThread, 1, calleeReg + 2);
				ObjInstance *ret = newInstance(vm, currentThread, klass);
				decCFrame(currentThread);
				assert(currentThread->stack + v == currentThread->base);
				currentThread->base[calleeReg] = OBJ_VAL(ret);
				return ip;
			}
			case OBJ_CLOSURE:
				return call(vm, currentThread, AS_CLOSURE(callee), calleeReg, argCount, ip);
native:
			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(callee);
				incCFrame(vm, currentThread, argCount, calleeReg + 2);
				bool ret = native(vm, currentThread, argCount);
				decCFrame(currentThread);
				currentThread->base[calleeReg] = currentThread->base[calleeReg+3];
				return ret ? ip : NULL;
			}
			default:
				break;
		}
	}

	runtimeError(vm, currentThread, "Can only call functions and classes.");
	return NULL;
}

static bool bindMethod(VM *vm, thread *currentThread, ObjInstance *instance, ObjClass *klass, Value name, Reg retReg) {
	Value method;
	assert(instance);
	assert(klass->methods);
	if(!tableGet(klass->methods, name, &method)) {
		if(instance->klass == &stringDef) {
			runtimeError(vm, currentThread, "Only instances have properties.");
			return false;
		}
		assert(IS_STRING(name));
		runtimeError(vm, currentThread, "Undefined property '%s'.", AS_STRING(name)->chars);
		return false;
	}
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));

	ObjBoundMethod *bound = newBoundMethod(vm, OBJ_VAL(instance), method);
	currentThread->base[retReg] = (OBJ_VAL(bound));
	return true;
}

static uint32_t* invokeMethod(VM *vm, thread *currentThread, int16_t instanceReg, ObjString *name, Reg argCount, uint32_t *ip) {
	Value inst = currentThread->base[instanceReg];
	Value method;
	if(!HAS_PROPERTIES(inst)) {
		runtimeError(vm, currentThread, "Only instances have properties.");
		return NULL;
	}
	ObjInstance *instance = AS_INSTANCE(inst);
	assert(instance->klass->methods);
	if((!(IS_ARRAY(inst) || IS_STRING(inst))) && (tableGet(instance->fields, OBJ_VAL(name), &method))) {
		currentThread->base[instanceReg] = method;
		return callValue(vm, currentThread, instanceReg, argCount, ip);
	}
	if(!tableGet(instance->klass->methods, OBJ_VAL(name), &method)) {
		runtimeError(vm, currentThread, "Undefined property '%s'.", name->chars);
		return NULL;
	}
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));

	currentThread->base[instanceReg + 2] = OBJ_VAL(instance);
	currentThread->base[instanceReg] = method;
	if(AS_OBJ(method)->type == OBJ_CLOSURE)
		return call(vm, currentThread, AS_CLOSURE(method), instanceReg, argCount, ip);
	assert(AS_OBJ(method)->type == OBJ_NATIVE);
	NativeFn native = AS_NATIVE(method);
	incCFrame(vm, currentThread, argCount, instanceReg + 2);
#ifdef DEBUG_STACK_USAGE
		dumpStack(vm, 25);
		printf("\n");
#endif /* DEBUG_STACK_USAGE */
	bool ret = native(vm, currentThread, argCount);
	decCFrame(currentThread);
	currentThread->base[instanceReg] = currentThread->base[instanceReg+3];
	return ret ? ip : NULL;
}

static ObjUpvalue *captureUpvalue(VM *vm, thread *currentThread, Value *local) {
	ObjUpvalue **uv = &currentThread->openUpvalues;

	while(*uv && (*uv)->location > local)
		uv = &(*uv)->next;

	if(*uv && ((*uv)->location == local))
		return *uv;

	ObjUpvalue *ret = newUpvalue(vm, local);
	ret->next = *uv;
	*uv = ret;
	return ret;
}

static void closeUpvalues(thread *currentThread, Value *last) {
	while(currentThread->openUpvalues && currentThread->openUpvalues->location >= last) {
		ObjUpvalue *uv = currentThread->openUpvalues;
		uv->closed = *uv->location;
		uv->location = &uv->closed;
		currentThread->openUpvalues = uv->next;
	}
}

static void defineMethod(VM *vm, Value ra, Value rb, Value rc) {
	assert(IS_CLASS(ra));
	assert(IS_STRING(rb));
	assert(IS_CLOSURE(rc));
	ObjClass *klass = AS_CLASS(ra);
	ObjString *name = AS_STRING(rb);
	assert(klass->methods);
	tableSet(vm, klass->methods, OBJ_VAL(name), rc);
}

#ifdef COMPUTED_GOTO
	#define SWITCH DISPATCH;
	#define DISPATCH \
		assert(IS_OBJ(currentThread->base[-3]) && (OBJ_TYPE(currentThread->base[-3]) == OBJ_CLOSURE)); \
		bytecode = READ_BYTECODE(); \
		goto *opcodes[OP(bytecode)]
	#define TARGET(op) TARGET_##op
	#define DEFAULT
#else
	#define SWITCH \
		assert(IS_OBJ(currentThread->base[-3]) && (OBJ_TYPE(currentThread->base[-3]) == OBJ_CLOSURE)); \
		bytecode = READ_BYTECODE(); \
		switch(OP(bytecode))
	#define DISPATCH continue
	#define TARGET(op) case op
	#define DEFAULT \
		default: \
		fprintf(stderr, "Unimplemented opcode %d.\n", OP(bytecode)); \
		return INTERPRET_RUNTIME_ERROR;
#endif
#define READ_BYTECODE() (*ip++)
#define BINARY_OPVV(valueType, op) \
	do { \
		Value b = currentThread->base[RB(bytecode)]; \
		Value c = currentThread->base[RC(bytecode)]; \
		if(IS_NUMBER(b) && IS_NUMBER(c)) { \
			currentThread->base[RA(bytecode)] = valueType(AS_NUMBER(b) op AS_NUMBER(c)); \
		} else { \
			runtimeError(vm, currentThread, "Operands must be numbers."); \
			goto exception_unwind; \
		} \
	} while(false)
#define BINARY_OPVK(valueType, op) \
	do { \
		Value b = currentThread->base[RB(bytecode)]; \
		Value c = CURRENT_FUNCTION->chunk.constants->values[RC(bytecode)]; \
		if(!IS_NUMBER(b) || !IS_NUMBER(c)) { \
			runtimeError(vm, currentThread, "Operands must be numbers."); \
			goto exception_unwind; \
		} \
		currentThread->base[RA(bytecode)] = valueType(AS_NUMBER(b) op AS_NUMBER(c)); \
	} while(false)
#define READ_STRING() AS_STRING(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)])
InterpretResult run(VM *vm, thread *currentThread) {
#ifdef COMPUTED_GOTO
	#define BUILD_GOTOS(op, _) &&TARGET_##op
	static void* opcodes[] = {
		OPCODE_BUILDER(BUILD_GOTOS, COMMA)
	};
#endif /* COMPUTED_GOTO */
	register uint32_t *ip = CURRENT_FUNCTION->chunk.code;
	while(true) {
#ifdef DEBUG_STACK_USAGE
		dumpStack(vm, 25);
#endif /* DEBUG_STACK_USAGE */
#ifdef DEBUG_UPVALUE_USAGE
		dumpOpenUpvalues(vm);
		dumpClosedUpvalues(AS_CLOSURE(vm->base[-3]));
#endif /* DEBUG_UPVALUE_USAGE */
#ifdef DEBUG_TRACE_EXECUTION
		disassembleInstruction(&CURRENT_FUNCTION->chunk, ip - CURRENT_FUNCTION->chunk.code);
#endif
#ifdef DEBUG_STACK_USAGE
		printf("\n");
#endif /* DEBUG_STACK_USAGE */
		uint32_t bytecode;
		SWITCH
		{
			TARGET(OP_CONST_NUM):
				currentThread->base[RA(bytecode)] = CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)];
				DISPATCH;
			TARGET(OP_PRIMITIVE):
				currentThread->base[RA(bytecode)] = getPrimitive(RD(bytecode)); DISPATCH;
			TARGET(OP_NEGATE): {
				Value vRD = currentThread->base[RD(bytecode)];
				if(!IS_NUMBER(vRD)) {
					runtimeError(vm, currentThread, "Operand must be a number.");
					goto exception_unwind;
				}
				currentThread->base[RA(bytecode)] = NUMBER_VAL(-AS_NUMBER(vRD));
				DISPATCH;
			}
			TARGET(OP_NOT): {
				Value vRD = currentThread->base[RD(bytecode)];
				currentThread->base[RA(bytecode)] = BOOL_VAL(isFalsey(vRD));
				DISPATCH;
			}
			TARGET(OP_GET_GLOBAL): {
				ObjString *name = READ_STRING();
				Value value;
				if(!tableGet(vm->globals, OBJ_VAL(name), &value)) {
					runtimeError(vm, currentThread, "Undefined variable '%s'.", name->chars);
					goto exception_unwind;
				}
				currentThread->base[RA(bytecode)] = value;
				DISPATCH;
			}
			TARGET(OP_DEFINE_GLOBAL): {
				ObjString *name = READ_STRING();
				tableSet(vm, vm->globals, OBJ_VAL(name), currentThread->base[RA(bytecode)]);
				DISPATCH;
			}
			TARGET(OP_SET_GLOBAL): {
				ObjString *name = READ_STRING();
				if(tableSet(vm, vm->globals, OBJ_VAL(name), currentThread->base[RA(bytecode)])) {
					runtimeError(vm, currentThread, "Undefined variable '%s'.", name->chars);
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_EQUAL): {
				Value b = currentThread->base[RB(bytecode)];
				Value c = currentThread->base[RC(bytecode)];
				currentThread->base[RA(bytecode)] = BOOL_VAL(valuesEqual(b, c));
				DISPATCH;
			}
			TARGET(OP_NEQ): {
				Value b = currentThread->base[RB(bytecode)];
				Value c = currentThread->base[RC(bytecode)];
				currentThread->base[RA(bytecode)] = BOOL_VAL(!valuesEqual(b, c));
				DISPATCH;
			}
			TARGET(OP_GREATER):  BINARY_OPVV(BOOL_VAL, >); DISPATCH;
			TARGET(OP_GEQ):      BINARY_OPVV(BOOL_VAL, >=); DISPATCH;
			TARGET(OP_LESS):     BINARY_OPVV(BOOL_VAL, <); DISPATCH;
			TARGET(OP_LEQ):      BINARY_OPVV(BOOL_VAL, <=); DISPATCH;
			TARGET(OP_ADDVV): {
				Value b = currentThread->base[RB(bytecode)];
				Value c = currentThread->base[RC(bytecode)];
				if(IS_STRING(b) && IS_STRING(c)) {
					incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed);
					Value ret = concatenate(vm, currentThread, AS_STRING(b), AS_STRING(c));
					decCFrame(currentThread);
					currentThread->base[RA(bytecode)] = ret;
				} else if(IS_NUMBER(b) && IS_NUMBER(c)) {
					currentThread->base[RA(bytecode)] = NUMBER_VAL(AS_NUMBER(b) + AS_NUMBER(c));
				} else {
					runtimeError(vm, currentThread, "Operands must be two numbers or two strings.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_SUBVV):	  BINARY_OPVV(NUMBER_VAL, -); DISPATCH;
			TARGET(OP_MULVV):	  BINARY_OPVV(NUMBER_VAL, *); DISPATCH;
			TARGET(OP_DIVVV):	  BINARY_OPVV(NUMBER_VAL, /); DISPATCH;
			TARGET(OP_MODVV): {
				Value b = currentThread->base[RB(bytecode)];
				Value c = currentThread->base[RC(bytecode)];
				if(!IS_NUMBER(b) || !IS_NUMBER(c)) {
					runtimeError(vm, currentThread, "Operands must be numbers.");
					goto exception_unwind;
				}
				currentThread->base[RA(bytecode)] = NUMBER_VAL(fmod(AS_NUMBER(b), AS_NUMBER(c)));
				DISPATCH;
			}
			TARGET(OP_ADDVK): {
				Value b = currentThread->base[RB(bytecode)];
				Value c = CURRENT_FUNCTION->chunk.constants->values[RC(bytecode)]; \
				if(IS_STRING(b) && IS_STRING(c)) {
					incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed);
					Value ret = concatenate(vm, currentThread, AS_STRING(b), AS_STRING(c));
					decCFrame(currentThread);
					currentThread->base[RA(bytecode)] = ret;
				} else if(IS_NUMBER(b) && IS_NUMBER(c)) {
					currentThread->base[RA(bytecode)] = NUMBER_VAL(AS_NUMBER(b) + AS_NUMBER(c));
				} else {
					runtimeError(vm, currentThread, "Operands must be two numbers or two strings.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_SUBVK):	  BINARY_OPVK(NUMBER_VAL, -); DISPATCH;
			TARGET(OP_MULVK):	  BINARY_OPVK(NUMBER_VAL, *); DISPATCH;
			TARGET(OP_DIVVK):	  BINARY_OPVK(NUMBER_VAL, /); DISPATCH;
			TARGET(OP_MODVK): {
				Value b = currentThread->base[RB(bytecode)];
				Value c = CURRENT_FUNCTION->chunk.constants->values[RC(bytecode)]; \
				if(!IS_NUMBER(b) || !IS_NUMBER(c)) {
					runtimeError(vm, currentThread, "Operands must be numbers.");
					goto exception_unwind;
				}
				currentThread->base[RA(bytecode)] = NUMBER_VAL(fmod(AS_NUMBER(b), AS_NUMBER(c)));
				DISPATCH;
			}
			TARGET(OP_RETURN): {
				if(currentThread->base == currentThread->stack + 3)
					return INTERPRET_OK;

				uint16_t count = RD(bytecode) - 1;
				int16_t ra = ((int16_t)(Reg)(RA(bytecode) + 1))-1;
				Value *oldBase = currentThread->base;
				ip = decFrame(currentThread);
				assert((OP(*(ip-1)) == OP_CALL) || (OP(*(ip-1)) == OP_INVOKE));
				// __attribute__((unused)) uint16_t nReturn = RB(*(ip - 1));
				closeUpvalues(currentThread, oldBase - 1);
				// ensure we close this in oldBase[-1] as an upvalue before moving return value.
				for(size_t i = 0; i < count; i++) {
					oldBase[-3 + i] = oldBase[ra + i];
				}
				DISPATCH;
			}
			TARGET(OP_JUMP):
OP_JUMP:
				assert(RJump(bytecode) != -1);
				ip += RJump(bytecode);
				DISPATCH;
			TARGET(OP_COPY_JUMP_IF_FALSE):
				currentThread->base[RA(bytecode)] = currentThread->base[RD(bytecode)];
				// intentional fallthrough
			TARGET(OP_JUMP_IF_FALSE):
				if(isFalsey(currentThread->base[RD(bytecode)])) {
					bytecode = READ_BYTECODE();
					assert(OP(bytecode) == OP_JUMP);
					goto OP_JUMP;
				}
				ip++;
				DISPATCH;
			TARGET(OP_COPY_JUMP_IF_TRUE):
				currentThread->base[RA(bytecode)] = currentThread->base[RD(bytecode)];
				// intentional fallthrough
			TARGET(OP_JUMP_IF_TRUE):
				if(!isFalsey(currentThread->base[RD(bytecode)])) {
					bytecode = READ_BYTECODE();
					assert(OP(bytecode) == OP_JUMP);
					goto OP_JUMP;
				}
				ip++;
				DISPATCH;
			TARGET(OP_MOV):
				currentThread->base[RA(bytecode)] = currentThread->base[(int16_t)RD(bytecode)];
				DISPATCH;
			TARGET(OP_CALL): {	// RA = func/dest reg; RB = retCount(used by OP_CALL when call returns); RC = argCount
				uint32_t *new_ip;
				if((new_ip = callValue(vm, currentThread, RA(bytecode), RC(bytecode), ip)) == NULL)
					goto exception_unwind;
				ip = new_ip;
				DISPATCH;
			}
			TARGET(OP_GET_UPVAL): {
				currentThread->base[RA(bytecode)] = *AS_CLOSURE(currentThread->base[-3])->upvalues[RD(bytecode)]->location;
				DISPATCH;
			}
			TARGET(OP_SET_UPVAL): {
				*AS_CLOSURE(currentThread->base[-3])->upvalues[RA(bytecode)]->location = currentThread->base[RD(bytecode)];
				DISPATCH;
			}
			TARGET(OP_CLOSURE): {
				ObjFunction *f = AS_FUNCTION(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				ObjClosure *cl = newClosure(vm, f);
				currentThread->base[RA(bytecode)] = OBJ_VAL(cl);	// Can be found by GC
				for(size_t i=0; i<f->uvCount; i++) {
					if((f->uv[i] & UV_IS_LOCAL) == UV_IS_LOCAL)
						cl->upvalues[i] = captureUpvalue(vm, currentThread, currentThread->base + (f->uv[i] & 0xff) - 1);
					else
						cl->upvalues[i] = AS_CLOSURE(currentThread->base[-3])->upvalues[(f->uv[i] & 0xff)-1];
					writeBarrier(vm, cl);
				}
				DISPATCH;
			}
			TARGET(OP_CLOSE_UPVALUES):
				closeUpvalues(currentThread, currentThread->base + RA(bytecode));
				DISPATCH;
			TARGET(OP_CLASS): {
				ObjString *name = READ_STRING();
				incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjClass *klass = newClass(vm, currentThread, name);
				decCFrame(currentThread);
				currentThread->base[RA(bytecode)] = OBJ_VAL(klass);
				DISPATCH;
			}
			TARGET(OP_GET_PROPERTY): {	// RA = dest reg; RB = object reg; RC = property reg
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = currentThread->base[rb];
				if(HAS_PROPERTIES(v)) {
					ObjInstance *instance = AS_INSTANCE(v);
					Value name = currentThread->base[RC(bytecode)];
					assert(IS_STRING(name));
					if((!(IS_ARRAY(v) || IS_STRING(v))) && (tableGet(instance->fields, name, &currentThread->base[RA(bytecode)]))) {
						DISPATCH;
					} else if(bindMethod(vm, currentThread, instance, instance->klass, name, RA(bytecode))) {
						DISPATCH;
					}
					goto exception_unwind;
				} else {
					runtimeError(vm, currentThread, "Only instances have properties.");
					goto exception_unwind;
				}
			}
			TARGET(OP_SET_PROPERTY): {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = currentThread->base[rb];
				if(HAS_PROPERTIES(v)) {
					ObjInstance *instance = AS_INSTANCE(v);
					Value name = currentThread->base[RC(bytecode)];
					assert(IS_STRING(name));
					if(!(IS_ARRAY(v) || IS_STRING(v))) {
						tableSet(vm, instance->fields, name, currentThread->base[RA(bytecode)]);
						DISPATCH;
					}
				}
				runtimeError(vm, currentThread, "Only instances have fields.");
				goto exception_unwind;
			}
			TARGET(OP_GET_PROPERTYK): {	// RA = dest reg; RB = object reg; RC = property in Constants
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = currentThread->base[rb];
				if(HAS_PROPERTIES(v)) {
					ObjInstance *instance = AS_INSTANCE(v);
					Value name = CURRENT_FUNCTION->chunk.constants->values[RC(bytecode)];
					assert(IS_STRING(name));
					if((!(IS_ARRAY(v) || IS_STRING(v))) && (tableGet(instance->fields, name, &currentThread->base[RA(bytecode)]))) {
						DISPATCH;
					} else if(bindMethod(vm, currentThread, instance, instance->klass, name, RA(bytecode))) {
						DISPATCH;
					}
					goto exception_unwind;
				} else {
					runtimeError(vm, currentThread, "Only instances have properties.");
					goto exception_unwind;
				}
			}
			TARGET(OP_SET_PROPERTYK): {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = currentThread->base[rb];
				if(HAS_PROPERTIES(v)) {
					ObjInstance *instance = AS_INSTANCE(v);
					Value name = CURRENT_FUNCTION->chunk.constants->values[RC(bytecode)];
					assert(IS_STRING(name));
					if(!(IS_ARRAY(v) || IS_STRING(v))) {
						tableSet(vm, instance->fields, name, currentThread->base[RA(bytecode)]);
						DISPATCH;
					}
				}
				runtimeError(vm, currentThread, "Only instances have fields.");
				goto exception_unwind;
			}
			TARGET(OP_INVOKE): {	// RA = object/dest reg; RA + 1 = property reg; RB = retCount; RC = argCount
				int16_t ra = ((int16_t)(Reg)(RA(bytecode) + 1))-1;
				ObjString *name = AS_STRING(currentThread->base[ra+1]);
				uint32_t *new_ip;
				if((new_ip = invokeMethod(vm, currentThread, ra, name, RC(bytecode), ip)) == NULL) {
					goto exception_unwind;
				}
				ip = new_ip;
				DISPATCH;
			}
			TARGET(OP_METHOD):
				defineMethod(vm, currentThread->base[RA(bytecode)], currentThread->base[RB(bytecode)], currentThread->base[RC(bytecode)]);
				DISPATCH;
			TARGET(OP_INHERIT): {
				Value superclass = currentThread->base[RD(bytecode)];
				if(!IS_CLASS(superclass)) {
					runtimeError(vm, currentThread, "Superclass must be a class.");
					goto exception_unwind;
				}
				ObjClass *subclass = AS_CLASS(currentThread->base[RA(bytecode)]);
				assert(subclass->methods);
				assert(AS_CLASS(superclass)->methods);
				tableAddAll(vm, AS_CLASS(superclass)->methods, subclass->methods);
				DISPATCH;
			}
			TARGET(OP_GET_SUPER): {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				ObjClass *superclass = AS_CLASS(currentThread->base[RA(bytecode)]);
				ObjInstance *instance = AS_INSTANCE(currentThread->base[rb]);
				Value name = currentThread->base[RC(bytecode)];
				if(!bindMethod(vm, currentThread, instance, superclass, name, RA(bytecode)))
					goto exception_unwind;
				DISPATCH;
			}
			TARGET(OP_NEW_ARRAY):
				incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed + 1);
				Value ret = OBJ_VAL(newArray(vm, currentThread, RD(bytecode)));
				decCFrame(currentThread);
				currentThread->base[RA(bytecode)] = ret;
				DISPATCH;
			TARGET(OP_DUPLICATE_ARRAY): {
				ObjArray *src = AS_ARRAY(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjArray *t = duplicateArray(vm, currentThread, src);
				decCFrame(currentThread);
				currentThread->base[RA(bytecode)] = OBJ_VAL(t);
				DISPATCH;
			}
			TARGET(OP_NEW_TABLE):
				incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjTable *t = newTable(vm, currentThread, RD(bytecode));
				decCFrame(currentThread);
				currentThread->base[RA(bytecode)] = OBJ_VAL(t);
				DISPATCH;
			TARGET(OP_DUPLICATE_TABLE): {
				ObjTable *src = AS_TABLE(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjTable *t = duplicateTable(vm, currentThread, src);
				decCFrame(currentThread);
				currentThread->base[RA(bytecode)] = OBJ_VAL(t);
				DISPATCH;
			}
			TARGET(OP_GET_SUBSCRIPT): {
				Value v = currentThread->base[RB(bytecode)];
				if(IS_ARRAY(v)) {
					ObjArray *a = AS_ARRAY(v);
					v = currentThread->base[RC(bytecode)];
					if(!IS_NUMBER(v)) {
						runtimeError(vm, currentThread, "Arrays can only be subscripted by numbers.");
						goto exception_unwind;
					}
					double n = AS_NUMBER(v);
					if(n != (int)n) {
						runtimeError(vm, currentThread, "Subscript must be an integer.");
						goto exception_unwind;
					}
					if(getArray(a, (int)n, &currentThread->base[RA(bytecode)])) {
						runtimeError(vm, currentThread, "Subscript out of bounds.");
						goto exception_unwind;
					}
				} else if(IS_TABLE(v)) {
					ObjTable *t = AS_TABLE(v);
					v = currentThread->base[RC(bytecode)];
					if(!(IS_STRING(v) || IS_NUMBER(v))) {
						runtimeError(vm, currentThread, "Tables can only be subscripted by strings or numbers.");
						goto exception_unwind;
					}
					if(!tableGet(t, v, &currentThread->base[RA(bytecode)])) {
						runtimeError(vm, currentThread, "Subscript out of bounds.");
						goto exception_unwind;
					}
				} else if(IS_STRING(v)) {
					ObjString *s = AS_STRING(v);
					v = currentThread->base[RC(bytecode)];
					if((!IS_NUMBER(v)) || (AS_NUMBER(v) != (double)(int)AS_NUMBER(v))) {
						runtimeError(vm, currentThread, "Strings can only be subscripted by integers.");
						goto exception_unwind;
					}
					int i = (int)AS_NUMBER(v);
					if((i < 0) || ((unsigned int)i >= s->length)) {
						runtimeError(vm, currentThread, "Subscript out of range.");
						goto exception_unwind;
					}
					incCFrame(vm, currentThread, 1, CURRENT_FUNCTION->stackUsed + 1);
					ObjString *ret = copyString(vm, currentThread, s->chars + i, 1);
					decCFrame(currentThread);
					currentThread->base[RA(bytecode)] = OBJ_VAL(ret);
				} else {
					runtimeError(vm, currentThread, "Only arrays, tables, and strings can be subscripted.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_SET_SUBSCRIPT): {
				Value v = currentThread->base[RB(bytecode)];
				if(IS_ARRAY(v)) {
					ObjArray *a = AS_ARRAY(v);
					v = currentThread->base[RC(bytecode)];
					if(!IS_NUMBER(v)) {
						runtimeError(vm, currentThread, "Arrays can only be subscripted by numbers.");
						goto exception_unwind;
					}
					double n = AS_NUMBER(v);
					if(n != (int)n) {
						runtimeError(vm, currentThread, "Subscript must be an integer.");
						goto exception_unwind;
					}
					setArray(vm, a, (int)n, currentThread->base[RA(bytecode)]);
				} else if(IS_TABLE(v)) {
					ObjTable *t = AS_TABLE(v);
					v = currentThread->base[RC(bytecode)];
					if(!(IS_STRING(v) || IS_NUMBER(v))) {
						runtimeError(vm, currentThread, "Tables can only be subscripted by strings or numbers.");
						goto exception_unwind;
					}
					tableSet(vm, t, v, currentThread->base[RA(bytecode)]);
				} else {
					runtimeError(vm, currentThread, "Only arrays can be subscripted.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_BEGIN_TRY):
				currentThread->_try[currentThread->tryCount].ip = (ip + RJump(bytecode)) - CURRENT_FUNCTION->chunk.code;
				currentThread->_try[currentThread->tryCount].exception = RA(bytecode);
				currentThread->tryCount++;
				DISPATCH;
			TARGET(OP_END_TRY):
				ip += RJump(bytecode);
				assert(currentThread->tryCount > 0);
				currentThread->tryCount--;
				DISPATCH;
			TARGET(OP_JUMP_IF_NOT_EXC): {
				Value v = currentThread->base[RA(bytecode)];
				Value exception = currentThread->exception;
				if(IS_CLASS(v) && IS_EXCEPTION(exception) && AS_EXCEPTION(exception)->klass == AS_CLASS(v))
					ip += RJump(bytecode);
				DISPATCH;
			}
			DEFAULT
			TARGET(OP_THROW): {
				Value err = currentThread->base[RA(bytecode)];
				if(IS_OBJ(err) && (IS_EXCEPTION(err) || (IS_INSTANCE(err) && AS_INSTANCE(err)->klass->isException))) {
					currentThread->exception = err;
				} else {
					runtimeError(vm, currentThread, "Only exceptions can be thrown.");
				}
			}
exception_unwind: {
				if(currentThread->tryCount > 0) {
					currentThread->tryCount--;
					ip = CURRENT_FUNCTION->chunk.code + currentThread->_try[currentThread->tryCount].ip;
					currentThread->base[currentThread->_try[currentThread->tryCount].exception] = currentThread->exception;
					DISPATCH;
				} else {
					ObjException *err = AS_EXCEPTION(currentThread->exception);
					fprintValue(stderr, err->msg);
					fputs("\n", stderr);
					for(Value *base = currentThread->stack + err->topBase;
							base > currentThread->stack;
							) {

						if(IS_CLOSURE(base[-3])) {	// In a xan frame.
							ObjFunction *f = AS_CLOSURE(base[-3])->f;
							size_t instruction = ip - f->chunk.code - 1;	// We have already advanced ip.
							fprintf(stderr, "[line %zu] in ", f->chunk.lines[instruction]);
							if(f->name == NULL) {
								fprintf(stderr, "script\n");
							} else {
								fprintf(stderr, "%s()\n", f->name->chars);
							}
							base -= RA(*((uint32_t*)(AS_IP(base[-2]))- 1)) + 3;
						} else {		// In a c frame.
							fprintf(stderr, "in a c function\n");
							base -= AS_IP(base[-2]);
						}
					}
					return INTERPRET_RUNTIME_ERROR;
				}
			}
		}
		continue;
	}
}
#undef BINARY_OPVV
#undef BINARY_OPVK
#undef READ_BYTECODE
#undef TARGET
#undef DISPATCH
#undef SWITCH

InterpretResult interpret(VM *vm, const char *source, bool printCode) {
	thread *currentThread = vm->baseThread;
	assert(currentThread->base == currentThread->stack);
	incCFrame(vm, currentThread, 3, 3);
	// The parser needs 3 stack slots to stash values to prevent premature freeing.
	ObjFunction *script = parse(vm, currentThread, source, printCode);
	decCFrame(currentThread);
	assert(currentThread->base == currentThread->stack);
#ifndef DEBUG_PRINT_CODE
	if(printCode)
		return INTERPRET_OK;
#endif
	if(script == NULL)
		return INTERPRET_COMPILE_ERROR;

	assert(currentThread->stackLast + 1 > currentThread->stack);
	currentThread->base[0] = OBJ_VAL(script);
	ObjClosure *cl = newClosure(vm, script);
	currentThread->base[0] = OBJ_VAL(cl);
	uint32_t op[2] = {OP_ABC(OP_CALL, 0, 0, 0), 0};
	call(vm, currentThread, cl, 0, 0, &op[1]);

	assert(currentThread->base == currentThread->stack + 3);
	return run(vm, currentThread);
}
