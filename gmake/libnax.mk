PROGRAM := libnax.so
SOLUTION_DIR := ../

# specify the program version
VERSION := 1.0.1

# TARGET shall be the output file when success compile
TARGET := $(PROGRAM).$(VERSION)

# add include directory, path MUST end with slash
INC_DIRS := 
INC_DIRS += $(SOLUTION_DIR)include/

# add include directory, this variable allow framework force traverse and include all head files in entire directoy and it's sub directory
# path MUST end with slash
INC_ENTIRE_DIRS :=

# add source directory, compiler shall compile all files which with $(SRC_SUFFIX) in these folders, path MUST end with slash
SRC_DIRS := $(SOLUTION_DIR)src/

# add source directory, this variable allow framework force traverse and include all source files in entire directoy and it's sub directory
# path MUST end with slash
SRC_ENTIRE_DIRS :=  $(SOLUTION_DIR)src/posix

# add some source file which didn't in any of $(SRC_DIRS)
SRC_ADDON :=

# exclude some source file which maybe in one of $(SRC_DIRS)
SRC_EXCLUDE :=

# specify the extension of source file name, can be one or more of (c/cc/cpp/cxx)
SRC_SUFFIX := c cpp

# $(TARGET_TYPE) can be one of (dll/so, exe/app, lib/archive) corresponding to (dynamic-library, executive-elf, static-archive)
TARGET_TYPE := dll

# $(BUILD) can be one of (debug, release) to change optimization pattern of this build, default to (release)
BUILD := debug

# specify the cross compiler prefix string (arm-seev100-linux-gnueabihf-)
CROSS_COMPILER_PREFIX :=

# user define complie-time options
CFLAGS_ADDON := -D__USE_MISC

# user define link-time options
LDFALGS_ADDON := -ldl

# target architecture, can be one of  (X64/X8664/IA64/X86_64, X86/I386, ARM/ARM32, ARM64)
ARCH := X64

# directory to save intermediate file and output target ( ./build/ by default), path MUST end with slash
BUILD_DIR :=

# sub directory which you want to build relating by this make. using this as the top make entry
SUB_DIRS :=

# other makefile which you want to invoke
INVOKE_MK :=

# both of below variable use to help you run shell script before/after gcc linker executive
PRE_LINK_ORDER =
POST_LINK_ORDER = ln -sf $(TAGS_DIR)$(TARGET) $(PROGRAM)

# finally, we MUST include make framework to complete the job
include  pattern.mk
