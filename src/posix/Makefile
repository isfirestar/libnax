PROGRAM := libnax.so

# specify the program version
VERSION := 1.0.1

# TARGET shall be the output file when success compile
TARGET := $(PROGRAM).$(VERSION)

# add include directory
INC_DIRS :=
INC_DIRS := ./
INC_DIRS += ../../include/
INC_DIRS += ../dict/

# add source directory, compiler shall compile all files which with $SRC_EXT in these folders
SRC_DIRS :=
SRC_DIRS += ./
SRC_DIRS += ./wosi/
SRC_DIRS += ../
SRC_DIRS += ../dict/

# add some source file which didn't in any of $SRC_DIRS
SRC_ADDON :=

# specify the extension of source file name
SRC_EXT := c

# TARGET_TYPE can be one of { dll/so, exe, lib/static } corresponding to { dynamic-library, executive-elf, static-archive }
TARGET_TYPE := dll

# BUILD can be one of { debug, release(default) } to change optimization pattern of this build
BUILD := debug

# specify the cross compiler prefix string (arm-seev100-linux-gnueabihf-)
CROSS_COMPILER_PREFIX := #arm-seev100-linux-gnueabihf-

# user define complie-time options
CFLAGS_ADDON := 

# use define link-time options
LDFALGS_ADDON := -pthread -lrt -ldl -lcrypt -lm

# target architecture, can be one of { X64/X8664/IA64/X86_64, X86/I386, ARM/ARM32, ARM64 }
ARCH := X64

######################################################################################################
#  strong recommend do not change any part of below unless you have understood this script very well #
######################################################################################################
DETACHED := .detached
DEBUGINFO := .debuginfo
SYSBITS := $(shell getconf LONG_BIT)

ifeq ($(filter $(ARCH), X86 x86 i386 X64 x64 x8664 X8664 x86_64 X86_64 IA64 ARM ARM32 arm arm32 ARM64),)
$(error unexpected ARCH: $(ARCH))
endif

ifeq ($(filter $(TARGET_TYPE), exe dll so lib),)
$(error unexpected TARGET_TYPE: $(TARGET_TYPE))
endif

ifeq ($(filter $(SRC_EXT), c cc cpp cxx),)
$(error unexpected SRC_EXT: $(SRC_EXT))
endif

SRCS := $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)*.$(SRC_EXT)))
SRCS += $(SRC_ADDON)
INCS := $(addprefix -I, $(INC_DIRS))

COMPILE_TIME := $(shell date +" %Y-%m-%d %H:%M:%S")
CFLAGS := $(INCS) -Wall -D_GNU_SOURCE -Wno-unused-function -D_BUILTIN_VERSION="\"$(VERSION) $(COMPILE_TIME)"\"
CFLAGS += $(CFLAGS_ADDON)

LDFLAGS := -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack

CC := $(CROSS_COMPILER_PREFIX)gcc
OBJCOPY := $(CROSS_COMPILER_PREFIX)objcopy
AR := $(CROSS_COMPILER_PREFIX)ar

COMPILER := 
ifeq ($(SRC_EXT), $(filter $(SRC_EXT), c C))
COMPILER := $(CROSS_COMPILER_PREFIX)gcc
endif

ifeq ($(SRC_EXT), $(filter $(SRC_EXT), cpp cc cxx))
COMPILER := $(CROSS_COMPILER_PREFIX)g++
endif

MIN_GCC_VERSION = "4.9"
GCC_VERSION := "`$(COMPILER) -dumpversion`"
IS_GCC_ABOVE_MIN_VERSION := $(shell expr "$(GCC_VERSION)" ">=" "$(MIN_GCC_VERSION)")
ifeq "$(IS_GCC_ABOVE_MIN_VERSION)" "1"
    CFLAGS += -fstack-protector-all -fstack-check=specific
else
    CFLAGS += -fstack-protector
endif

# select option to use
ifeq ($(TARGET_TYPE), exe)
	CFLAGS += -fPIE
endif

ifeq ($(TARGET_TYPE), $(filter $(TARGET_TYPE), dll so))
CFLAGS += -fPIC -fvisibility=hidden
LDFLAGS += -shared
endif

ifeq ($(BUILD), debug)
CFLAGS += -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG=1
else
CFLAGS += -O2 -g -funroll-loops -D NO_DEBUG -fkeep-inline-functions -Winline
LDFLAGS += -Wl,-O2
endif

ifeq ($(ARCH), $(filter $(ARCH), X86 x86 i386))
CFLAGS  += -m32 -D_X86 -D_FILE_OFFSET_BITS=32
LDFALGS_ADDON += -m32
endif

ifeq ($(ARCH), $(filter $(ARCH), X64 x64 x8664 X8664 IA64 X86_64 x86_64))
CFLAGS  += -D_X64 -D_X8664 -D_FILE_OFFSET_BITS=64
endif

ifeq ($(ARCH), $(filter $(ARCH), ARM ARM32 arm arm32))
CFLAGS  += -mfloat-abi=hard -mfpu=neon -D_ARM32 -D_FILE_OFFSET_BITS=32
endif

ifeq ($(ARCH), $(filter $(ARCH), ARM64))
CFLAGS  += -mfloat-abi=hard -mfpu=neon -D_ARM64 -D_FILE_OFFSET_BITS=64
endif

ifeq ($(SRC_EXT), $(filter $(SRC_EXT), c C))
CFLAGS += -std=gnu99
CPPFLAGS :=
endif

ifeq ($(SRC_EXT), $(filter $(SRC_EXT), cpp cc cxx))
CFLAGS += -std=c++11
CPPFLAGS := $(CFLAGS)
CFLAGS :=
endif

LDFLAGS += $(LDFALGS_ADDON)

# define the middle directory for build
BUILD_DIR := gbuild
OBJS_DIR := $(BUILD_DIR)/objs
DEPS_DIR := $(BUILD_DIR)/deps
DISA_DIR := $(BUILD_DIR)/disa
TAGS_DIR := $(BUILD_DIR)/bin/

VPATH := $(SRC_DIRS)
OBJS := $(addprefix $(OBJS_DIR)/,$(patsubst %.$(SRC_EXT),%.o,$(notdir $(SRCS))))
DEPS := $(addprefix $(DEPS_DIR)/,$(patsubst %.$(SRC_EXT),%.d,$(notdir $(SRCS))))
DISA := $(addprefix $(DISA_DIR)/,$(patsubst %.$(SRC_EXT),%.s,$(notdir $(SRCS))))

ifeq ($(TARGET_TYPE), $(filter $(TARGET_TYPE), lib static))
$(TAGS_DIR)$(TARGET):$(OBJS)
	@if [ ! -d $(TAGS_DIR) ]; then mkdir -p $(TAGS_DIR); fi;
	@echo == AR $@
	$(CROSS_COMPILER_PREFIX)ar crv $@ $^
else
$(TAGS_DIR)$(TARGET):$(OBJS)
	@if [ ! -d $(TAGS_DIR) ]; then mkdir -p $(TAGS_DIR); fi;
	@echo == CC $@
	@$(COMPILER) -o $@ $^ $(LDFLAGS)
endif

$(OBJS_DIR)/%.o:%.$(SRC_EXT)
	@if [ ! -d $(OBJS_DIR) ]; then mkdir -p $(OBJS_DIR); fi;
	@echo == CC -c $@
	@$(COMPILER) -c $< $(CFLAGS) $(CPPFLAGS) -o $@

$(DEPS_DIR)/%.d:%.$(SRC_EXT)
	@if [ ! -d $(DEPS_DIR) ]; then mkdir -p $(DEPS_DIR); fi;
	@echo == CC -MM $@;
	@set -e; rm -f $@;\
	$(COMPILER) -MM $(CFLAGS) $(CPPFLAGS) $< > $@.$$$$;\
	sed 's,\($*\)\.o[ :]*,$(OBJS_DIR)/\1.o $@ : ,g' < $@.$$$$ > $@;\
	rm -f $@.$$$$
	
$(DISA_DIR)/%.s:%.$(SRC_EXT)
	@if [ ! -d $(DISA_DIR) ]; then mkdir -p $(DISA_DIR); fi;
	@echo == CC -S $@;
	set -e; rm -f $@;\
	$(COMPILER) -S $< $(CFLAGS) $(CPPFLAGS) -o $@

PHONY := clean all install detach

.PHONY : $(PHONY)

all:
	$(TAGS_DIR)$(TARGET)

clean:
	@echo == cleanup build dir
	@set -e;rm -rf $(BUILD_DIR)

detach:
	@echo == gcc detach debug info to $(TAGS_DIR)$(TARGET)$(DEBUGINFO)
	@$(OBJCOPY) --only-keep-debug $(TAGS_DIR)$(TARGET) $(TAGS_DIR)$(TARGET)$(DEBUGINFO)
	@$(OBJCOPY) --strip-unneeded $(TAGS_DIR)$(TARGET) $(TAGS_DIR)$(TARGET)$(DETACHED)

ifneq ($(MAKECMDGOALS), clean)
-include $(DEPS)
endif
