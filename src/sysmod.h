#ifndef XAN_SYS_H
#define XAN_SYS_H

#include "type.h"

extern ModuleDef SysDef;

void SysInit(VM *vm, thread *currentThread, ObjModule *SysM, int argc, char** argv, int start);

#endif /* XAN_SYS_H */
