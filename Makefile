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

PATHS = 			src
PATHI =				include
PATHU =				unittest
PATHC = 			clients
PATHB = 			build
PATHLB = 			libbuild
PATHD =				depend
PATHUB =			unittestBuild
SRCS =				$(wildcard $(PATHS)/*.c)
USRCS =				$(wildcard $(PATHU)/*.c)
LIBRARY =			libxan.a
CC = 				gcc
LINK = 				$(CC)
AR = 				ar
C_STD =				c99
DEF =				-pg -g 
CFLAGS =			-I$(PATHS) -I$(PATHI) -Wall -Wextra -pedantic $(ARCH) -std=$(C_STD) -D_POSIX_C_SOURCE=200809L $(DEF)
CLIENT_CFLAGS = 	-I$(PATHI) -Wall -Wextra -pedantic $(ARCH) -std=$(C_STD) -D_POSIX_C_SOURCE=200809L $(DEF)
# CFLAGS =			-I$(PATHS) -I$(PATHI) -Wall -Wextra -pedantic $(ARCH) -std=$(C_STD) -D_POSIX_C_SOURCE=200809L -O3 $(DEF)
# CLIENT_CFLAGS = 	-I$(PATHI) -Wall -Wextra -pedantic $(ARCH) -std=$(C_STD) -D_POSIX_C_SOURCE=200809L -O3 $(DEF)

LDFLAGS =			$(ARCH) $(DEF)
LDLIBS =

COMPILE =			$(CC) $(CFLAGS) -MT $@ -MP -MMD -MF $(PATHD)/$*.Td
OBJS =				$(addprefix $(PATHLB)/, $(notdir $(SRCS:.c=.o)))
POSTCOMPILE =		@mv -f $(PATHD)/$*.Td $(PATHD)/$*.d && touch $@
UBINS =				$(addprefix $(PATHUB)/, $(notdir $(USRCS:.c=$(TARGET_EXTENSION))))

.PHONY: all clean test unittest release

.PRECIOUS: $(PATHD)/%.d
.PRECIOUS: $(PATHB)/%.o
.PRECIOUS: $(PATHB)/%.a
.PRECIOUS: $(PATHLB)/%.o
.PRECIOUS: $(PATHUB)/%


# Rules

all: xan$(TARGET_EXTENSION)


$(PATHB):
	$(MKDIR) $@

$(PATHLB):
	$(MKDIR) $@

$(PATHD):
	$(MKDIR) $@

$(PATHUB):
	$(MKDIR) $@


xan$(TARGET_EXTENSION): $(PATHB)/xan$(TARGET_EXTENSION)
	ln -sf $^ $@

$(PATHB)/%$(TARGET_EXTENSION): $(PATHB)/%.o $(PATHLB)/$(LIBRARY) | $(PATHB)
	$(LINK) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(PATHLB)/$(LIBRARY): $(OBJS)
	$(MAKE) unittest
	$(RM) $@
	$(AR) rcs $@ $^

$(PATHB)/%.o: $(PATHC)/%.c | $(PATHB) $(PATHD)
	$(COMPILE) -c $< -o $@
	$(POSTCOMPILE)

$(PATHLB)/%.o: $(PATHS)/%.c | $(PATHLB) $(PATHD)
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
	$(CLEANUP) $(PATHLB)/*
	$(CLEANUP) $(PATHB)/*
	$(CLEANUP) $(PATHUB)/*
	$(CLEANUP) ./xan


include $(wildcard $(PATHD)/*.d)
