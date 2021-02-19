#include "exception.h"

#include <stdarg.h>

#include "class.h"
#include "memory.h"

static bool ExceptionNew(VM *vm, int argCount, Value *args) {
	if(argCount > 0) {
		ExceptionFormattedStr(vm, "Method 'new' of class 'exception' expected 0 arguments but got %d.", argCount);
		return false;
	}
	ObjException *exc = ALLOCATE_OBJ(vm, ObjException, OBJ_EXCEPTION);
	exc->klass = NULL;
	exc->fields = NULL;
	exc->msg = NIL_VAL;
	exc->topFrame = vm->frameCount;
	args[-3] = OBJ_VAL(exc);
	return true;
}

void ExceptionFormattedStr(VM *vm, const char* format, ...) {
	Value args[5];
	ExceptionNew(vm, 0, args + 3);
	ObjException *exc = AS_EXCEPTION(args[0]);
	vm->exception = OBJ_VAL(exc);
	va_list args1, args2;
	va_start(args1, format);
	va_copy(args2, args1);
	size_t length = vsnprintf(NULL, 0, format, args1);
	va_end(args1);
	char *buffer = ALLOCATE(vm, char, length+1);
	vsnprintf(buffer, length + 1, format, args2);
	va_end(args2);
	
	exc->msg = OBJ_VAL(takeString(buffer, length, vm));
}

bool ExceptionInit(VM *vm, int argCount, Value *args) {
	if(argCount > 1) {
		ExceptionFormattedStr(vm, "Method 'init' of class 'exception' expected 1 argument but got %d.", argCount);
		return false;
	}
	ExceptionNew(vm, 0, args);
	ObjException *ret = AS_EXCEPTION(args[-3]);
	ret->msg = args[0];

	return true;
}

NativeDef exceptionMethods[] = {
	{"init", &ExceptionInit},
	{"new", &ExceptionNew},
	{NULL, NULL}
};

ObjClass exceptionDef = {
	CLASS_HEADER,
	"Exception",
	exceptionMethods,
	RUNTIME_CLASSDEF_FIELDS,
	true
};
