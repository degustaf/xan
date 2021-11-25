#include <stdio.h>
#include <stdlib.h>

char* readFile(const char *path) {
	FILE *file = fopen(path, "rb");
	if(file == NULL)
		return NULL;

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char *buffer = malloc(fileSize+1);
	if(buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
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
