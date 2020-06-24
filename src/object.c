#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType, vm)

static Obj* allocateObject(size_t size, ObjType type, VM *vm) {
	Obj *object = (Obj*)reallocate(vm, NULL, 0, size);
	object->type = type;
	object->isMarked = false;
	object->next = vm->objects;
	vm->objects = object;
#ifdef DEBUG_LOG_GC
	printf("%p allocate %ld for %s\n", (void*)object, size, ObjTypeNames[type]);
#endif /* DEBUG_LOG_GC */
	return object;
}

ObjFunction *newFunction(VM *vm, size_t uvCount) {
	ObjFunction *f = (ObjFunction*)allocateObject(sizeof(*f) + uvCount * sizeof(uint16_t), OBJ_FUNCTION, vm);

	f->arity = 0;
	f->uvCount = uvCount;
	f->stackUsed = 0;
	f->name = NULL;
	vm->temp4GC = OBJ_VAL(f);
	initChunk(vm, &f->chunk);
	vm->temp4GC = NIL_VAL;
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

ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, ObjClosure *method) {
	ObjBoundMethod *ret = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	ret->receiver = receiver;
	ret->method = method;
	return ret;
}

ObjClass *newClass(VM *vm, ObjString *name) {
	ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	initTable(&klass->methods);
	return klass;
}

ObjInstance *newInstance(VM *vm, ObjClass *klass) {
	ObjInstance *o = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	o->klass = klass;
	initTable(&o->fields);
	return o;
}

ObjNative *newNative(VM *vm, NativeFn function) {
	ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjArray *newArray(VM *vm, size_t count) {
	ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->count = 0;
	array->capacity = 0;
	array->values = NULL;
	if(count) {
		size_t capacity = round_up_pow_2(count);
		array->values = GROW_ARRAY(array->values, Value, 0, capacity);
		array->count = count;
		array->capacity = capacity;
	}
	return array;
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
		array->values = GROW_ARRAY(array->values, Value, array->capacity, capacity);
		array->capacity = capacity;
	}

	for(size_t i = array->count; i < (size_t)idx; i++)
		array->values[i] = NIL_VAL;

	array->values[idx] = v;
	if((size_t)idx >= array->count)
		array->count = (size_t)idx + 1;

}

bool getArray(VM *vm, ObjArray *array, int idx, Value *ret) {
	if((idx < 0) || (idx >= array->count))
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
	vm->temp4GC = OBJ_VAL(string);
	tableSet(vm, &vm->strings, string, NIL_VAL);
	vm->temp4GC = NIL_VAL;

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
	ObjString *interned = tableFindString(&vm->strings, chars, length, hash);
	if(interned) {
		FREE_ARRAY(char, chars, length+1);
		return interned;
	}
	return allocateString(chars, length, hash, vm);
}

ObjString* copyString(const char *chars, size_t length, VM *vm) {
	uint32_t hash = hashString(chars, length);
	ObjString *interned = tableFindString(&vm->strings, chars, length, hash);
	if(interned) return interned;

	char *heapChars = ALLOCATE(char, length+1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(heapChars, length, hash, vm);
}

void fprintObject(FILE *restrict stream, Value value) {
	switch(OBJ_TYPE(value)) {
		case OBJ_BOUND_METHOD:
			fprintFunction(stream, AS_BOUND_METHOD(value)->method->f);
			break;
		case OBJ_CLASS:
			fprintf(stream, "%s", AS_CLASS(value)->name->chars);
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
		case OBJ_NATIVE:
			fprintf(stream, "<native fn>");
			break;
		case OBJ_STRING:
			fprintf(stream, "%s", AS_CSTRING(value));
			break;
		case OBJ_UPVALUE:
			fprintf(stream, "upvalue");
			break;
		case OBJ_ARRAY:
			fprintArray(stream, AS_ARRAY(value));
			break;
	}
}

void printObject(Value value) {
	fprintObject(stdout, value);
}
