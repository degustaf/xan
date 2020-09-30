#ifndef XAN_EXCEPTION_H
#define XAN_EXCEPTION_H

#include "type.h"

extern classDef exceptionDef;

// TODO convert this to instance of exception class.
typedef struct {
	INSTANCE_FIELDS;
	ObjString *msg;
	size_t topFrame;
} ObjException;

ObjException *ExceptionFormattedStr(VM *vm, const char* format, ...);

#endif /* XAN_EXCEPTION_H */
