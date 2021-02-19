#ifndef XAN_VM_H
#define XAN_VM_H

#include <stddef.h>

#include "type.h"

Value *incFrame(VM *vm, Reg stackUsed, Value *base, ObjClosure *function);
CallFrame *decFrame(VM *vm);
void growStack(VM *vm, size_t space_needed);

static inline void StackUsed(VM *vm, Value **base, size_t usage) {
	if(*base + usage > vm->stackLast) {
		size_t base_index = *base - vm->stack;
		growStack(vm, base_index + usage + 1);
		*base = vm->stack + base_index;
	}
	if(*base + usage > vm->stackTop)
		vm->stackTop = *base + usage;
}

#endif /* XAN_VM_H */
