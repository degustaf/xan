#ifndef XAN_EXCEPTION_H
#define XAN_EXCEPTION_H

#include "type.h"

extern ObjClass exceptionDef;

typedef struct {
	INSTANCE_FIELDS;
	Value msg;
	size_t topBase;
} ObjException;

void ExceptionFormattedStr(VM *vm, const char* format, ...);

#endif /* XAN_EXCEPTION_H */
