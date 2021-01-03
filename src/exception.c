#include "exception.h"
#include "memory.h"

#include <stdarg.h>

ObjException *newException(VM *vm) {
	ObjException *exc = ALLOCATE_OBJ(ObjException, OBJ_EXCEPTION);
	exc->klass = NULL;
	exc->fields = NULL;
	exc->msg = NIL_VAL;
	exc->topFrame = vm->frameCount;

	return exc;
}

void ExceptionFormattedStr(VM *vm, const char* format, ...) {
	ObjException *exc = newException(vm);
	vm->exception = OBJ_VAL(exc);
	va_list args1, args2;
	va_start(args1, format);
	va_copy(args2, args1);
	size_t length = vsnprintf(NULL, 0, format, args1);
	va_end(args1);
	char *buffer = ALLOCATE(char, length+1);
	vsnprintf(buffer, length + 1, format, args2);
	va_end(args2);
	
	exc->msg = OBJ_VAL(takeString(buffer, length+1, vm));
}

Value ExceptionInit(VM *vm, int argCount, Value *args) {
	ObjException *ret = newException(vm);
	ret->msg = args[0];

	return OBJ_VAL(ret);
}

NativeDef exceptionMethods[] = {
	{"init", &ExceptionInit},
	{NULL, NULL}
};

ObjClass exceptionDef = {
	{
		OBJ_CLASS,
		false,
		NULL
	},
	"Exception",
	exceptionMethods,
	NULL,
	NULL,
	true
};
