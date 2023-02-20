PROGRAM :=

# specify the program version
VERSION :=

# TARGET shall be the output file when success compile
TARGET :=

# add include directory, path MUST end with slash
INC_DIRS :=

# add include directory, this variable allow framework force traverse and include all head files in entire directoy and it's sub directory
# path MUST end with slash
INC_ENTIRE_DIRS :=

# add source directory, compiler shall compile all files which with $(SRC_SUFFIX) in these folders, path MUST end with slash
SRC_DIRS :=

# add source directory, this variable allow framework force traverse and include all source files in entire directoy and it's sub directory
# path MUST end with slash
SRC_ENTIRE_DIRS := 

# add some source file which didn't in any of $(SRC_DIRS)
SRC_ADDON :=

# exclude some source file which maybe in one of $(SRC_DIRS)
SRC_EXCLUDE :=

# specify the extension of source file name, can be one or more of (c/cc/cpp/cxx)
SRC_SUFFIX :=

# $(TARGET_TYPE) can be one of (dll/so, exe/app, lib/archive) corresponding to (dynamic-library, executive-elf, static-archive)
TARGET_TYPE :=

# $(BUILD) can be one of (debug, release) to change optimization pattern of this build, default to (release)
BUILD :=

# specify the cross compiler prefix string (arm-seev100-linux-gnueabihf-)
CROSS_COMPILER_PREFIX :=

# user define complie-time options
CFLAGS_ADDON :=

# user define link-time options
LDFALGS_ADDON :=

# target architecture, can be one of  (X64/X8664/IA64/X86_64, X86/I386, ARM/ARM32, ARM64)
ARCH :=

# directory to save intermediate file and output target ( ./build/ by default), path MUST end with slash
BUILD_DIR :=
 
# invoke other makefile or subdirs, using this field in the top make entry
INVOKE := libnax.mk demo.mk tester.mk demo-c++.mk

# both of below variable use to help you run shell script before/after gcc linker executive
PRE_LINK_ORDER =
POST_LINK_ORDER =

include pattern.mk

