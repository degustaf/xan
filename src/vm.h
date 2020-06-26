#ifndef XAN_VM_H
#define XAN_VM_H

#include "chunk.h"

#include <stddef.h>

#include "table.h"

void initVM(VM *vm);
void freeVM(VM *vm);
InterpretResult interpret(VM *vm, const char *source, bool printCode);

#endif /* XAN_VM_H */
