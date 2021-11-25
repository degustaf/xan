#include "builtin.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include "array.h"
#include "chunk.h"
#include "class.h"
#include "exception.h"
#include "object.h"
#include "parse.h"
#include "table.h"
#include "xanString.h"
#include "vm.h"

static bool clockNative(VM *vm, thread *currentThread, int argCount) {
	if(argCount != 0) {
		ExceptionFormattedStr(vm, currentThread, "Function 'clock' expected 0 argument but got %d.", argCount);
		return false;
	}
	currentThread->base[0] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
	return true;
}

static bool importNative(VM *vm, thread *currentThread, int argCount) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, currentThread, "Function 'import' expected 1 argument but got %d.", argCount);
		return false;
	}
	if(!IS_STRING(currentThread->base[0])) {
		ExceptionFormattedStr(vm, currentThread, "Function 'import' expected a string.");
		return false;
	}

	Value ret = NIL_VAL;
	if(tableGet(vm->builtinMods, currentThread->base[0], &ret)) {
		currentThread->base[0] = ret;
		return true;
	}

	ObjModule *builtinM = NULL;
	tableGet(vm->builtinMods, OBJ_VAL(copyString(vm, currentThread, "builtin", 7)), &OBJ_VAL(builtinM));
	tableGet(vm->builtinMods, OBJ_VAL(copyString(vm, currentThread, "sys", 3)), &ret);
	assert(IS_MODULE(ret));
	tableGet(AS_MODULE(ret)->fields, OBJ_VAL(copyString(vm, currentThread, "path", 4)), &ret);
	assert(IS_ARRAY(ret));
	ObjArray *pathArray = AS_ARRAY(ret);
	for(size_t i=0; i<pathArray->count; i++) {
		size_t len = AS_STRING(pathArray->values[i])->length + AS_STRING(currentThread->base[0])->length + 6;
		char temp[len];
		snprintf(temp, len, "%s/%s.xan", AS_CSTRING(pathArray->values[i]), AS_CSTRING(currentThread->base[0]));
		errno = 0;
		char *source = readFile(temp);
		if(source) {
			incCFrame(vm, currentThread, 3, 3);
			ObjFunction *script = parse(vm, currentThread, source, false);	// TODO create new thread.
			decCFrame(currentThread);
			if(script == NULL)
				return false;

			currentThread->base[0] = OBJ_VAL(vm->globals);
			vm->globals = newTable(vm, currentThread, 0);
			tableAddAll(vm, builtinM->fields, vm->globals);
			incCFrame(vm, currentThread, 1, 3);
			currentThread->base[0] = OBJ_VAL(script);
			ObjClosure *cl = newClosure(vm, script);
			currentThread->base[0] = OBJ_VAL(cl);
			uint32_t op[2] = {OP_ABC(OP_CALL, 0, 0, 0), 0};
			call(vm, currentThread, cl, 0, 0, &op[1]);

			InterpretResult res = run(vm, currentThread);
			currentThread->base[0] = OBJ_VAL(newModule(vm, currentThread, AS_STRING(currentThread->base[0])));
			decCFrame(currentThread);
			if(res != INTERPRET_OK)
				return false;
		}
		errno = 0;	// We don't really care why it can't be opened. It probably doesn't exist.
	}

	ExceptionFormattedStr(vm, currentThread, "Cannot find module '%s'.", AS_CSTRING(currentThread->base[0]));
	return false;
}

static bool printNative(VM *vm, thread *currentThread, int argCount) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, currentThread, "Function 'print' expected 1 argument but got %d.", argCount);
		return false;
	}
	printValue(currentThread->base[0]);
	printf("\n");
	currentThread->base[0] = NIL_VAL;
	return true;
}

static bool sqrtNative(VM *vm, thread *currentThread, int argCount) {
	if(argCount != 1) {
		ExceptionFormattedStr(vm, currentThread, "Function 'sqrt' expected 1 argument but got %d.", argCount);
		return false;
	}
	currentThread->base[0] = NUMBER_VAL(sqrt(AS_NUMBER(currentThread->base[0])));
	return true;
}

ObjClass *BuiltinClasses[] = {
	&classDef,
	&arrayDef,
	&exceptionDef,
	&moduleDef,
	&stringDef,
	&tableDef,
	NULL
};

NativeDef BuiltinMethods[] = {
	{"clock", clockNative},
	{"import", importNative},
	{"print", printNative},
	{"sqrt", sqrtNative},
	{NULL, NULL},
};

ModuleDef builtinDef = {
	"builtin",
	BuiltinClasses,
	BuiltinMethods
};
