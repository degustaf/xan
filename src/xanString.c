#include "xanString.h"

#include <assert.h>
#include <string.h>

#include "class.h"
#include "exception.h"
#include "memory.h"
#include "object.h"
#include "table.h"

static ObjString* allocateString(char *chars, size_t length, uint32_t hash, VM *vm) {
	ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;
	string->fields = NULL;
	string->klass = &stringDef;
	vm->base[0] = OBJ_VAL(string);
	tableSet(vm, vm->strings, OBJ_VAL(string), NIL_VAL);

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
		FREE_ARRAY(&vm->gc, char, chars, length+1);
		return interned;
	}
	return allocateString(chars, length, hash, vm);
}

ObjString* copyString(const char *chars, size_t length, VM *vm) {
	uint32_t hash = hashString(chars, length);
	ObjString *interned = tableFindString(vm->strings, chars, length, hash);
	if(interned) return interned;

	char *heapChars = ALLOCATE(vm, char, length+1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(heapChars, length, hash, vm);
}

static bool stringLength(VM *vm, int argCount, Value *args) {
	if(argCount > 0) {
		ExceptionFormattedStr(vm, "Method 'length' of class 'string' expected 0 argument but got %d.", argCount);
		return false;
	}
	assert(IS_STRING(args[-1]));
	args[-3] = NUMBER_VAL(AS_STRING(args[-1])->length);
	return true;
}

NativeDef stringMethods[] = {
	{"length", &stringLength},
	{NULL, NULL}
};

ObjClass stringDef = {
	CLASS_HEADER,
	"string",
	stringMethods,
	RUNTIME_CLASSDEF_FIELDS,
	false,
};
