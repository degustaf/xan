#ifndef XAN_DEBUG_H
#define XAN_DEBUG_H

#include "chunk.h"

void disassembleChunk(Chunk* chunk, const char *name);
void disassembleInstruction(Chunk* chunk, size_t offset);

#endif /* XAN_DEBUG_H */
