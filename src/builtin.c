#include "builtin.h"

#include <time.h>

#include "array.h"
#include "object.h"
#include "table.h"

static Value clockNative(__attribute__((unused))VM *vm, __attribute__((unused))int argCount, __attribute__((unused))Value *args) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value printNative(__attribute__((unused))VM *vm, __attribute__((unused))int argCount, Value *args) {
	// if(argCount != 1) {		// TODO add error handling to native functions.
	// 	runtimeError(vm, "Expected 1 argument but got %d.", argCount);
	// 	return NIL_VAL;
	// }
	printValue(*args);
	printf("\n");
	return NIL_VAL;
}

classDef *BuiltinClasses[] = {
	&arrayDef,
	&tableDef,
	NULL
};

NativeDef BuiltinMethods[] = {
	{"clock", clockNative},
	{"print", printNative},
	{NULL, NULL},
};

ModuleDef builtinDef = {
	"Builtin Module",
	BuiltinClasses,
	BuiltinMethods
};
