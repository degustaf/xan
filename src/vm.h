#ifndef XAN_VM_H
#define XAN_VM_H

#include <stddef.h>

#include "type.h"

CallFrame *incFrame(VM *vm, Reg stackUsed, Value *base, ObjClosure *function);
CallFrame *decFrame(VM *vm);

#endif /* XAN_VM_H */
