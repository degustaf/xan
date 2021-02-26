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
#include "table.h"
#include "xanString.h"

#define CURRENT_CLOSURE (AS_CLOSURE(vm->base[-3]))
#define CURRENT_FUNCTION (CURRENT_CLOSURE->f)

#if defined(DEBUG_TRACE_EXECUTION) || defined(DEBUG_STACK_USAGE)
#include "debug.h"
#endif

static bool isFalsey(Value v) {
	return IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v))
					 || (IS_ARRAY(v) && AS_ARRAY(v)->count == 0)
					 || (IS_TABLE(v) && count(AS_TABLE(v)) == 0);
}

static Value concatenate(VM *vm, ObjString *b, ObjString *c) {
	size_t length = b->length + c->length;
	char *chars = ALLOCATE(vm, char, length+1);
	memcpy(chars, b->chars, b->length);
	memcpy(chars + b->length, c->chars, c->length);
	chars[length] = '\0';

	ObjString *result = takeString(chars, length, vm);
	return OBJ_VAL(result);
}

#define runtimeError(vm, ...) do { \
	ExceptionFormattedStr(vm, __VA_ARGS__); \
} while(false)

void growStack(VM *vm, size_t space_needed) {
	// pointers into stack: stackTop, stackLast, openUpvalues, base
	size_t stackTopIndex = vm->stackTop - vm->stack;

	Value *oldStack = vm->stack;
	size_t oldStackSize = vm->stackSize;
	while(vm->stackSize < space_needed)
		vm->stackSize = GROW_CAPACITY(vm->stackSize);
	vm->stack = GROW_ARRAY(vm, vm->stack, Value, oldStackSize, vm->stackSize);
	vm->stackTop = vm->stack + stackTopIndex;
	vm->stackLast = vm->stack + vm->stackSize - 1;
	vm->base += vm->stack - oldStack;
	for(ObjUpvalue **uv = &vm->openUpvalues; *uv; uv = &(*uv)->next)
		(*uv)->location += vm->stack - oldStack;
}

static void incFrame(VM *vm, Reg stackUsed, size_t shift, ObjClosure *function, uint32_t *ip) {
	if(vm->base + shift + 1 + stackUsed + 1 > vm->stackLast) {
		size_t base_index = vm->base + shift + 1 - vm->stack;
		growStack(vm, base_index + stackUsed + 1 + 1);
	}
	if(vm->base + shift + 1 + stackUsed + 1 > vm->stackTop)
		vm->stackTop = vm->base + shift + 1 + stackUsed + 1;

	// fprintf(stderr, "base increased for %s frame from %p to %p\n", function== NULL ? "C" : "xan", vm->base, vm->base + shift + 1);
	assert(function);
	assert(ip);
	vm->base += shift + 1;
	vm->base[-2] = IP_VAL((intptr_t)ip);
	vm->base[-3] = OBJ_VAL(function);
}

uint32_t* decFrame(VM *vm) {
	assert(!IS_NIL(vm->base[-3]));
	uint32_t *ip = (uint32_t*)(AS_IP(vm->base[-2]));
	uint32_t bytecode = *(ip - 1);
	Reg ra = RA(bytecode) + 1;
	// fprintf(stderr, "base decreased for xan frame from %p to %p\n", vm->base, vm->base - ra - 2);
	vm->base -= ra + 2;
	return ip;
}

void initVM(VM *vm) {
	// Initialize VM without calling allocator.
	vm->tryCount = 0;
	vm->stack = NULL;
	vm->stackTop = vm->stack + 1;
	vm->stackLast = NULL;
	vm->base = 0;
	vm->stackSize = 0;

	vm->exception = NIL_VAL;
	vm->strings = NULL;
	vm->globals = NULL;
	vm->initString = NULL;
	vm->newString = NULL;
	vm->openUpvalues = NULL;
	vm->currentCompiler = NULL;
	vm->currentClassCompiler = NULL;

	GarbageCollector *gc = &vm->gc;
	gc->objects = NULL;
	gc->grayStack = NULL;
	gc->bytesAllocated = 0;
	gc->nextMinorGC = 256 * 1024;
	gc->nextMajorGC = 1024 * 1024;
	gc->grayCount = 0;
	gc->grayCapacity = 0;
	gc->nextGCisMajor = false;

	vm->stackSize = BASE_STACK_SIZE;
	vm->stack = GROW_ARRAY(vm, NULL, Value, 0, vm->stackSize);
	vm->stackLast = vm->stack + vm->stackSize - 1;
	for(size_t i = 0; i <vm->stackSize; i++)
		vm->stack[i] = NIL_VAL;
	vm->base = vm->stack;
	vm->stackTop = vm->base;

	incCFrame(vm, 2, 3);
	vm->strings = newTable(vm, 0);
	vm->globals = newTable(vm, 0);
	vm->initString = copyString("init", 4, vm);
	vm->newString = copyString("new", 3, vm);
	ObjModule *builtinM = defineNativeModule(vm, &builtinDef);
	vm->globals = builtinM->items;
	decCFrame(vm);
	assert(vm->base == vm->stack);
}

void freeVM(VM *vm) {
	FREE_ARRAY(&vm->gc, Value, vm->stack, vm->stackSize);
	vm->stackSize = 0;
	vm->stackTop = vm->stackLast = NULL;
	vm->strings = NULL;
	vm->globals = NULL;
	vm->initString = NULL;
	vm->newString = NULL;
	freeObjects(&vm->gc);
	if(vm->gc.bytesAllocated > 0)
		fprintf(stderr, "Memory manager lost %zu bytes.\n", vm->gc.bytesAllocated);
}

static uint32_t* call(VM *vm, ObjClosure *function, Reg calleeReg, Reg argCount, uint32_t *ip) {
	if(argCount < function->f->minArity) {
		runtimeError(vm, "Expected at least %d arguments but got %d.", function->f->minArity, argCount);
		return NULL;
	}
	if(argCount > function->f->maxArity) {
		runtimeError(vm, "Expected at most %d arguments but got %d.", function->f->maxArity, argCount);
		return false;
	}

	incFrame(vm, function->f->stackUsed, calleeReg + 2, function, ip);

	size_t codeOffset = argCount - function->f->minArity;
	return function->f->chunk.code + function->f->code_offsets[codeOffset];
}

static uint32_t* callValue(VM *vm, Reg calleeReg, Reg argCount, uint32_t *ip) {
	Value callee = vm->base[calleeReg];
	if(IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
			case OBJ_BOUND_METHOD: {
				ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
				vm->base[calleeReg + 2] = bound->receiver;
				if(bound->method->type == OBJ_CLOSURE)
					return call(vm, (ObjClosure*)bound->method, calleeReg, argCount, ip);
				assert(bound->method->type == OBJ_NATIVE);
				NativeFn native = ((ObjNative*)bound->method)->function;
				assert(false);	// Do we have a test that reaches this point?
				if(native(vm, argCount, vm->base + calleeReg + 1))
					return ip;
				return NULL;
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
					ptrdiff_t v = vm->base - vm->stack;
#endif
					incCFrame(vm, 1, AS_CLOSURE(vm->base[-3])->f->stackUsed + 3);	// don't overwrite arguments.
					ObjInstance *ret = newInstance(vm, klass);
					decCFrame(vm);
					assert(vm->stack + v == vm->base);
					vm->base[calleeReg + 2] = OBJ_VAL(ret);	// vm->base[-1] is this after frame is incremented.
					return call(vm, AS_CLOSURE(initializer), calleeReg, argCount, ip);
				}
				if(argCount) {
					runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
					return NULL;
				}
#ifndef NDEBUG
				ptrdiff_t v = vm->base - vm->stack;
#endif
				assert(argCount == 0);
				incCFrame(vm, 1, calleeReg + 2);
				ObjInstance *ret = newInstance(vm, klass);
				decCFrame(vm);
				assert(vm->stack + v == vm->base);
				vm->base[calleeReg] = OBJ_VAL(ret);
				return ip;
			}
			case OBJ_CLOSURE:
				return call(vm, AS_CLOSURE(callee), calleeReg, argCount, ip);
native:
			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(callee);
				incCFrame(vm, argCount, calleeReg + 2);
				bool ret = native(vm, argCount, vm->base);
				decCFrame(vm);
				vm->base[calleeReg] = vm->base[calleeReg+3];
				return ret ? ip : NULL;
			}
			default:
				break;
		}
	}

	runtimeError(vm, "Can only call functions and classes.");
	return NULL;
}

static bool bindMethod(VM *vm, ObjInstance *instance, ObjClass *klass, Value name, Reg retReg) {
	Value method;
	assert(instance);
	assert(klass->methods);
	if(!tableGet(klass->methods, name, &method)) {
		if(instance->klass == &stringDef) {
			runtimeError(vm, "Only instances have properties.");
			return false;
		}
		assert(IS_STRING(name));
		runtimeError(vm, "Undefined property '%s'.", AS_STRING(name)->chars);
		return false;
	}
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));

	ObjBoundMethod *bound = newBoundMethod(vm, OBJ_VAL(instance), method);
	vm->base[retReg] = (OBJ_VAL(bound));
	return true;
}

static uint32_t* invokeMethod(VM *vm, int16_t instanceReg, ObjString *name, Reg argCount, uint32_t *ip) {
	Value inst = vm->base[instanceReg];
	Value method;
	if(!HAS_PROPERTIES(inst)) {
		runtimeError(vm, "Only instances have properties.");
		return NULL;
	}
	ObjInstance *instance = AS_INSTANCE(inst);
	assert(instance->klass->methods);
	if((!(IS_ARRAY(inst) || IS_STRING(inst))) && (tableGet(instance->fields, OBJ_VAL(name), &method))) {
		vm->base[instanceReg] = method;
		return callValue(vm, instanceReg, argCount, ip);
	}
	if(!tableGet(instance->klass->methods, OBJ_VAL(name), &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return NULL;
	}
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));

	vm->base[instanceReg + 2] = OBJ_VAL(instance);
	vm->base[instanceReg] = method;
	if(AS_OBJ(method)->type == OBJ_CLOSURE)
		return call(vm, AS_CLOSURE(method), instanceReg, argCount, ip);
	assert(AS_OBJ(method)->type == OBJ_NATIVE);
	NativeFn native = AS_NATIVE(method);
	incCFrame(vm, argCount, instanceReg + 2);
#ifdef DEBUG_STACK_USAGE
		dumpStack(vm, 25);
		printf("\n");
#endif /* DEBUG_STACK_USAGE */
	bool ret = native(vm, argCount, vm->base);
	decCFrame(vm);
	vm->base[instanceReg] = vm->base[instanceReg+3];
	return ret ? ip : NULL;
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
	assert(klass->methods);
	tableSet(vm, klass->methods, OBJ_VAL(name), rc);
}

#ifdef COMPUTED_GOTO
	#define SWITCH DISPATCH;
	#define DISPATCH \
		assert(IS_OBJ(vm->base[-3]) && (OBJ_TYPE(vm->base[-3]) == OBJ_CLOSURE)); \
		bytecode = READ_BYTECODE(); \
		goto *opcodes[OP(bytecode)]
	#define TARGET(op) TARGET_##op
	#define DEFAULT
#else
	#define SWITCH \
		assert(IS_OBJ(vm->base[-3]) && (OBJ_TYPE(vm->base[-3]) == OBJ_CLOSURE)); \
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
		Value b = vm->base[RB(bytecode)]; \
		Value c = vm->base[RC(bytecode)]; \
		if(!IS_NUMBER(b) || !IS_NUMBER(c)) { \
			runtimeError(vm, "Operands must be numbers."); \
			goto exception_unwind; \
		} \
		vm->base[RA(bytecode)] = valueType(AS_NUMBER(b) op AS_NUMBER(c)); \
	} while(false)
#define READ_STRING() AS_STRING(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)])
static InterpretResult run(VM *vm) {
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
				vm->base[RA(bytecode)] = CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)];
				DISPATCH;
			TARGET(OP_PRIMITIVE):
				vm->base[RA(bytecode)] = getPrimitive(RD(bytecode)); DISPATCH;
			TARGET(OP_NEGATE): {
				Value vRD = vm->base[RD(bytecode)];
				if(!IS_NUMBER(vRD)) {
					runtimeError(vm, "Operand must be a number.");
					goto exception_unwind;
				}
				vm->base[RA(bytecode)] = NUMBER_VAL(-AS_NUMBER(vRD));
				DISPATCH;
			}
			TARGET(OP_NOT): {
				Value vRD = vm->base[RD(bytecode)];
				vm->base[RA(bytecode)] = BOOL_VAL(isFalsey(vRD));
				DISPATCH;
			}
			TARGET(OP_GET_GLOBAL): {
				ObjString *name = READ_STRING();
				Value value;
				if(!tableGet(vm->globals, OBJ_VAL(name), &value)) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					goto exception_unwind;
				}
				vm->base[RA(bytecode)] = value;
				DISPATCH;
			}
			TARGET(OP_DEFINE_GLOBAL): {
				ObjString *name = READ_STRING();
				tableSet(vm, vm->globals, OBJ_VAL(name), vm->base[RA(bytecode)]);
				DISPATCH;
			}
			TARGET(OP_SET_GLOBAL): {
				ObjString *name = READ_STRING();
				if(tableSet(vm, vm->globals, OBJ_VAL(name), vm->base[RA(bytecode)])) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_EQUAL): {
				Value b = vm->base[RB(bytecode)];
				Value c = vm->base[RC(bytecode)];
				vm->base[RA(bytecode)] = BOOL_VAL(valuesEqual(b, c));
				DISPATCH;
			}
			TARGET(OP_NEQ): {
				Value b = vm->base[RB(bytecode)];
				Value c = vm->base[RC(bytecode)];
				vm->base[RA(bytecode)] = BOOL_VAL(!valuesEqual(b, c));
				DISPATCH;
			}
			TARGET(OP_GREATER):  BINARY_OPVV(BOOL_VAL, >); DISPATCH;
			TARGET(OP_GEQ):      BINARY_OPVV(BOOL_VAL, >=); DISPATCH;
			TARGET(OP_LESS):     BINARY_OPVV(BOOL_VAL, <); DISPATCH;
			TARGET(OP_LEQ):      BINARY_OPVV(BOOL_VAL, <=); DISPATCH;
			TARGET(OP_ADDVV): {
				Value b = vm->base[RB(bytecode)];
				Value c = vm->base[RC(bytecode)];
				if(IS_STRING(b) && IS_STRING(c)) {
					incCFrame(vm, 1, CURRENT_FUNCTION->stackUsed);
					Value ret = concatenate(vm, AS_STRING(b), AS_STRING(c));
					decCFrame(vm);
					vm->base[RA(bytecode)] = ret;
				} else if(IS_NUMBER(b) && IS_NUMBER(c)) {
					vm->base[RA(bytecode)] = NUMBER_VAL(AS_NUMBER(b) + AS_NUMBER(c));
				} else {
					runtimeError(vm, "Operands must be two numbers or two strings.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_SUBVV):	  BINARY_OPVV(NUMBER_VAL, -); DISPATCH;
			TARGET(OP_MULVV):	  BINARY_OPVV(NUMBER_VAL, *); DISPATCH;
			TARGET(OP_DIVVV):	  BINARY_OPVV(NUMBER_VAL, /); DISPATCH;
			TARGET(OP_MODVV): {
				Value b = vm->base[RB(bytecode)];
				Value c = vm->base[RC(bytecode)];
				if(!IS_NUMBER(b) || !IS_NUMBER(c)) {
					runtimeError(vm, "Operands must be numbers.");
					goto exception_unwind;
				}
				vm->base[RA(bytecode)] = NUMBER_VAL(fmod(AS_NUMBER(b), AS_NUMBER(c)));
				DISPATCH;
			}
			TARGET(OP_RETURN): {
				if(vm->base == vm->stack + 3)
					return INTERPRET_OK;

				uint16_t count = RD(bytecode) - 1;
				int16_t ra = ((int16_t)(Reg)(RA(bytecode) + 1))-1;
				Value *oldBase = vm->base;
				ip = decFrame(vm);
				assert((OP(*(ip-1)) == OP_CALL) || (OP(*(ip-1)) == OP_INVOKE));
				// __attribute__((unused)) uint16_t nReturn = RB(*(ip - 1));
				closeUpvalues(vm, oldBase - 1);
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
				vm->base[RA(bytecode)] = vm->base[RD(bytecode)];
				// intentional fallthrough
			TARGET(OP_JUMP_IF_FALSE):
				if(isFalsey(vm->base[RD(bytecode)])) {
					bytecode = READ_BYTECODE();
					assert(OP(bytecode) == OP_JUMP);
					goto OP_JUMP;
				}
				ip++;
				DISPATCH;
			TARGET(OP_COPY_JUMP_IF_TRUE):
				vm->base[RA(bytecode)] = vm->base[RD(bytecode)];
				// intentional fallthrough
			TARGET(OP_JUMP_IF_TRUE):
				if(!isFalsey(vm->base[RD(bytecode)])) {
					bytecode = READ_BYTECODE();
					assert(OP(bytecode) == OP_JUMP);
					goto OP_JUMP;
				}
				ip++;
				DISPATCH;
			TARGET(OP_MOV):
				vm->base[RA(bytecode)] = vm->base[(int16_t)RD(bytecode)];
				DISPATCH;
			TARGET(OP_CALL): {	// RA = func/dest reg; RB = retCount(used by OP_CALL when call returns); RC = argCount
				uint32_t *new_ip;
				if((new_ip = callValue(vm, RA(bytecode), RC(bytecode), ip)) == NULL)
					goto exception_unwind;
				ip = new_ip;
				DISPATCH;
			}
			TARGET(OP_GET_UPVAL): {
				vm->base[RA(bytecode)] = *AS_CLOSURE(vm->base[-3])->upvalues[RD(bytecode)]->location;
				DISPATCH;
			}
			TARGET(OP_SET_UPVAL): {
				*AS_CLOSURE(vm->base[-3])->upvalues[RA(bytecode)]->location = vm->base[RD(bytecode)];
				DISPATCH;
			}
			TARGET(OP_CLOSURE): {
				ObjFunction *f = AS_FUNCTION(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				ObjClosure *cl = newClosure(vm, f);
				vm->base[RA(bytecode)] = OBJ_VAL(cl);	// Can be found by GC
				for(size_t i=0; i<f->uvCount; i++) {
					if((f->uv[i] & UV_IS_LOCAL) == UV_IS_LOCAL)
						cl->upvalues[i] = captureUpvalue(vm, vm->base + (f->uv[i] & 0xff) - 1);
					else
						cl->upvalues[i] = AS_CLOSURE(vm->base[-3])->upvalues[(f->uv[i] & 0xff)-1];
					writeBarrier(vm, cl);
				}
				DISPATCH;
			}
			TARGET(OP_CLOSE_UPVALUES):
				closeUpvalues(vm, vm->base + RA(bytecode));
				DISPATCH;
			TARGET(OP_CLASS): {
				ObjString *name = AS_STRING(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				incCFrame(vm, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjClass *klass = newClass(vm, name);
				decCFrame(vm);
				vm->base[RA(bytecode)] = OBJ_VAL(klass);
				DISPATCH;
			}
			TARGET(OP_GET_PROPERTY): {	// RA = dest reg; RB = object reg; RC = property reg
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = vm->base[rb];
				if(HAS_PROPERTIES(v)) {
					ObjInstance *instance = AS_INSTANCE(v);
					Value name = vm->base[RC(bytecode)];
					assert(IS_STRING(name));
					if((!(IS_ARRAY(v) || IS_STRING(v))) && (tableGet(instance->fields, name, &vm->base[RA(bytecode)]))) {
						DISPATCH;
					} else if(bindMethod(vm, instance, instance->klass, name, RA(bytecode))) {
						DISPATCH;
					}
					goto exception_unwind;
				} else {
					runtimeError(vm, "Only instances have properties.");
					goto exception_unwind;
				}
			}
			TARGET(OP_SET_PROPERTY): {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = vm->base[rb];
				if(HAS_PROPERTIES(v)) {
					ObjInstance *instance = AS_INSTANCE(v);
					Value name = vm->base[RC(bytecode)];
					assert(IS_STRING(name));
					if(!(IS_ARRAY(v) || IS_STRING(v))) {
						tableSet(vm, instance->fields, name, vm->base[RA(bytecode)]);
						DISPATCH;
					}
				}
				runtimeError(vm, "Only instances have fields.");
				goto exception_unwind;
			}
			TARGET(OP_INVOKE): {	// RA = object/dest reg; RA + 1 = property reg; RB = retCount; RC = argCount
				int16_t ra = ((int16_t)(Reg)(RA(bytecode) + 1))-1;
				ObjString *name = AS_STRING(vm->base[ra+1]);
				uint32_t *new_ip;
				if((new_ip = invokeMethod(vm, ra, name, RC(bytecode), ip)) == NULL) {
					goto exception_unwind;
				}
				ip = new_ip;
				DISPATCH;
			}
			TARGET(OP_METHOD):
				defineMethod(vm, vm->base[RA(bytecode)], vm->base[RB(bytecode)], vm->base[RC(bytecode)]);
				DISPATCH;
			TARGET(OP_INHERIT): {
				Value superclass = vm->base[RD(bytecode)];
				if(!IS_CLASS(superclass)) {
					runtimeError(vm, "Superclass must be a class.");
					goto exception_unwind;
				}
				ObjClass *subclass = AS_CLASS(vm->base[RA(bytecode)]);
				assert(subclass->methods);
				assert(AS_CLASS(superclass)->methods);
				tableAddAll(vm, AS_CLASS(superclass)->methods, subclass->methods);
				DISPATCH;
			}
			TARGET(OP_GET_SUPER): {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				ObjClass *superclass = AS_CLASS(vm->base[RA(bytecode)]);
				ObjInstance *instance = AS_INSTANCE(vm->base[rb]);
				Value name = vm->base[RC(bytecode)];
				if(!bindMethod(vm, instance, superclass, name, RA(bytecode)))
					goto exception_unwind;
				DISPATCH;
			}
			TARGET(OP_NEW_ARRAY):
				incCFrame(vm, 1, CURRENT_FUNCTION->stackUsed + 1);
				Value ret = OBJ_VAL(newArray(vm, RD(bytecode)));
				decCFrame(vm);
				vm->base[RA(bytecode)] = ret;
				DISPATCH;
			TARGET(OP_DUPLICATE_ARRAY): {
				ObjArray *src = AS_ARRAY(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				incCFrame(vm, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjArray *t = duplicateArray(vm, src);
				decCFrame(vm);
				vm->base[RA(bytecode)] = OBJ_VAL(t);
				DISPATCH;
			}
			TARGET(OP_NEW_TABLE):
				vm->base[RA(bytecode)] = OBJ_VAL(newTable(vm, RD(bytecode)));
				DISPATCH;
			TARGET(OP_DUPLICATE_TABLE): {
				ObjTable *src = AS_TABLE(CURRENT_FUNCTION->chunk.constants->values[RD(bytecode)]);
				incCFrame(vm, 1, CURRENT_FUNCTION->stackUsed + 1);
				ObjTable *t = duplicateTable(vm, src);
				decCFrame(vm);
				vm->base[RA(bytecode)] = OBJ_VAL(t);
				DISPATCH;
			}
			TARGET(OP_GET_SUBSCRIPT): {
				Value v = vm->base[RB(bytecode)];
				if(IS_ARRAY(v)) {
					ObjArray *a = AS_ARRAY(v);
					v = vm->base[RC(bytecode)];
					if(!IS_NUMBER(v)) {
						runtimeError(vm, "Arrays can only be subscripted by numbers.");
						goto exception_unwind;
					}
					double n = AS_NUMBER(v);
					if(n != (int)n) {
						runtimeError(vm, "Subscript must be an integer.");
						goto exception_unwind;
					}
					if(getArray(a, (int)n, &vm->base[RA(bytecode)])) {
						runtimeError(vm, "Subscript out of bounds.");
						goto exception_unwind;
					}
				} else if(IS_TABLE(v)) {
					ObjTable *t = AS_TABLE(v);
					v = vm->base[RC(bytecode)];
					if(!(IS_STRING(v) || IS_NUMBER(v))) {
						runtimeError(vm, "Tables can only be subscripted by strings or numbers.");
						goto exception_unwind;
					}
					if(!tableGet(t, v, &vm->base[RA(bytecode)])) {
						runtimeError(vm, "Subscript out of bounds.");
						goto exception_unwind;
					}
				} else if(IS_STRING(v)) {
					ObjString *s = AS_STRING(v);
					v = vm->base[RC(bytecode)];
					if((!IS_NUMBER(v)) || (AS_NUMBER(v) != (double)(int)AS_NUMBER(v))) {
						runtimeError(vm, "Strings can only be subscripted by integers.");
						goto exception_unwind;
					}
					int i = (int)AS_NUMBER(v);
					if((i < 0) || ((unsigned int)i >= s->length)) {
						runtimeError(vm, "Subscript out of range.");
						goto exception_unwind;
					}
					incCFrame(vm, 1, CURRENT_FUNCTION->stackUsed + 1);
					ObjString *ret = copyString(s->chars + i, 1, vm);
					decCFrame(vm);
					vm->base[RA(bytecode)] = OBJ_VAL(ret);
				} else {
					runtimeError(vm, "Only arrays, tables, and strings can be subscripted.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_SET_SUBSCRIPT): {
				Value v = vm->base[RB(bytecode)];
				if(IS_ARRAY(v)) {
					ObjArray *a = AS_ARRAY(v);
					v = vm->base[RC(bytecode)];
					if(!IS_NUMBER(v)) {
						runtimeError(vm, "Arrays can only be subscripted by numbers.");
						goto exception_unwind;
					}
					double n = AS_NUMBER(v);
					if(n != (int)n) {
						runtimeError(vm, "Subscript must be an integer.");
						goto exception_unwind;
					}
					setArray(vm, a, (int)n, vm->base[RA(bytecode)]);
				} else if(IS_TABLE(v)) {
					ObjTable *t = AS_TABLE(v);
					v = vm->base[RC(bytecode)];
					if(!(IS_STRING(v) || IS_NUMBER(v))) {
						runtimeError(vm, "Tables can only be subscripted by strings or numbers.");
						goto exception_unwind;
					}
					tableSet(vm, t, v, vm->base[RA(bytecode)]);
				} else {
					runtimeError(vm, "Only arrays can be subscripted.");
					goto exception_unwind;
				}
				DISPATCH;
			}
			TARGET(OP_BEGIN_TRY):
				vm->_try[vm->tryCount].ip = (ip + RJump(bytecode)) - CURRENT_FUNCTION->chunk.code;
				vm->_try[vm->tryCount].exception = RA(bytecode);
				vm->tryCount++;
				DISPATCH;
			TARGET(OP_END_TRY):
				ip += RJump(bytecode);
				assert(vm->tryCount > 0);
				vm->tryCount--;
				DISPATCH;
			TARGET(OP_JUMP_IF_NOT_EXC): {
				Value v = vm->base[RA(bytecode)];
				Value exception = vm->exception;
				if(IS_CLASS(v) && IS_EXCEPTION(exception) && AS_EXCEPTION(exception)->klass == AS_CLASS(v))
					ip += RJump(bytecode);
				DISPATCH;
			}
			DEFAULT
			TARGET(OP_THROW): {
				Value err = vm->base[RA(bytecode)];
				if(IS_OBJ(err) && (IS_EXCEPTION(err) || (IS_INSTANCE(err) && AS_INSTANCE(err)->klass->isException))) {
					vm->exception = err;
				} else {
					runtimeError(vm, "Only exceptions can be thrown.");
				}
			}
exception_unwind: {
				if(vm->tryCount > 0) {
					vm->tryCount--;
					ip = CURRENT_FUNCTION->chunk.code + vm->_try[vm->tryCount].ip;
					vm->base[vm->_try[vm->tryCount].exception] = vm->exception;
					DISPATCH;
				} else {
					ObjException *err = AS_EXCEPTION(vm->exception);
					fprintValue(stderr, err->msg);
					fputs("\n", stderr);
					for(Value *base = vm->stack + err->topBase;
							base > vm->stack;
							base -= (IS_NIL(base[-3]) ? AS_IP(base[-2]) :
								(RA(*((uint32_t*)(AS_IP(base[-2]))- 1)) + 3))) {

						if(IS_CLOSURE(base[-3])) {	// In a xan frame.
							ObjFunction *f = AS_CLOSURE(base[-3])->f;
							size_t instruction = ip - f->chunk.code - 1;	// We have already advanced ip.
							fprintf(stderr, "[line %zu] in ", f->chunk.lines[instruction]);
							if(f->name == NULL) {
								fprintf(stderr, "script\n");
							} else {
								fprintf(stderr, "%s()\n", f->name->chars);
							}
						} else {		// In a c frame.
							fprintf(stderr, "in a c function\n");
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
#undef READ_BYTECODE
#undef TARGET
#undef DISPATCH
#undef SWITCH

InterpretResult interpret(VM *vm, const char *source, bool printCode) {
	assert(vm->base == vm->stack);
	incCFrame(vm, 3, 3);
	// The parser needs 3 stack slots to stash values to prevent premature freeing.
	ObjFunction *script = parse(vm, source, printCode);
	decCFrame(vm);
	assert(vm->base == vm->stack);
#ifndef DEBUG_PRINT_CODE
	if(printCode)
		return INTERPRET_OK;
#endif
	if(script == NULL)
		return INTERPRET_COMPILE_ERROR;

	assert(vm->stackSize > 0);
	vm->base[0] = OBJ_VAL(script);
	ObjClosure *cl = newClosure(vm, script);
	vm->base[0] = OBJ_VAL(cl);
	// call(vm, cl, 0, 0);	// inlined, constant propagated, and tweaked to prevent vm->base underflow.

	if(vm->base + 3 + cl->f->stackUsed + 1 > vm->stackLast) {
		size_t base_index = vm->base + 3 - vm->stack;
		growStack(vm, base_index + cl->f->stackUsed + 1 + 1);
	}
	if(vm->base + 3 + cl->f->stackUsed + 1 > vm->stackTop)
		vm->stackTop = vm->base + 3 + cl->f->stackUsed + 1;

	vm->base += 3;
	vm->base[-3] = OBJ_VAL(cl);
	uint32_t op[2] = {OP_ABC(OP_CALL, 0, 0, 0), 0};
	vm->base[-2] = IP_VAL((intptr_t)&op[1]);

	assert(vm->base == vm->stack + 3);
	return run(vm);
}
