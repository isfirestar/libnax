TARGET=demo
VERSION=1.1.0
DATE:=$(shell date -I)
build=release
arch=Ia64

RM=rm

SRCS+=$(wildcard ./*.c)
SRCS+=$(wildcard ../src/*.c)
SRCS+=$(wildcard ../src/posix/*.c)
SRCS+=$(wildcard ../src/posix/wosi/*.c)

INC := -I ./
INC += -I ../include/
INC += -I ../src/posix/

CFLAGS := $(INC) -Wall -std=gnu99 -D_GNU_SOURCE -Wno-unused-function -D_FILE_OFFSET_BITS=64
LDFLAGS:=-ldl -lpthread -lrt -lcrypt -fPIE

ifeq ($(build),debug)
	CFLAGS += -g3 -fstack-protector-all -fstack-check=specific -fno-omit-frame-pointer -fno-optimize-sibling-calls
	LDFLAGS += -Wl,-g
else
	CFLAGS += -O2
	LDFLAGS += -Wl,-O2
endif

OBJS=$(patsubst %.c,%.o,$(SRCS))
COMPILER=cc
CC=$(COMPILER)
OBJCOPY=objcopy

ifeq ($(arch),arm)
    CC=arm-linux-gnueabihf-gcc
	CFLAGS+=-mfloat-abi=hard -mfpu=neon
endif

ifeq ($(arch), $(filter $(arch),arm64 aarch64))
    CC=aarch64-linux-gnu-gcc
endif



all:$(TARGET)

$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o:%.c
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	$(RM) -f $(OBJS) $(TARGET)


.PHONY:clean all install
