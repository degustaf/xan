#ifndef XAN_DEBUG_H
#define XAN_DEBUG_H

#include "vm.h"

void disassembleChunk(Chunk* chunk, const char *name);
void disassembleInstruction(Chunk* chunk, size_t offset);
void dumpStack(VM *vm, size_t count);
void dumpOpenUpvalues(VM *vm);
void dumpClosedUpvalues(ObjClosure *c);

#endif /* XAN_DEBUG_H */
