#ifndef XAN_DEBUG_H
#define XAN_DEBUG_H

#include "chunk.h"
#include "vm.h"

void disassembleInstruction(Chunk* chunk, size_t offset);
void disassembleChunk(Chunk* chunk);
void disassembleFunction(ObjFunction *f);
void dumpStack(thread *currentThread, size_t count);
void dumpOpenUpvalues(thread *currentThread);
void dumpClosedUpvalues(ObjClosure *c);

#endif /* XAN_DEBUG_H */
