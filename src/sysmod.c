#include "sysmod.h"

#include <string.h>

#include "object.h"
#include "table.h"

ObjClass *SysClasses[] = {
	NULL
};

NativeDef SysMethods[] = {
	{NULL, NULL},
};

ModuleDef SysDef = {
	"sys",
	SysClasses,
	SysMethods
};

void SysInit(VM *vm, thread *currentThread, ObjModule *SysM, int argc, char** argv, int start) {
	ObjArray *ARGV = newArray(vm, currentThread, argc - start);
	currentThread->base[0] = OBJ_VAL(ARGV);
	ObjString *ARGVname = copyString(vm, currentThread, "ARGV", 4);
	tableSet(vm, SysM->fields, OBJ_VAL(ARGVname), OBJ_VAL(ARGV));
	for(size_t i = 0; i < (size_t)(argc - start); i++) {
		ARGV->values[i] = OBJ_VAL(copyString(vm, currentThread, argv[start + i], strlen(argv[start + i])));
	}

	ObjArray *path = newArray(vm, currentThread, 2);
	currentThread->base[0] = OBJ_VAL(path);
	ObjString *pathName = copyString(vm, currentThread, "path", 4);
	tableSet(vm, SysM->fields, OBJ_VAL(pathName), OBJ_VAL(path));
	path->values[0] = OBJ_VAL(copyString(vm, currentThread, ".", 1));
	path->values[1] = OBJ_VAL(copyString(vm, currentThread, "/home/degustaf/xan/library", 26));	// TODO this shouldn't be hard coded.
}
