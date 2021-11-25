#ifndef XAN_PARSE_H
#define XAN_PARSE_H

#include <stdbool.h>

#include "object.h"
#include "vm.h"

ObjFunction *parse(VM *vm, thread *currentThread, const char *source, bool printCode);

#endif /* XAN_PARSE_H */
