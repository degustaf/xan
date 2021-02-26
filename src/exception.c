#include "exception.h"

#include <stdarg.h>

#include "chunk.h"
#include "class.h"
#include "memory.h"

static bool ExceptionNew(VM *vm, int argCount, __attribute__((unused)) Value *args) {
	if(argCount > 0) {
		ExceptionFormattedStr(vm, "Method 'new' of class 'exception' expected 0 arguments but got %d.", argCount);
		return false;
	}
	ObjException *exc = ALLOCATE_OBJ(vm, ObjException, OBJ_EXCEPTION);
	exc->klass = NULL;
	exc->fields = NULL;
	exc->msg = NIL_VAL;
	exc->topBase = 0;
	vm->base[0] = OBJ_VAL(exc);
	return true;
}

void ExceptionFormattedStr(VM *vm, const char* format, ...) {
	Value args[5];
	incCFrame(vm, 1, 3);
	ExceptionNew(vm, 0, args + 3);
	ObjException *exc = AS_EXCEPTION(vm->base[0]);
	decCFrame(vm);
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
	exc->topBase = vm->base - vm->stack;
}

bool ExceptionInit(VM *vm, int argCount, Value *args) {
	if(argCount > 1) {
		ExceptionFormattedStr(vm, "Method 'init' of class 'exception' expected 1 argument but got %d.", argCount);
		return false;
	}
	incCFrame(vm, 1, argCount + 3);
	ExceptionNew(vm, 0, args);
	ObjException *ret = AS_EXCEPTION(vm->base[0]);
	decCFrame(vm);
	ret->topBase = (vm->base - (IS_NIL(vm->base[-3]) ? AS_IP(vm->base[-2]) :
			(RA(*((uint32_t*)(AS_IP(vm->base[-2]))- 1)) + 3))) - vm->stack;
	ret->msg = vm->base[0];
	vm->base[0] = OBJ_VAL(ret);

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
