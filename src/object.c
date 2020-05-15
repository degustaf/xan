#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType, vm)

static Obj* allocateObject(size_t size, ObjType type, VM *vm) {
	Obj *object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;
	object->next = vm->objects;
	vm->objects = object;
	return object;
}

ObjFunction *newFunction(VM *vm) {
	ObjFunction *f = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);

	f->arity = 0;
	f->stackUsed = 0;
	f->name = NULL;
	initChunk(&f->chunk);
	return f;
}

ObjNative *newNative(VM *vm, NativeFn function) {
	ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

static void fprintFunction(FILE *restrict stream, ObjFunction *f) {
	if(f->name == NULL) {
		fprintf(stream, "<script>");
	} else {
		fprintf(stream, "<fn %s>", f->name->chars);
	}
}

static ObjString* allocateString(char *chars, size_t length, uint32_t hash, VM *vm) {
	ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;
	tableSet(&vm->strings, string, NIL_VAL);

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
		case OBJ_FUNCTION:
			fprintFunction(stream, AS_FUNCTION(value));
			break;
		case OBJ_NATIVE:
			fprintf(stream, "<native fn>");
			break;
		case OBJ_STRING:
			fprintf(stream, "%s", AS_CSTRING(value));
			break;
	}
}

void printObject(Value value) {
	fprintObject(stdout, value);
}
