PROGRAM=nshost.so
VERSION=.9.9.1
DETACHED=.detached
DEBUGINFO=.debuginfo

TARGET := $(PROGRAM)$(VERSION)
build := release
arch := IA64
SRC_EXT := c
SYS_WIDTH := $(shell getconf LONG_BIT)
OBJCOPY := objcopy

SRCDIR := ./
SRCDIR += ../libnsp/common/
SRCDIR += ../libnsp/posix/

FILES := $(foreach dir, $(SRCDIR), $(wildcard $(dir)*.$(SRC_EXT)))
# arp && raw socket are not in my plan
# SRCS := $(filter-out ./arp.c ./arpio.c, $(FILES))

INC_DIR := ../libnsp/include/
INC_DIR += ./
INCS := $(addprefix -I, $(INC_DIR))

CFLAGS += $(INC_DIR) -fPIC -Wall -std=gnu99 -ansi -D_GNU_SOURCE -fvisibility=hidden -Wno-unused-function

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
# GIT_COMMIT_ID=$(shell git rev-parse HEAD)
# LDFLAGS=-shared -lcrypt -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-soname,"$(TARGET) $(COMPILE_TIME)"
LDFLAGS := -shared -lcrypt -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack

INSTALL_DIR := /usr/local/lib64/

ifeq ($(arch), i686)
	CFLAGS += -m32
	LDFLAGS += -m32
	INSTALL_DIR := /usr/local/lib/
endif

ifeq ($(arch), $(filter $(arch),arm arm32))
	CC := arm-linux-gnueabihf-gcc
	OBJCOPY := arm-linux-gnueabihf-objcopy
	CFLAGS += -mfloat-abi=hard -mfpu=neon
	INSTALL_DIR := /usr/local/lib/
endif

ifeq ($(arch), $(filter $(arch),arm64 aarch64))
	CC := aarch64-linux-gnu-gcc
	OBJCOPY := aarch64-linux-gnu-objcopy
	INSTALL_DIR := /usr/local/lib/aarch64-linux-gnu/
endif

ifeq ($(build),debug)
	CFLAGS += -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG
else
	CFLAGS += -O2 -g -funroll-loops -fkeep-inline-functions -Winline -DNO_DEBUG
	LDFLAGS += -Wl,-O2
endif

# define the middle directory for build
BUILD_DIR := tmp
OBJS_DIR := $(BUILD_DIR)/objs
DEPS_DIR := $(BUILD_DIR)/deps
# determine the complier target directorys
#DIRS=./ $(PLATFORM_SRCDIR)
# add all $(DIRS) into vpath variable
#VPATH = $(DIRS)
# construct the object output files

#OBJS := $(addprefix $(OBJS_DIR)/,$(patsubst %.$(SRC_EXT),%.o,$(notdir $(SRCS))))
OBJS := $(patsubst %.c,%.o,$(SRCS))
#DEPS := $(addprefix $(DEPS_DIR)/,$(patsubst %.$(SRC_EXT),%.d,$(notdir $(SRCS))))

$(TARGET):$(PROGRAM)
	cp -f $(PROGRAM) $(TARGET)

$(PROGRAM):$(OBJS)
	@echo cc  $@
	@$(CC) -o $@ $^ $(LDFLAGS)

%.o:%.c
	@echo cc -c $<
	@$(CC) -c $< $(CFLAGS) -o $@

# $(OBJS_DIR)/%.o:%.$(SRC_EXT)
# 	@if [ ! -d $(OBJS_DIR) ]; then mkdir -p $(OBJS_DIR); fi;
# 	@if [ 'mxx.c' = $<  ]; then sed -i "s/CompileDateOfProgramDefinition-ChangeInMakefile/$(COMPILE_TIME)/g" $<; fi;
# 	$(CC) -c $< $(CFLAGS) -o $@
# 	@if [ 'mxx.c' = $<  ]; then sed -i "s/$(COMPILE_TIME)/CompileDateOfProgramDefinition-ChangeInMakefile/g" $<; fi;

# $(DEPS_DIR)/%.d:%.$(SRC_EXT)
# 	@if [ ! -d $(DEPS_DIR) ]; then mkdir -p $(DEPS_DIR); fi;
# 	set -e; rm -f $@;\
# 	$(CC) -MM $(CFLAGS) $< > $@.$$$$;\
# 	sed 's,\($*\)\.o[ :]*,$(OBJS_DIR)/\1.o $@ : ,g' < $@.$$$$ > $@;\
# 	rm -f $@.$$$$

-include $(DEPS)

PHONY := clean all install detach

.PHONY : $(PHONY)

all:
	$(TARGET)

clean:
	#@echo cleanup project
	@rm -rf $(OBJS_DIR) $(DEPS_DIR) $(OBJS)
	@rm -f $(PROGRAM)*
	@rm -f ./*.o

install:
	install -m644 $(TARGET) $(INSTALL_DIR)
	ln -sf $(INSTALL_DIR)$(TARGET) $(INSTALL_DIR)$(PROGRAM)

detach:
	# save debuginfo to another file:
	# objcopy --only-keep-debug program program.sym
	# strip debuginfo from execute file:(program.bin doesn't have debug infos)
	# objcopy --strip-debug program program.bin
	# using both program file and it's matched debug info to run gdb:
	# gdb -e program -s program.sym
	$(OBJCOPY) --only-keep-debug $(PROGRAM) $(PROGRAM)$(DEBUGINFO)
	$(OBJCOPY) --strip-unneeded $(PROGRAM) $(PROGRAM)$(DETACHED)

# 1 compile:
# gcc -g -o foo foo.c
# 2 detach independing debug info file:
# objcopy --only-keep-debug foo foo.dbg
# 3 strip debug info from debug-program file:
# objcopy --strip-debug foo
# 4 append debug info to program file:
# objcopy --add-gnu-debuglink=foo.dbg foo
# 5 determine debug info:
# objdump -s -j .gnu_debuglink foo
#
# before add debuginfo to program:
#	zhuoyunzhi@WINDOWS-GANN0FE:~/test/gdbinfo$ objdump -s -j .gnu_debuglink program.bin
#	program.bin:     file format elf64-x86-64
#	objdump: section '.gnu_debuglink' mentioned in a -j option, but not found in any input file

# after add debuginfo to program:
#   zhuoyunzhi@WINDOWS-GANN0FE:~/test/gdbinfo$ objdump -s -j .gnu_debuglink program.bin
#   program.bin:     file format elf64-x86-64
#   Contents of section .gnu_debuglink:
#   0000 70726f67 72616d2e 73796d00 1fbec464  program.sym....d
#
# readelf Key to Flags:
#  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),
#  L (link order), O (extra OS processing required), G (group), T (TLS),
#  C (compressed), x (unknown), o (OS specific), E (exclude),
#  l (large), p (processor specific)#
