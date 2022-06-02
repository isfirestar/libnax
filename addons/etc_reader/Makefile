PROGRAM=etcr
VERSION=

# ELFTYPE canbe one of : app/dynamic
ELFTYPE := app

ROOTDIR := ../../

TARGET := $(PROGRAM)$(VERSION)
INC_DIRS :=
INC_DIRS := ./
INC_DIRS += $(ROOTDIR)include/

SRC_DIRS :=
SRC_DIRS += ./
SRC_DIRS += $(ROOTDIR)src/
SRC_DIRS += $(ROOTDIR)src/posix/
SRC_DIRS += $(ROOTDIR)src/posix/wosi/

CC_ADDONS := -D_TEST=1
LD_ADDONS :=

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

CFLAGS := $(INCS) -Wall -std=gnu99 -D_GNU_SOURCE -fvisibility=hidden -Wno-unused-function -D_FILE_OFFSET_BITS=64
CFLAGS += $(CC_ADDONS)

ifeq "$(ELFTYPE)" "app"
	CFLAGS += -fPIE
else
	ifeq "$(ELFTYPE)" "dynamic"
		CFLAGS += -fPIC
	endif
endif

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

# LDFLAGS for gcc build
LDFLAGS := -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
LDFLAGS += $(LD_ADDONS)

ifeq "$(ELFTYPE)" "app"
	LDFLAGS += -pthread -lrt -ldl -lcrypt
else
	ifeq "$(ELFTYPE)" "dynamic"
		LDFLAGS += -shared
	endif
endif

OBJCOPY := objcopy

ifeq ($(arch), i686)
	CFLAGS  += -m32
	LDFLAGS += -m32
endif

ifeq ($(arch), $(filter $(arch),arm arm32))
	CC := arm-seev100-linux-gnueabihf-gcc
	OBJCOPY := arm-seev100-linux-gnueabihf-objcopy
	CFLAGS  += -mfloat-abi=hard -mfpu=neon
endif

ifeq ($(arch), $(filter $(arch),arm64 aarch64))
	CC := aarch64-linux-gnu-gcc
	OBJCOPY := aarch64-linux-gnu-objcopy
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
