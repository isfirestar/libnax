PROGRAM := redis-rax

SOLUTION_DIR := ../

PROJECT_DIR := $(SOLUTION_DIR)rax/

# specify the program version
VERSION := 1.0.0

# TARGET shall be the output file when success compile
TARGET := $(PROGRAM)

# add include directory, path MUST end with slash
INC_DIRS := $(PROJECT_DIR) $(SOLUTION_DIR)public/

# add include directory, this variable allow framework force traverse and include all head files in entire directoy and it's sub directory
# path MUST end with slash
INC_ENTIRE_DIRS :=

# add source directory, compiler shall compile all files which with $(SRC_SUFFIX) in these folders, path MUST end with slash
SRC_DIRS := $(PROJECT_DIR) 

# add source directory, this variable allow framework force traverse and include all source files in entire directoy and it's sub directory
# path MUST end with slash
SRC_ENTIRE_DIRS :=

# add some source file which didn't in any of $(SRC_DIRS)
SRC_ADDON := $(SOLUTION_DIR)public/zmalloc.c

# exclude some source file which maybe in one of $(SRC_DIRS)
SRC_EXCLUDE :=

# specify the extension of source file name, can be one or more of (c/cc/cpp/cxx)
SRC_SUFFIX := c

# $(TARGET_TYPE) can be one of (dll/so, exe/app, lib/archive) corresponding to (dynamic-library, executive-elf, static-archive)
TARGET_TYPE := app

# $(BUILD) can be one of (debug, release) to change optimization pattern of this build, default to (release)
BUILD := debug

# target architecture, can be one of  (X64/X8664/IA64/X86_64, X86/I386, ARM/ARM32, ARM64)
ARCH := X64

# specify the cross compiler prefix string (arm-seev100-linux-gnueabihf-)
CROSS_COMPILER_PREFIX :=

# user define complie-time options
CFLAGS_ADDON := 

# user define link-time options
LDFALGS_ADDON := -pthread -lm

# ifeq ($(ARCH), $(filter $(ARCH), ARM arm))
# LDFALGS_ADDON += -L $(DAVINCI_DIR)libs/libs_h5/ -lhal_bsp
# endif

# directory to save intermediate file and output target ( ./build/ by default), path MUST end with slash
BUILD_DIR :=

# invoke other makefile or subdirs, using this field in the top make entry
INVOKE :=

# both of below variable use to help you run shell script before/after gcc linker executive
PRE_LINK_ORDER =
POST_LINK_ORDER =

# finally, we MUST include make framework to complete the job
include pattern.mk

