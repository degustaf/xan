#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "vm.h"

static void repl(void) {
	VM vm;
	initVM(&vm);
	char line[1024];	// TODO there should not be a hardcoded line length.

	while(true) {
		printf("> ");

		// TODO The REPL should handle multi-line input.
		if(!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		interpret(&vm, line);
	}

	freeVM(&vm);
}

static char* readFile(const char *path) {
	FILE *file = fopen(path, "rb");
	if(file == NULL) {
		int errnum = errno;
		errno = 0;
		fprintf(stderr, "Could not open file \"%s\": %s\n", path, strerror(errnum));
		exit(errnum);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char *buffer = malloc(fileSize+1);
	if(buffer == NULL) {
		fprintf(stderr, "Not enough mempry to read \"%s\".\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if(bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
	}
	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

static void runFile(const char *path) {
	VM vm;
	initVM(&vm);
	char *source = readFile(path);
	InterpretResult result = interpret(&vm, source);
	free(source);

	if(result == INTERPRET_COMPILE_ERROR) exit(EXIT_COMPILE_ERROR);
	if(result == INTERPRET_RUNTIME_ERROR) exit(EXIT_RUNTIME_ERROR);

	freeVM(&vm);
}

int main(int argc, char** argv) {
	if(argc == 1) {
		repl();
	} else if(argc == 2) {
		runFile(argv[1]);
	} else {
		fprintf(stderr, "Usage: %s [path]\n", argv[0]);
		exit(64);
	}

	return 0;
}
