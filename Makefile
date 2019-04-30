# Defines

ifeq ($(OSTYPE),cygwin)
	CLEANUP=rm -f
	CLEANDIR=rm -rf
	MKDIR=mkdir -p
	TARGET_EXTENSION=
else ifeq ($(OS),Windows_NT)
	CLEANUP=del /F /Q
	CLEANDIR=rd /S /Q
	MKDIR=mkdir
	TARGET_EXTENSION=.exe
else
	CLEANUP=rm -f
	CLEANDIR=rm -rf
	MKDIR=mkdir -p
	TARGET_EXTENSION=
endif

ifndef CC
	CC = gcc
endif

PATHS = 		src
PATHB = 		build
PATHD =			depend
SRCS =			$(wildcard $(PATHS)/*.c)
LINK = 			$(CC)
C_STD =			c99
CFLAGS =		-I$(PATHS) -Wall -Wextra -pedantic $(ARCH) -std=$(C_STD) -D_POSIX_C_SOURCE=200809L

LDFLAGS =
LDLIBS =

COMPILE =		$(CC) $(CFLAGS) -MT $@ -MP -MMD -MF $(PATHD)/$*.Td
OBJS =			$(addprefix $(PATHB)/, $(notdir $(SRCS:.c=.o)))
POSTCOMPILE =	@mv -f $(PATHD)/$*.Td $(PATHD)/$*.d && touch $@

.PHONY: all clean test

.PRECIOUS: $(PATHD)/%.d
.PRECIOUS: $(PATHB)/%.o


# Rules

# all: CFLAGS += -DDEBUG -DTEST -g -fprofile-arcs
all: CFLAGS += -DDEBUG -DTEST -g
all: xan$(TARGET_EXTENSION)


$(PATHB):
	$(MKDIR) $@

$(PATHD):
	$(MKDIR) $@

xan$(TARGET_EXTENSION): $(PATHB)/xan$(TARGET_EXTENSION)
	ln -sf $^ $@

$(PATHB)/xan$(TARGET_EXTENSION): $(OBJS) | $(PATHB)
	$(LINK) -o $@ $^ $(LDLIBS) $(LDFLAGS)

$(PATHB)/%.o: $(PATHS)/%.c | $(PATHB) $(PATHD)
	$(COMPILE) -c $< -o $@
	$(POSTCOMPILE)

test: xan$(TARGET_EXTENSION)
	python3 util/test.py $<

clean:
	$(CLEANUP) $(PATHS)/*.d
	$(CLEANUP) $(PATHD)/*.Td
	$(CLEANUP) $(PATHB)/*.o
	$(CLEANUP) $(PATHB)/xan
	$(CLEANUP) ./xan


include $(wildcard $(PATHD)/*.d)
