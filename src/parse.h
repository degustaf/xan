#ifndef XAN_PARSE_H
#define XAN_PARSE_H

#include <stdbool.h>

#include "object.h"
#include "vm.h"

bool parse(VM *vm, const char *source, Chunk *c);

#endif /* XAN_PARSE_H */
