BASE_DIR := .
SRC_DIR :=  $(BASE_DIR)/src

LIBDIR := $(BASE_DIR)/lib 

LIBATOMIC_DIR := $(BASE_DIR)/libatomic

SRC_FILES := makalu_base_md.c makalu_hdr.c \
             makalu_transient.c makalu_persistent.c

INCLUDES := -I $(LIBATOMIC_DIR)/include \
            -I $(SRC_DIR)/internal_include \
            -I $(BASE_DIR)/include

SRC := $(addprefix $(SRC_DIR)/, $(SRC_FILES)) 

OBJS := ${SRC:.c=.o}

LIBNAME := libmakalu.a

CC := gcc
CFLAGS := -g -O2
MKDIR := mkdir -p
AR := ar
RANLIB := ranlib
RM := rm -f

.PHONY: all
all: depend directories $(LIBNAME)
	echo $(SRC)
	echo $(INCLUDE_DIRS)


depend: .depend


.depend: $(SRC)
	$(RM) ./.depend
	$(CC) $(CFLAGS) $(INCLUDES) -MM $^>>./.depend;

include .depend


.PHONY: directories
directories:
	$(MKDIR) $(LIBDIR)

$(LIBNAME): $(OBJS)
	$(AR) cru $@ $^
	$(RANLIB) $@


.c.o:
	$(CC) $(CFLAGS) $< -o $@



