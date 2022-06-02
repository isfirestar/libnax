TARGET=demo
VERSION=1.1.0
DATE:=$(shell date -I)
build=release
arch=Ia64

RM=rm

SRCS+=$(wildcard ./*.cpp)
SRCS+=$(wildcard ../*.cpp)

INC := -I ./
INC += -I ../
INC += -I ../../include/

CPPFLAGS := $(INC) -Wall -std=c++11 -D_GNU_SOURCE -Wno-unused-function -D_FILE_OFFSET_BITS=64
LDFLAGS:=-ldl -lpthread -lrt -lcrypt -fPIE -L ./ -lnax

ifeq ($(build),debug)
	CPPFLAGS += -g3 -fstack-protector-all -fstack-check=specific -fno-omit-frame-pointer -fno-optimize-sibling-calls
	LDFLAGS += -Wl,-g
else
	CPPFLAGS += -O2
	LDFLAGS += -Wl,-O2
endif

OBJS=$(patsubst %.cpp,%.o,$(SRCS))
COMPILER=g++
CC=$(COMPILER)
OBJCOPY=objcopy

ifeq ($(arch),arm)
    CC=arm-linux-gnueabihf-gcc
	CPPFLAGS+=-mfloat-abi=hard -mfpu=neon
endif

ifeq ($(arch), $(filter $(arch),arm64 aarch64))
    CC=aarch64-linux-gnu-gcc
endif

all:$(TARGET)

$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o:%.c
	$(CC) -c $< $(CPPFLAGS) -o $@

clean:
	$(RM) -f $(OBJS) $(TARGET)


.PHONY:clean all install
