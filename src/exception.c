#include "exception.h"

#include <stdarg.h>

#include "chunk.h"
#include "class.h"
#include "memory.h"

static bool ExceptionNew(VM *vm, thread *currentThread, int argCount) {
	if(argCount > 0) {
		ExceptionFormattedStr(vm, currentThread, "Method 'new' of class 'exception' expected 0 arguments but got %d.", argCount);
		return false;
	}
	ObjException *exc = ALLOCATE_OBJ(vm, ObjException, OBJ_EXCEPTION);
	exc->klass = NULL;
	exc->fields = NULL;
	exc->msg = NIL_VAL;
	exc->topBase = 0;
	currentThread->base[0] = OBJ_VAL(exc);
	return true;
}

void ExceptionFormattedStr(VM *vm, thread *currentThread, const char* format, ...) {
	incCFrame(vm, currentThread, 1, 3);
	ExceptionNew(vm, currentThread, 0);
	ObjException *exc = AS_EXCEPTION(currentThread->base[0]);
	decCFrame(currentThread);
	currentThread->exception = OBJ_VAL(exc);
	va_list args1, args2;
	va_start(args1, format);
	va_copy(args2, args1);
	size_t length = vsnprintf(NULL, 0, format, args1);
	va_end(args1);
	char *buffer = ALLOCATE(vm, char, length+1);
	vsnprintf(buffer, length + 1, format, args2);
	va_end(args2);
	
	exc->msg = OBJ_VAL(takeString(vm, currentThread, buffer, length));
	exc->topBase = currentThread->base - currentThread->stack;
}

bool ExceptionInit(VM *vm, thread *currentThread, int argCount) {
	if(argCount > 1) {
		ExceptionFormattedStr(vm, currentThread, "Method 'init' of class 'exception' expected 1 argument but got %d.", argCount);
		return false;
	}
	incCFrame(vm, currentThread, 1, argCount + 3);
	ExceptionNew(vm, currentThread, 0);
	ObjException *ret = AS_EXCEPTION(currentThread->base[0]);
	decCFrame(currentThread);
	ret->topBase = (currentThread->base - (IS_NIL(currentThread->base[-3]) ? AS_IP(currentThread->base[-2]) :
			(RA(*((uint32_t*)(AS_IP(currentThread->base[-2]))- 1)) + 3))) - currentThread->stack;
	ret->msg = currentThread->base[0];
	currentThread->base[0] = OBJ_VAL(ret);

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
