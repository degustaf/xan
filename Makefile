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
PATHU =			unittest
PATHUB =		unittestBuild
SRCS =			$(wildcard $(PATHS)/*.c)
USRCS =			$(wildcard $(PATHU)/*.c)
LINK = 			$(CC)
C_STD =			c99
DEF =			-pg -g 
CFLAGS =		-I$(PATHS) -Wall -Wextra -pedantic $(ARCH) -std=$(C_STD) -D_POSIX_C_SOURCE=200809L -O3 $(DEF)

LDFLAGS =		$(ARCH) $(DEF)
LDLIBS =

COMPILE =		$(CC) $(CFLAGS) -MT $@ -MP -MMD -MF $(PATHD)/$*.Td
OBJS =			$(addprefix $(PATHB)/, $(notdir $(SRCS:.c=.o)))
POSTCOMPILE =	@mv -f $(PATHD)/$*.Td $(PATHD)/$*.d && touch $@
UBINS =			$(addprefix $(PATHUB)/, $(notdir $(USRCS:.c=$(TARGET_EXTENSION))))

.PHONY: all clean test unittest release

.PRECIOUS: $(PATHD)/%.d
.PRECIOUS: $(PATHB)/%.o
.PRECIOUS: $(PATHUB)/%


# Rules

# all: CFLAGS += -DDEBUG -DTEST -g -fprofile-arcs
all: CFLAGS += -DDEBUG -DTEST -g
all: xan$(TARGET_EXTENSION)


$(PATHB):
	$(MKDIR) $@

$(PATHD):
	$(MKDIR) $@

$(PATHUB):
	$(MKDIR) $@

xan$(TARGET_EXTENSION): $(PATHB)/xan$(TARGET_EXTENSION)
	ln -sf $^ $@

$(PATHB)/xan$(TARGET_EXTENSION): | unittest
$(PATHB)/xan$(TARGET_EXTENSION): $(OBJS) | $(PATHB)
	$(LINK) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(PATHB)/%.o: $(PATHS)/%.c | $(PATHB) $(PATHD)
	$(COMPILE) -c $< -o $@
	$(POSTCOMPILE)

$(PATHUB)/%.o: $(PATHU)/%.c | $(PATHUB) $(PATHD)
	$(COMPILE) -c $< -o $@
	$(POSTCOMPILE)

$(PATHUB)/%$(TARGET_EXTENSION): $(PATHUB)/%.o | $(PATHB)
	$(LINK) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$@

test: xan$(TARGET_EXTENSION)
	python3 util/test.py $<

unittest: $(UBINS)

release: clean
	$(MAKE) DEF=-DNDEBUG test

clean:
	$(CLEANUP) $(PATHS)/*.d
	$(CLEANUP) $(PATHD)/*.Td
	$(CLEANUP) $(PATHB)/*.o
	$(CLEANUP) $(PATHB)/xan
	$(CLEANUP) $(PATHUB)/*
	$(CLEANUP) ./xan


include $(wildcard $(PATHD)/*.d)
