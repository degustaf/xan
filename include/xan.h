#ifndef XAN_XAN_H
#define XAN_XAN_H

#include <stdbool.h>

typedef struct sObj Obj;
typedef struct sVM VM;

void initVM(VM *vm, int argc, char** argv, int start);
void freeVM(VM *vm);

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
} InterpretResult;

InterpretResult interpret(VM *vm, const char *source, bool printCode);

char* readFile(const char *path);

#endif /* XAN_XAN_H */
