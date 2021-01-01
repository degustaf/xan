#include "object.h"

#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "exception.h"
#include "memory.h"
#include "table.h"

ObjFunction *newFunction(VM *vm, size_t uvCount) {
	ObjFunction *f = (ObjFunction*)allocateObject(sizeof(*f) + uvCount * sizeof(uint16_t), OBJ_FUNCTION, vm);

	f->arity = 0;
	f->uvCount = uvCount;
	f->stackUsed = 0;
	f->name = NULL;
	fwdWriteBarrier(vm, OBJ_VAL(f));
	initChunk(vm, &f->chunk);
	return f;
}

ObjUpvalue *newUpvalue(VM *vm, Value *slot) {
	ObjUpvalue *uv = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	uv->location = slot;
	uv->closed = NIL_VAL;
	uv->next = NULL;
	return uv;
}

ObjClosure *newClosure(VM *vm, ObjFunction *f) {
	ObjUpvalue **uvs = ALLOCATE(ObjUpvalue*, f->uvCount);
	for(size_t i=0; i<f->uvCount; i++)
		uvs[i] = NULL;
	ObjClosure *cl = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	cl->f = f;
	cl->upvalues = uvs;
	cl->uvCount = f->uvCount;
	return cl;
}

ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, Value method) {
	ObjBoundMethod *ret = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	ret->receiver = receiver;
	assert(IS_OBJ(method));
	assert(isObjType(method, OBJ_CLOSURE) || isObjType(method, OBJ_NATIVE));
	ret->method = AS_OBJ(method);
	return ret;
}

ObjClass *newClass(VM *vm, ObjString *name) {
	ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	klass->methods = NULL;
	klass->cname = NULL;
	klass->methodsArray = NULL;
	fwdWriteBarrier(vm, OBJ_VAL(klass));
	klass->methods = newTable(vm, 0);
	return klass;
}

void defineNative(VM *vm, ObjTable *t, CallFrame *frame, const NativeDef *f) {
	frame->slots[0] = OBJ_VAL(copyString(f->name, strlen(f->name), vm));
	frame->slots[1] = OBJ_VAL(newNative(vm, f->method));
	tableSet(vm, t, AS_STRING(frame->slots[0]), frame->slots[1]);
}

void defineNativeClass(VM *vm, ObjTable *t, CallFrame *frame, ObjClass *klass) {
	klass->name = copyString(klass->cname, strlen(klass->cname), vm);

	// klass is statically allocated, so it avoids newClass. This puts it in the linked list of objects for the garbafe collector.
	klass->obj.next = vm->objects;
	vm->objects = (Obj*)klass;

	frame->slots[0] = OBJ_VAL(klass);

	CallFrame *frame2 = incFrame(vm, 2, &frame->slots[1], NULL);
	klass->methods = newTable(vm, 0);
	assert(klass->methods);
	for(size_t i = 0; klass->methodsArray[i].name; i++)
		defineNative(vm, klass->methods, frame2, &klass->methodsArray[i]);
	decFrame(vm);

	tableSet(vm, t, klass->name, OBJ_VAL(klass));
}

ObjModule * newModule(VM *vm, ObjString *name) {
	// It is the callers responsability to ensure that name is findable by the GC.
	ObjModule *module = ALLOCATE_OBJ(ObjModule, OBJ_MODULE);
	module->name = name;
	module->items = NULL;
	fwdWriteBarrier(vm, OBJ_VAL(module));
	module->items = newTable(vm, 0);
	return module;
}

ObjModule *defineNativeModule(VM *vm, CallFrame *frame, ModuleDef *def) {
	frame->slots[0] = OBJ_VAL(copyString(def->name, strlen(def->name), vm));
	ObjModule *ret = newModule(vm, AS_STRING(frame->slots[0]));
	frame->slots[0] = OBJ_VAL(ret);		// For GC.

	CallFrame *frame2 = incFrame(vm, 1, &frame->slots[1], NULL);
	for(ObjClass **c = def->classes; *c; c++)
		defineNativeClass(vm, ret->items, frame2, *c);
	assert(def->methods);
	for(NativeDef *m = def->methods; m->name; m++)
		defineNative(vm, ret->items, frame2, m);
	decFrame(vm);

	return ret;
}

ObjInstance *newInstance(VM *vm, ObjClass *klass) {
	ObjInstance *o = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	o->klass = klass;
	o->fields = NULL;
	fwdWriteBarrier(vm, OBJ_VAL(o));
	o->fields = newTable(vm, 0);
	return o;
}

ObjNative *newNative(VM *vm, NativeFn function) {
	ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjArray *duplicateArray(VM *vm, ObjArray *source) {
	ObjArray *dest = newArray(vm, source->count);
	for(size_t i=0; i<source->count; i++)
		dest->values[i] = source->values[i];
	return dest;
}

void setArray(VM *vm, ObjArray *array, int idx, Value v) {
	assert(idx >= 0);
	if((size_t)idx >= array->capacity) {
		size_t capacity = GROW_CAPACITY(round_up_pow_2(idx));
		fwdWriteBarrier(vm, v);
		array->values = GROW_ARRAY(array->values, Value, array->capacity, capacity);
		array->capacity = capacity;
	}

	for(size_t i = array->count; i < (size_t)idx; i++)
		array->values[i] = NIL_VAL;

	array->values[idx] = v;
	if((size_t)idx >= array->count)
		array->count = (size_t)idx + 1;

}

bool getArray(ObjArray *array, int idx, Value *ret) {
	if((idx < 0) || ((size_t)idx >= array->count))
		return true;

	*ret = array->values[idx];
	return false;
}

static void fprintFunction(FILE *restrict stream, ObjFunction *f) {
	if(f->name == NULL) {
		fprintf(stream, "<script>");
	} else {
		fprintf(stream, "<fn %s>", f->name->chars);
	}
}

static void fprintArray(FILE *restrict stream, ObjArray *array) {
	if(array->count == 0) {
		fprintf(stream, "[]");
		return;
	}

	fprintf(stream, "[");
	fprintValue(stream, array->values[0]);
	for(size_t i = 1; i < array->count; i++) {
		fprintf(stream, ", ");
		fprintValue(stream, array->values[i]);
	}
	fprintf(stream, "]");
}

static ObjString* allocateString(char *chars, size_t length, uint32_t hash, VM *vm) {
	ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;
	fwdWriteBarrier(vm, OBJ_VAL(string));
	tableSet(vm, vm->strings, string, NIL_VAL);

	return string;
}

static uint32_t hashString(const char *key, size_t length) {
	uint32_t hash = 2166136261u;

	for(size_t i=0; i<length; i++) {
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

ObjString *takeString(char *chars, size_t length, VM *vm) {
	uint32_t hash = hashString(chars, length);
	ObjString *interned = tableFindString(vm->strings, chars, length, hash);
	if(interned) {
		FREE_ARRAY(char, chars, length+1);
		return interned;
	}
	return allocateString(chars, length, hash, vm);
}

ObjString* copyString(const char *chars, size_t length, VM *vm) {
	uint32_t hash = hashString(chars, length);
	ObjString *interned = tableFindString(vm->strings, chars, length, hash);
	if(interned) return interned;

	char *heapChars = ALLOCATE(char, length+1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(heapChars, length, hash, vm);
}

void fprintObject(FILE *restrict stream, Value value) {
	switch(OBJ_TYPE(value)) {
		case OBJ_ARRAY:
			fprintArray(stream, AS_ARRAY(value));
			break;
		case OBJ_BOUND_METHOD: {
				Obj *method = AS_BOUND_METHOD(value)->method;
				if(method->type == OBJ_CLOSURE) {
					fprintFunction(stream, ((ObjClosure*)method)->f);
					break;
				}
				assert(method->type == OBJ_NATIVE);
				fprintf(stream, "<native method>");
				break;
			}
		case OBJ_CLASS:
			fprintf(stream, "<Class %s>", AS_CLASS(value)->name->chars);
			break;
		case OBJ_CLOSURE:
			fprintFunction(stream, AS_CLOSURE(value)->f);
			break;
		case OBJ_FUNCTION:
			fprintFunction(stream, AS_FUNCTION(value));
			break;
		case OBJ_INSTANCE:
			fprintf(stream, "%s instance", AS_INSTANCE(value)->klass->name->chars);
			break;
		case OBJ_MODULE:
			fprintf(stream, "<Module %s>", AS_MODULE(value)->name->chars);
			break;
		case OBJ_NATIVE:
			fprintf(stream, "<native fn>");
			break;
		case OBJ_STRING:
			fprintf(stream, "%s", AS_CSTRING(value));
			break;
		case OBJ_TABLE:
			fprintTable(stream, AS_TABLE(value));
			break;
		case OBJ_UPVALUE:
			fprintf(stream, "upvalue");
			break;
		case OBJ_EXCEPTION:
			fprintf(stream, "Error: ");
			fprintValue(stream, AS_EXCEPTION(value)->msg);
			fprintf(stream, ": ");
			break;
	}
}

void printObject(Value value) {
	fprintObject(stdout, value);
}

bool valuesEqual(Value a, Value b) {
	if(a.type != b.type) return false;

	switch(a.type) {
		case VAL_BOOL:		return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL:		return true;
		case VAL_NUMBER:	return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ:		return AS_OBJ(a) == AS_OBJ(b);
	}
	return false;
}

void fprintValue(FILE *restrict stream, Value value) {
	switch(value.type) {
		case VAL_BOOL:
			fprintf(stream, AS_BOOL(value) ? "true" : "false");
			break;
		case VAL_NIL:
			fprintf(stream, "nil");
			break;
		case VAL_NUMBER:
			fprintf(stream, "%g", AS_NUMBER(value));
			break;
		case VAL_OBJ:
			fprintObject(stream, value);
			break;
	}
}

void printValue(Value value) {
	fprintValue(stdout, value);
}
