#include "class.h"

NativeDef classMethods[] = {
	{NULL, NULL}
};

ObjClass classDef = {
	CLASS_HEADER,
	"Class",
	classMethods,
	RUNTIME_CLASSDEF_FIELDS,
	false
};
