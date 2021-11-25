#include "../src/common.h"
#include "xan.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/debug.h"
#include "../src/vm.h"

static void repl(bool printCode, int argc, char** argv) {
	VM vm;
	initVM(&vm, argc, argv, argc);
	char line[1024];	// TODO there should not be a hardcoded line length.

	while(true) {
		printf("> ");

		// TODO The REPL should handle multi-line input.
		if(!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		interpret(&vm, line, printCode);
	}

	freeVM(&vm);
}

static void runFile(const char *path, bool printCode, int argc, char** argv, int start) {
	VM vm;
	initVM(&vm, argc, argv, start);
	char *source = readFile(path);
	if(source == NULL) {
		int errnum = errno;
		errno = 0;
		fprintf(stderr, "Could not open file \"%s\": %s\n", path, strerror(errnum));
		exit(errnum);
	}
	InterpretResult result = interpret(&vm, source, printCode);
	free(source);
	freeVM(&vm);

	if(result == INTERPRET_COMPILE_ERROR) exit(EXIT_COMPILE_ERROR);
	if(result == INTERPRET_RUNTIME_ERROR) exit(EXIT_RUNTIME_ERROR);
}

int main(int argc, char** argv) {
	bool printCode = false;
	int i = 1;
	if((argc > 1) && (strcmp(argv[i], "-b") == 0)) {
		printCode = true;
		i++;
	}
	if(argc == i) {
		repl(printCode, argc, argv);
	} else if(argc >= i+1) {
		runFile(argv[i], printCode, argc, argv, i);
	} else {
		fprintf(stderr, "Usage: %s [path]\n", argv[0]);
		exit(64);
	}

	return 0;
}
