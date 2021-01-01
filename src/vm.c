#include "vm.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "chunk.h"
#include "exception.h"
#include "memory.h"
#include "parse.h"
#include "table.h"
#include "builtin.h"

#if defined(DEBUG_TRACE_EXECUTION) || defined(DEBUG_STACK_USAGE)
#include "debug.h"
#endif

static void resetStack(VM *vm) {
	vm->stackTop = vm->stack;
	vm->frameCount = 0;
}

static bool isFalsey(Value v) {
	return IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v))
					 || (IS_ARRAY(v) && AS_ARRAY(v)->count == 0)
					 || (IS_TABLE(v) && count(AS_TABLE(v)) == 0);
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

#define runtimeError(vm, ...) do { \
	vm->exception = OBJ_VAL(ExceptionFormattedStr(vm, __VA_ARGS__)); \
} while(false)
/*
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
*/

static void growStack(VM *vm, size_t space_needed) {
	// pointers into stack: stackTop, stackLast, vm->frames[].slots
	size_t stackTopIndex = vm->stackTop - vm->stack;

	Value *oldStack = vm->stack;
	size_t oldStackSize = vm->stackSize;
	while(vm->stackSize < space_needed)
		vm->stackSize = GROW_CAPACITY(vm->stackSize);
	vm->stack = GROW_ARRAY(vm->stack, Value, oldStackSize, vm->stackSize);
	vm->stackTop = vm->stack + stackTopIndex;
	vm->stackLast = vm->stack + vm->stackSize - 1;
	for(size_t i = 0; i < vm->frameCount; i++)
		vm->frames[i].slots = (Value*)((char*)vm->frames[i].slots + ((char*)vm->stack - (char*)oldStack));
}

CallFrame *incFrame(VM *vm, Reg stackUsed, Value *base, ObjClosure *function) {
	if(vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "Stack overflow.");
		return NULL;
	}
	if(base + 1 + stackUsed > vm->stackLast) {
		size_t base_index = base - vm->stack;
		growStack(vm, base_index + stackUsed + 2);
		base = vm->stack + base_index;
	}
	if(base + stackUsed + 1 > vm->stackTop)
		vm->stackTop = base + stackUsed + 1;

	CallFrame *frame = &vm->frames[vm->frameCount++];
	frame->c = function;
	frame->slots = base + 1;

	return frame;
}

CallFrame *decFrame(VM *vm) {
	vm->frameCount--;
	return &vm->frames[vm->frameCount - 1];
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
	vm->strings = NULL;
	vm->globals = NULL;
	vm->frameCount = 0;
	vm->tryCount = 0;

	vm->initString = NULL;
	vm->stackSize = BASE_STACK_SIZE;
	vm->stack = NULL;
	vm->stackLast = NULL;
	resetStack(vm);
	vm->stack = GROW_ARRAY(NULL, Value, 0, vm->stackSize);
	vm->stackLast = vm->stack + vm->stackSize - 1;
	for(size_t i = 0; i <vm->stackSize; i++)
		vm->stack[i] = NIL_VAL;
	resetStack(vm);

	vm->strings = newTable(vm, 0);
	vm->globals = newTable(vm, 0);
	vm->initString = copyString("init", 4, vm);

	CallFrame *frame = incFrame(vm, 2, &vm->stack[1], NULL);
	ObjModule *builtinM = defineNativeModule(vm, frame, &builtinDef);
	vm->globals = builtinM->items;
//		for(const NativeDef *f = builtins; f->name; f++)
//			defineNative(vm, vm->globals, frame, f);
	decFrame(vm);
}

void freeVM(VM *vm) {
	FREE_ARRAY(Value, vm->stack, vm->stackSize);
	vm->stackSize = 0;
	vm->stackTop = vm->stackLast = NULL;
	vm->strings = NULL;
	vm->globals = NULL;
	vm->initString = NULL;
	freeObjects(vm);
}

static bool call(VM *vm, ObjClosure *function, Value *base, Reg argCount, Reg retCount) {
	if(argCount != function->f->arity) {	// TODO make variadic functions.
		runtimeError(vm, "Expected %d arguments but got %d.", function->f->arity, argCount);
		return false;
	}

	CallFrame *frame = incFrame(vm, function->f->stackUsed, base, function);
	if(frame == NULL)
		return false;

	frame->ip = function->f->chunk.code;
	return true;
}

static bool callValue(VM *vm, Value *callee, Reg retCount, Reg argCount) {
	if(IS_OBJ(*callee)) {
		switch(OBJ_TYPE(*callee)) {
			case OBJ_BOUND_METHOD: {
				ObjBoundMethod *bound = AS_BOUND_METHOD(*callee);
				*callee = bound->receiver;
				if(bound->method->type == OBJ_CLOSURE)
					return call(vm, (ObjClosure*)bound->method, callee, argCount, retCount);
				assert(bound->method->type == OBJ_NATIVE);
				NativeFn native = ((ObjNative*)bound->method)->function;
				Value result = native(vm, argCount, callee+1);
				*callee = result;
				return true;
			}
			case OBJ_CLASS: {
				ObjClass *klass = AS_CLASS(*callee);
				Value initializer;
				assert(klass->methods);
				if(tableGet(klass->methods, vm->initString, &initializer)) {
					if(IS_NATIVE(initializer)) {
						*callee = initializer;
						goto native;
					}
					assert(IS_CLOSURE(initializer));
					*callee = OBJ_VAL(newInstance(vm, klass));
					return call(vm, AS_CLOSURE(initializer), callee, argCount, retCount);
				} else if(argCount) {
					runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
					return false;
				}
				*callee = OBJ_VAL(newInstance(vm, klass));
				return true;
			}
			case OBJ_CLOSURE:
				return call(vm, AS_CLOSURE(*callee), callee, argCount, retCount);
native:
			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(*callee);
				Value result = native(vm, argCount, callee+1);
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

static bool bindMethod(VM *vm, ObjInstance *instance, ObjClass *klass, ObjString *name, Value *slot) {
	Value method;
	assert(instance);
	assert(klass->methods);
	if(!tableGet(klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));

	ObjBoundMethod *bound = newBoundMethod(vm, OBJ_VAL(instance), method);
	*slot = (OBJ_VAL(bound));
	return true;
}

static bool invokeMethod(VM *vm, Value *slot, ObjString *name, Reg retCount, Reg argCount) {
	Value method;
	if(!IS_INSTANCE(*slot)) {
		runtimeError(vm, "Only instances have properties.");
		return false;
	}
	ObjInstance *instance = AS_INSTANCE(*slot);
	assert(instance->klass->methods);
	if(!tableGet(instance->klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));

	ObjBoundMethod *bound = newBoundMethod(vm, OBJ_VAL(instance), method);
	*slot = (OBJ_VAL(bound));
	*slot = bound->receiver;
	// return call(vm, bound->method, slot, argCount, retCount);
	if(bound->method->type == OBJ_CLOSURE)
		return call(vm, (ObjClosure*)bound->method, slot, argCount, retCount);
	assert(bound->method->type == OBJ_NATIVE);
	NativeFn native = ((ObjNative*)bound->method)->function;
	Value result = native(vm, argCount, slot+1);
	*slot = result;
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
	assert(klass->methods);
	tableSet(vm, klass->methods, name, rc);
}

#define READ_BYTECODE() (*frame->ip++)
#define BINARY_OPVV(valueType, op) \
	do { \
		Value b = frame->slots[RB(bytecode)]; \
		Value c = frame->slots[RC(bytecode)]; \
		if(!IS_NUMBER(b) || !IS_NUMBER(c)) { \
			runtimeError(vm, "Operands must be numbers."); \
			goto exception_unwind; \
		} \
		frame->slots[RA(bytecode)] = valueType(AS_NUMBER(b) op AS_NUMBER(c)); \
	} while(false)
#define READ_STRING() AS_STRING(frame->c->f->chunk.constants->values[RD(bytecode)])
static InterpretResult run(VM *vm) {
	CallFrame *frame = &vm->frames[vm->frameCount - 1];
	while(true) {
#ifdef DEBUG_STACK_USAGE
		dumpStack(vm, 8);
#endif /* DEBUG_STACK_USAGE */
#ifdef DEBUG_UPVALUE_USAGE
		dumpOpenUpvalues(vm);
		dumpClosedUpvalues(frame->c);
#endif /* DEBUG_UPVALUE_USAGE */
#ifdef DEBUG_TRACE_EXECUTION
		disassembleInstruction(&frame->c->f->chunk, frame->ip - frame->c->f->chunk.code);
#endif
#ifdef DEBUG_STACK_USAGE
		printf("\n");
#endif /* DEBUG_STACK_USAGE */
		uint32_t bytecode = READ_BYTECODE();
		switch(OP(bytecode)) {
			case OP_CONST_NUM:
				frame->slots[RA(bytecode)] = frame->c->f->chunk.constants->values[RD(bytecode)];
				break;
			case OP_PRIMITIVE:
				frame->slots[RA(bytecode)] = getPrimitive(RD(bytecode)); break;
			case OP_NEGATE: {
				Value vRD = frame->slots[RD(bytecode)];
				if(!IS_NUMBER(vRD)) {
					runtimeError(vm, "Operand must be a number.");
					goto exception_unwind;
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
				if(!tableGet(vm->globals, name, &value)) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					goto exception_unwind;
				}
				frame->slots[RA(bytecode)] = value;
				break;
			}
			case OP_DEFINE_GLOBAL: {
				ObjString *name = READ_STRING();
				tableSet(vm, vm->globals, name, frame->slots[RA(bytecode)]);
				break;
			}
			case OP_SET_GLOBAL: {
				ObjString *name = READ_STRING();
				if(tableSet(vm, vm->globals, name, frame->slots[RA(bytecode)])) {
					runtimeError(vm, "Undefined variable '%s'.", name->chars);
					goto exception_unwind;
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
					goto exception_unwind;
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
				int16_t ra = ((int16_t)(Reg)(RA(bytecode) + 1))-1;
				closeUpvalues(vm, frame->slots - 1);
				// ensure we close this in slots[-1] as an upvalue before moving return value.
				frame->slots[-1] = frame->slots[ra];
				frame = decFrame(vm);
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
					assert(OP(bytecode) == OP_JUMP);
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
					assert(OP(bytecode) == OP_JUMP);
					goto OP_JUMP;
				}
				frame->ip++;
				break;
			case OP_MOV:
				frame->slots[RA(bytecode)] = frame->slots[(int16_t)RD(bytecode)];
				break;
			case OP_CALL:	// RA = func/dest reg; RB = retCount; RC = argCount
				if(!callValue(vm, &frame->slots[RA(bytecode)], RB(bytecode), RC(bytecode)))
					goto exception_unwind;
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
				ObjFunction *f = AS_FUNCTION(frame->c->f->chunk.constants->values[RD(bytecode)]);
				ObjClosure *cl = newClosure(vm, f);
				frame->slots[RA(bytecode)] = OBJ_VAL(cl);	// Can be found by GC
				for(size_t i=0; i<f->uvCount; i++) {
					if((f->uv[i] & UV_IS_LOCAL) == UV_IS_LOCAL)
						cl->upvalues[i] = captureUpvalue(vm, frame->slots + (f->uv[i] & 0xff) - 1);
					else
						cl->upvalues[i] = frame->c->upvalues[(f->uv[i] & 0xff)-1];
				}
				break;
			}
			case OP_CLOSE_UPVALUES:
				closeUpvalues(vm, frame->slots + RA(bytecode));
				break;
			case OP_CLASS: {
				ObjClass *klass = newClass(vm, AS_STRING(frame->c->f->chunk.constants->values[RD(bytecode)]));
				frame->slots[RA(bytecode)] = OBJ_VAL(klass);
				break;
			}
			case OP_GET_PROPERTY: {	// RA = dest reg; RB = object reg; RC = property reg
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = frame->slots[rb];
				if(!IS_INSTANCE(v)) {
					runtimeError(vm, "Only instances have properties.");
					goto exception_unwind;
				}
				ObjInstance *instance = AS_INSTANCE(v);
				ObjString *name = AS_STRING(frame->slots[RC(bytecode)]);
				if(tableGet(instance->fields, name, &frame->slots[RA(bytecode)])) {
					break;
				} else if(bindMethod(vm, instance, instance->klass, name, &frame->slots[RA(bytecode)])) {
					break;
				}
				goto exception_unwind;
			}
			case OP_SET_PROPERTY: {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				Value v = frame->slots[rb];
				if(!IS_INSTANCE(v)) {
					runtimeError(vm, "Only instances have fields.");
					goto exception_unwind;
				}
				ObjInstance *instance = AS_INSTANCE(v);
				assert(IS_STRING(frame->slots[RC(bytecode)]));
				tableSet(vm, instance->fields, AS_STRING(frame->slots[RC(bytecode)]), frame->slots[RA(bytecode)]);
				break;
			}
			case OP_METHOD:
				defineMethod(vm, frame->slots[RA(bytecode)], frame->slots[RB(bytecode)], frame->slots[RC(bytecode)]);
				break;
			/*
			case OP_INVOKE: {
				ObjString *name = AS_STRING(frame->slots[RN(bytecode)]);
				if(invokeMethod(vm, &frame->slots[RM(bytecode)], name, RO(bytecode), RP(bytecode)))
					break;
				goto exception_unwind;
			}
			*/
			case OP_INHERIT: {
				Value superclass = frame->slots[RD(bytecode)];
				if(!IS_CLASS(superclass)) {
					runtimeError(vm, "Superclass must be a class.");
					goto exception_unwind;
				}
				ObjClass *subclass = AS_CLASS(frame->slots[RA(bytecode)]);
				assert(subclass->methods);
				assert(AS_CLASS(superclass)->methods);
				tableAddAll(vm, AS_CLASS(superclass)->methods, subclass->methods);
				break;
			}
			case OP_GET_SUPER: {
				int16_t rb = ((int16_t)(Reg)(RB(bytecode) + 1))-1;
				ObjClass *superclass = AS_CLASS(frame->slots[RA(bytecode)]);
				ObjInstance *instance = AS_INSTANCE(frame->slots[rb]);
				ObjString *name = AS_STRING(frame->slots[RC(bytecode)]);
				if(!bindMethod(vm, instance, superclass, name, &frame->slots[RA(bytecode)]))
					goto exception_unwind;
				break;
			}
			case OP_NEW_ARRAY:
				frame->slots[RA(bytecode)] = OBJ_VAL(newArray(vm, RD(bytecode)));
				break;
			case OP_DUPLICATE_ARRAY: {
				ObjArray *t = duplicateArray(vm, AS_ARRAY(frame->c->f->chunk.constants->values[RD(bytecode)]));
				frame->slots[RA(bytecode)] = OBJ_VAL(t);
				break;
			}
			case OP_NEW_TABLE:
				frame->slots[RA(bytecode)] = OBJ_VAL(newTable(vm, RD(bytecode)));
				break;
			case OP_DUPLICATE_TABLE: {
				ObjTable *t = duplicateTable(vm, AS_TABLE(frame->c->f->chunk.constants->values[RD(bytecode)]));
				frame->slots[RA(bytecode)] = OBJ_VAL(t);
				break;
			}
			case OP_GET_SUBSCRIPT: {
				Value v = frame->slots[RB(bytecode)];
				if(IS_ARRAY(v)) {
					ObjArray *a = AS_ARRAY(v);
					v = frame->slots[RC(bytecode)];
					if(!IS_NUMBER(v)) {
						runtimeError(vm, "Arrays can only be subscripted by numbers.");
						goto exception_unwind;
					}
					double n = AS_NUMBER(v);
					if(n != (int)n) {
						runtimeError(vm, "Subscript must be an integer.");
						goto exception_unwind;
					}
					if(getArray(a, (int)n, &frame->slots[RA(bytecode)])) {
						runtimeError(vm, "Subscript out of bounds.");
						goto exception_unwind;
					}
				} else if(IS_TABLE(v)) {
					ObjTable *t = AS_TABLE(v);
					v = frame->slots[RC(bytecode)];
					if(!IS_STRING(v)) {
						runtimeError(vm, "Tables can only be subscripted by strings.");
						goto exception_unwind;
					}
					if(!tableGet(t, AS_STRING(v), &frame->slots[RA(bytecode)])) {
						runtimeError(vm, "Subscript out of bounds.");
						goto exception_unwind;
					}
				} else {
					runtimeError(vm, "Only arrays and tables can be subscripted.");
					goto exception_unwind;
				}
				break;
			}
			case OP_SET_SUBSCRIPT: {
				Value v = frame->slots[RB(bytecode)];
				if(IS_ARRAY(v)) {
					ObjArray *a = AS_ARRAY(v);
					v = frame->slots[RC(bytecode)];
					if(!IS_NUMBER(v)) {
						runtimeError(vm, "Arrays can only be subscripted by numbers.");
						goto exception_unwind;
					}
					double n = AS_NUMBER(v);
					if(n != (int)n) {
						runtimeError(vm, "Subscript must be an integer.");
						goto exception_unwind;
					}
					setArray(vm, a, (int)n, frame->slots[RA(bytecode)]);
				} else if(IS_TABLE(v)) {
					ObjTable *t = AS_TABLE(v);
					v = frame->slots[RC(bytecode)];
					if(!IS_STRING(v)) {
						runtimeError(vm, "Tables can only be subscripted by strings.");
						goto exception_unwind;
					}
					tableSet(vm, t, AS_STRING(v), frame->slots[RA(bytecode)]);
				} else {
					runtimeError(vm, "Only arrays can be subscripted.");
					goto exception_unwind;
				}
				break;
			}
			case OP_BEGIN_TRY:
				vm->_try[vm->tryCount].ip = (frame->ip + RJump(bytecode)) - frame->c->f->chunk.code;
				vm->_try[vm->tryCount].exception = RA(bytecode);
				vm->_try[vm->tryCount].frame = frame - vm->frames;
				vm->tryCount++;
				break;
			case OP_END_TRY:
				frame->ip += RJump(bytecode);
				assert(vm->tryCount > 0);
				vm->tryCount--;
				break;
			case OP_JUMP_IF_NOT_EXC: {
				Value v = frame->slots[RA(bytecode)];
				Value exception = vm->exception;
				if(IS_CLASS(v) && IS_EXCEPTION(exception) && AS_EXCEPTION(exception)->klass == AS_CLASS(v))
					frame->ip += RJump(bytecode);
				break;
			}
			default:
				fprintf(stderr, "Unimplemented opcode %d.\n", OP(bytecode));
				return INTERPRET_RUNTIME_ERROR;
			case OP_THROW: {
				Value err = frame->slots[RA(bytecode)];
				if(IS_OBJ(err) && (IS_EXCEPTION(err) || (IS_INSTANCE(err) && AS_INSTANCE(err)->klass->isException))) {
					vm->exception = err;
				} else {
					runtimeError(vm, "Only exceptions can be thrown.");
				}
			}
exception_unwind: {
				if(vm->tryCount > 0) {
					vm->tryCount--;
					vm->frameCount = vm->_try[vm->tryCount].frame + 1;
					frame = &vm->frames[vm->frameCount - 1];
					frame->ip = frame->c->f->chunk.code + vm->_try[vm->tryCount].ip;
					frame->slots[vm->_try[vm->tryCount].exception] = vm->exception;
					break;
				} else {
					ObjException *err = AS_EXCEPTION(vm->exception);
					fprintValue(stderr, err->msg);
					fputs("\n", stderr);
					for(size_t i = err->topFrame; i > 0; i--) {
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
					return INTERPRET_RUNTIME_ERROR;
				}
			}
		}
		continue;
	}
}
#undef BINARY_OPVV
#undef READ_BYTECODE

InterpretResult interpret(VM *vm, const char *source, bool printCode) {
	ObjFunction *script = parse(vm, source, printCode);
#ifndef DEBUG_PRINT_CODE
	if(printCode)
		return INTERPRET_OK;
#endif
	if(script == NULL)
		return INTERPRET_COMPILE_ERROR;

	assert(vm->stackSize > 0);
	vm->stack[1] = OBJ_VAL(script);
	ObjClosure *cl = newClosure(vm, script);
	vm->stack[1] = OBJ_VAL(cl);
	call(vm, cl, &vm->stack[1], 0, 1);

	return run(vm);
}
