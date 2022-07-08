PROGRAM=libnax.so
VERSION=.1.0.1

TARGET := $(PROGRAM)$(VERSION)
INC_DIRS :=
INC_DIRS := ./
INC_DIRS += ../../include/
INC_DIRS += ../dict/

SRC_DIRS :=
SRC_DIRS += ./
SRC_DIRS += ./wosi/
SRC_DIRS += ../
SRC_DIRS += ../dict/

##################################################################################
# 				strong recommend do NOT change anything below  		        	 #
##################################################################################
DETACHED := .detached
DEBUGINFO := .debuginfo
build := release
arch := IA64
SYSBITS := $(shell getconf LONG_BIT)

SRCS := $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)*.c))
INCS := $(addprefix -I, $(INC_DIRS))
CFLAGS := $(INCS) -Wall -std=gnu99 -D_GNU_SOURCE -fvisibility=hidden -fPIC -Wno-unused-function -D_FILE_OFFSET_BITS=64 -D_X64

MIN_GCC_VERSION = "4.9"
GCC_VERSION := "`$(CC) -dumpversion`"
IS_GCC_ABOVE_MIN_VERSION := $(shell expr "$(GCC_VERSION)" ">=" "$(MIN_GCC_VERSION)")
ifeq "$(IS_GCC_ABOVE_MIN_VERSION)" "1"
    CFLAGS += -fstack-protector-all -fstack-check=specific
else
    CFLAGS += -fstack-protector
endif

# COMPILE_TIME=$(shell date +" %Y-%m-%d %H:%M:%S")
COMPILE_TIME=$(shell date)
LDFLAGS := -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -pthread -lrt -ldl -lcrypt -shared
OBJCOPY := objcopy

ifeq ($(arch), i686)
	CFLAGS  += -m32 -D_X86
	LDFLAGS += -m32
endif

ifeq ($(arch), $(filter $(arch),arm arm32))
	CC := arm-seev100-linux-gnueabihf-gcc
	OBJCOPY := arm-seev100-linux-gnueabihf-objcopy
	CFLAGS  += -mfloat-abi=hard -mfpu=neon -D_ARM32
endif

ifeq ($(arch), $(filter $(arch),arm64 aarch64))
	CC := aarch64-linux-gnu-gcc
	OBJCOPY := aarch64-linux-gnu-objcopy
	CFLAGS += -D_ARM64
endif

ifeq ($(build),debug)
	CFLAGS += -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG=1
else
	CFLAGS += -O2 -g -funroll-loops -D NO_DEBUG #-fkeep-inline-functions -Winline
	LDFLAGS += -Wl,-O2
endif

# define the middle directory for build
BUILD_DIR := tmp
OBJS_DIR := $(BUILD_DIR)/objs
DEPS_DIR := $(BUILD_DIR)/deps

OBJS := $(patsubst %.c,%.o,$(SRCS))

$(TARGET):$(OBJS)
	@echo cc $@
	@$(CC) -o $@ $^ $(LDFLAGS)

%.o:%.c
	@echo cc $<
	@$(CC) -c $< $(CFLAGS) -o $@

-include $(DEPS)

PHONY := clean all install detach

.PHONY : $(PHONY)

all:
	$(TARGET)

clean:
	@echo 'cleanup project dir.'
	@rm -rf $(OBJS_DIR) $(DEPS_DIR) $(OBJS)
	@rm -f $(TARGET)

detach:
	$(OBJCOPY) --only-keep-debug $(PROGRAM) $(PROGRAM)$(DEBUGINFO)
	$(OBJCOPY) --strip-unneeded $(PROGRAM) $(PROGRAM)$(DETACHED)
