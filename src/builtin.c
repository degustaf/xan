#include "builtin.h"

#include <math.h>
#include <time.h>

#include "array.h"
#include "exception.h"
#include "object.h"
#include "table.h"
#include "xanString.h"

static bool clockNative(VM *vm, int argCount, Value *args) {
	if(argCount != 0) {
		ExceptionFormattedStr(vm, "Function 'clock' expected 0 argument but got %d.", argCount);
		return false;
	}
	args[-1] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
	return true;
}

static bool printNative(VM *vm, int argCount, Value *args) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, "Function 'print' expected 1 argument but got %d.", argCount);
		return false;
	}
	printValue(*args);
	printf("\n");
	args[-1] = NIL_VAL;
	return true;
}

static bool sqrtNative(VM *vm, int argCount, Value *args) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, "Function 'sqrt' expected 1 argument but got %d.", argCount);
		return false;
	}
	args[-1] = NUMBER_VAL(sqrt(AS_NUMBER(*args)));
	return true;
}

ObjClass *BuiltinClasses[] = {
	&arrayDef,
	&exceptionDef,
	&stringDef,
	&tableDef,
	NULL
};

NativeDef BuiltinMethods[] = {
	{"clock", clockNative},
	{"print", printNative},
	{"sqrt", sqrtNative},
	{NULL, NULL},
};

ModuleDef builtinDef = {
	"Builtin Module",
	BuiltinClasses,
	BuiltinMethods
};
