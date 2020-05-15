#ifndef XAN_DEBUG_H
#define XAN_DEBUG_H

#include "vm.h"

void disassembleChunk(Chunk* chunk, const char *name);
void disassembleInstruction(Chunk* chunk, size_t offset);
void dumpStack(VM *vm, size_t count);

#endif /* XAN_DEBUG_H */
