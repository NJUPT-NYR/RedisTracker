
# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?= -W -Wall -fno-common -g -ggdb -std=c99 -O2
	SHOBJ_LDFLAGS ?= -shared
else
	SHOBJ_CFLAGS ?= -W -Wall -dynamic -fno-common -g -ggdb -std=c99 -O2
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup
endif

.SUFFIXES: .c .so .xo .o

TOPDIR := $(shell pwd)
SRCDIR := ${TOPDIR}/src

SOURCE_FILES_C := $(shell find ${SRCDIR} -name "*.c")
OBJS := $(SOURCE_FILES_C:.c=.o)
INCLUDE = -I ${TOPDIR} -I ${SRCDIR}

all: redistracker.so

%.o: %.c
	$(CC) ${INCLUDE} $(CFLAGS) $(SHOBJ_CFLAGS) -fPIC -c $< -o $@ 

redistracker.so: ${OBJS}
	$(LD) $^ -o $@ $(SHOBJ_LDFLAGS) ${LDFLAGS}

clean:
	-rm -rf *.so *.o ${OBJS}
	-rm -rf ${SRCDIR}/*.gcda ${SRCDIR}/*.gcno ${SRCDIR}/*.gcov