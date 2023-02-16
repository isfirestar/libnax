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
CROSS_COMPILER_PREFIX := #arm-seev100-linux-gnueabihf-

# user define complie-time options
CFLAGS_ADDON :=

# user define link-time options
LDFALGS_ADDON :=

# target architecture, can be one of  (X64/X8664/IA64/X86_64, X86/I386, ARM/ARM32, ARM64)
ARCH :=

# directory to save intermediate file and output target ( ./gbuild/ by default), path MUST end with slash
BUILD_DIR :=

# sub directory which you want to build relating by this make. using this as the top make entry
COMPONENT := ./src/posix/ ./demo/

#################################################################################################
#      build framework, you didn't need to understand what below script have done 		#
#################################################################################################
DETACHED := .detached
DEBUGINFO := .debuginfo
CC := $(CROSS_COMPILER_PREFIX)gcc
OBJCOPY := $(CROSS_COMPILER_PREFIX)objcopy
AR := $(CROSS_COMPILER_PREFIX)ar

# ld options
LDFLAGS := -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
ifeq ($(TARGET_TYPE), $(filter $(TARGET_TYPE), dll so))
LDFLAGS += -shared
endif
ifneq ($(BUILD), debug)
LDFLAGS += -Wl,-O2
endif
ifeq ($(ARCH), $(filter $(ARCH), X86 x86 i386))
LDFLAGS += -m32
endif
LDFLAGS += $(LDFALGS_ADDON)

# compile options
CFLAGS :=
COMPILE_TIME := $(shell date +" %Y-%m-%d %H:%M:%S")
CFLAGS += $(INCS) -Wall -D_GNU_SOURCE -Wno-unused-function -D_BUILTIN_VERSION="\"$(VERSION) $(COMPILE_TIME)"\"
ifeq ($(TARGET_TYPE), $(filter $(TARGET_TYPE), exe app))
CFLAGS += -fPIE
endif
ifeq ($(TARGET_TYPE), $(filter $(TARGET_TYPE), dll so))
CFLAGS += -fPIC -fvisibility=hidden
endif
MIN_GCC_VERSION = "4.9"
GCC_VERSION := "`$(CC) -dumpversion`"
IS_GCC_ABOVE_MIN_VERSION := $(shell expr "$(GCC_VERSION)" ">=" "$(MIN_GCC_VERSION)")
ifeq "$(IS_GCC_ABOVE_MIN_VERSION)" "1"
STACK_PROT = -fstack-protector-all -fstack-check=specific
else
STACK_PROT = -fstack-protector
endif
ifeq ($(BUILD), debug)
CFLAGS += -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG=1 $(STACK_PROT)
else
CFLAGS += -O2 -g -funroll-loops -D NO_DEBUG -fkeep-inline-functions -Winline
endif
ifeq ($(ARCH), $(filter $(ARCH), X86 x86 i386))
CFLAGS  += -m32 -D_X86 -D_FILE_OFFSET_BITS=32
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

# all includes
INCS := $(addprefix -I, $(INC_DIRS))
INCS += $(foreach i,$(INC_ENTIRE_DIRS),$(addprefix -I,$(shell find $(i) -type d -exec echo {}/ \;)))

#src-$1 = $(foreach d,$2,$(wildcard $d*.$1))
define set_src_x
src-$1 = $(filter-out $4,$(foreach d,$2,$(wildcard $d*.$1)) $(filter %.$1,$3))

endef

define set_obj_x
obj-$1 = $(patsubst %.$1,$3%.o,$(notdir $2))

endef

ifeq ($(BUILD_DIR),)
BUILD_DIR := gbuild
endif

# define the middle directory for build
OBJS_DIR := $(BUILD_DIR)objs/
TAGS_DIR := $(BUILD_DIR)bin/

# expand SRC_DIRS
SRC_DIRS += $(foreach i,$(SRC_ENTIRE_DIRS),$(shell find $(i) -type d -exec echo {}/ \;))

$(eval $(foreach i,$(SRC_SUFFIX),$(call set_src_x,$i,$(SRC_DIRS),$(SRC_ADDON),$(SRC_EXCLUDE))))
$(eval $(foreach i,$(SRC_SUFFIX),$(call set_obj_x,$i,$(src-$i),$(OBJS_DIR))))

VPATH := $(SRC_DIRS)
OBJS = $(foreach i,$(SRC_SUFFIX),$(obj-$i))
SRCS = $(foreach i,$(SRC_SUFFIX),$(src-$i))

PHONY := clean .mkdir .component all detach

all: .mkdir .component $(TAGS_DIR)$(TARGET)

define build_obj_x
$$(obj-$1): $2%.o: %.$1  $(MAKEFILE_LIST)
	@echo compiling $$<
	@$(CC) -Wp,-MT,$$@ -Wp,-MMD,$$@.d -c $$< $(INCS) $(CFLAGS) $(CFLAGS_ADDON) $3 -o $$@

endef
$(eval $(foreach i,$(SRC_SUFFIX),$(call build_obj_x,$i,$(OBJS_DIR),$(if $(filter $i,cpp cc cxx),-std=c++11,-std=gnu99))))

ifeq ($(TARGET_TYPE), $(filter $(TARGET_TYPE), lib static))
$(TAGS_DIR)$(TARGET): $(OBJS)
	@echo archiving $@
	@$(AR) crv $@ $^
else
LDFLAGS += $(if $(strip $(src-cpp) $(src-cc) $(src-cxx)), -lstdc++)
$(TAGS_DIR)$(TARGET): $(OBJS)
	@echo linking $@
	@$(CC) -o $@ $^ $(LDFLAGS)
endif

detach:
	$(OBJCOPY) --only-keep-debug $(TAGS_DIR)$(TARGET) $(TAGS_DIR)$(TARGET)$(DEBUGINFO)
	$(OBJCOPY) --strip-unneeded $(TAGS_DIR)$(TARGET) $(TAGS_DIR)$(TARGET)$(DETACHED)

.mkdir:
	@if [ ! -d $(OBJS_DIR) ]; then mkdir -p $(OBJS_DIR); fi
	@if [ ! -d $(TAGS_DIR) ]; then mkdir -p $(TAGS_DIR); fi

.component:
  @for i in $(COMPONENT); do make -C $$i; done

clean:
	@echo cleaning project.
	@rm -fr $(BUILD_DIR)
  @for i in $(COMPONENT); do make -C $$i clean; done
  
.PHONY : $(PHONY)

# ifneq ($(MAKECMDGOALS), clean)
# -include $(DISS)
# endif

