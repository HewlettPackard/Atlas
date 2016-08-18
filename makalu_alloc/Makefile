BASE_DIR := .
SRC_DIR :=  $(BASE_DIR)/src

LIBDIR := $(BASE_DIR)/lib

LIBATOMIC_DIR := $(BASE_DIR)/libatomic

SRC_FILES := makalu_base_md.c makalu_hdr.c \
             makalu_init.c  makalu_mark.c \
             makalu_transient.c makalu_persistent.c \
             makalu_util.c makalu_reclaim.c \
             makalu_block.c makalu_thread.c \
             makalu_local_heap.c makalu_malloc.c

INCLUDES := -I $(LIBATOMIC_DIR)/include \
            -I $(SRC_DIR)/internal_include \
            -I $(BASE_DIR)/include

SRC := $(addprefix $(SRC_DIR)/, $(SRC_FILES)) 

OBJS := ${SRC:.c=.o}

LIBNAME :=libmakalu.a

CC := gcc
CFLAGS := -g -O0 -Wall -c
MKDIR := mkdir -p
AR := ar
RANLIB := ranlib
RM := rm -f

.PHONY: all
all: depend directories $(LIBDIR)/$(LIBNAME)


depend: .depend


.depend: $(SRC)
	$(RM) ./.depend
	$(CC) $(CFLAGS) $(INCLUDES) -MM $^>>./.depend;

include .depend


.PHONY: directories
directories:
	$(MKDIR) $(LIBDIR)

$(LIBDIR)/$(LIBNAME): $(OBJS)
	$(AR) cr $@ $^
	$(RANLIB) $@

.PHONY: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBDIR)/$(LIBNAME)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

