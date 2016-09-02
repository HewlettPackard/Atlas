CC = clang
CXX = clang++

# Choice of compiler flags inspired by Terence's recommended gcc flags list.
# Organized into sections & eliminated deprecated/redundant flags as of gcc 5.4

## Debug Options
DFLAGS = #-fsanitize=address -g -D_GLIBCXX_DEBUG

## Optimization Options
OFLAGS  = -O3 -fstack-protector-all
# included in -O2 and above
# -fstrict-aliasing -fstrict-overflow

## Code Generation Options
GFLAGS  = -ftrapv

## Preprocessor Options
PFLAGS  = -Wendif-labels -Wunused-macros

## Warning Options
WFLAGS  = -Wall -Wextra -Wpedantic # essential
# Included in -Wall
# -Wimplicit (same as -Wimplicit-int and -Wimplicit-function-declaration)
#
# -Waddress -Warray-bounds -Wchar-subscripts -Wenum-compare -Wimplicit-int
# -Wimplicit-function-declaration -Wcomment -Wformat -Wmain -Wmissing-braces
# -Wnonnull -Wparentheses -Wpointer-sign -Wreturn-type -Wsequence-point
# -Wsign-compare -Wstrict-aliasing -Wswitch -Wtrigraphs -Wuninitialized
# -Wunknown-pragmas -Wunused -Wunused-function -Wunused-label -Wunused-value
# -Wunused-variable -Wvolatile-register-var -Wunused-but-set-variable
#
# Included in -Wextra
# -Wclobbered -Wempty-body -Wignored-qualifiers -Wmissing-field-initializers
# -Wmissing-parameter-type -Wold-style-declaration -Woverride-init
# -Wtype-limits -Wunused-parameter -Wunused-but-set-parameter
#
# included in -Wpedantic
# -Woverlength-strings -Wpointer-arith -Wvariadic-macros

WFLAGS += -Wformat=2
# Included in -Wformat=2
# -Wformat-nonliteral -Wformat-security -Wformat-y2k

WFLAGS += -Waggregate-return -Wcast-align -Wcast-qual -Wconversion
WFLAGS += -Wdisabled-optimization -Wdouble-promotion -Wfloat-equal -Winit-self
WFLAGS += -Winline -Winvalid-pch -Wmissing-declarations
WFLAGS += -Wmissing-format-attribute -Wmissing-include-dirs -Wpacked -Wpadded
WFLAGS += -Wredundant-decls -Wstack-protector -Wstrict-overflow=5
WFLAGS += -Wswitch-default -Wswitch-enum -Wundef -Wvla -Wwrite-strings

# For testing only, comment out for production
WFLAGS += -Wshadow #-Werror

### C only
CWFLAGS  = -Wc++-compat
# Included in -Wc++-compat
# -Wjump-misses-init

CWFLAGS += -Wbad-function-cast -Wdeclaration-after-statement
CWFLAGS += -Wmissing-prototypes -Wnested-externs -Wold-style-definition
CWFLAGS += -Wstrict-prototypes

### GNU only (enable manually if you switch to gcc/g++ as compiler)
GNUWFLAGS  = -Wlogical-op -Wnormalized=nfc -Wpacked-bitfield-compat -Wsync-nand
GNUWFLAGS += -Wtrampolines -Wunsafe-loop-optimizations

# GNU, C only (enable manually if you switch to gcc/g++ as compiler)
GNUCWFLAGS = -Wtraditional-conversion -Wunsuffixed-float-constants


COMMONFLAGS = $(OFLAGS) $(GFLAGS) $(PFLAGS) $(WFLAGS) $(DFLAGS)


NUM_THREADS ?= 11
NUM_VERTICES ?= 1048576

CFLAGS    = -std=c11   $(COMMONFLAGS) $(CWFLAGS)
CXXFLAGS  = -std=c++11 $(COMMONFLAGS) -D NUM_THREADS=$(NUM_THREADS) -DNUM_VERTICES=$(NUM_VERTICES)
LDFLAGS	  = -lpthread

SRCS = contraption.cpp
OBJS = $(SRCS:.cpp=.o)
EXEC = driver graph_gen

.PHONY: all clean
all: $(EXEC)

gen_input: graph_gen
	./graph_gen 1000000 7586063 | ./graph_gen > gg_1000000

split_input:
	split gg_1000000 -n l/$(NUM_THREADS)

driver: $(OBJS) driver.cpp

graph_gen: graph_gen.c

clean:
	rm -f $(EXEC) *.o
