IDL_SUBDIR := message
APP_SUBDIR := app

## modify this if your mola runtime install in other place
RT_PATH := /usr/local/
## modify this if your zookeeper c api library install in other place
ZK_PATH ?= /usr/local/

ZK_LDFLAGS := -L$(ZK_PATH)/lib -lzookeeper_mt

## make sure this command include in your PATH env
PIGEON := pigeon
MOCC := mocc
MOCHECKER := mochecker

RT_LOADER := $(RT_PATH)/bin/moloader

## add some additional options if neccessary
MODULE_CFLAGS  += -I. -I$(RT_PATH)/include -fPIC -shared

MODULE_LDFLAGS += -L$(RT_PATH)/lib -lmola_io -lmola_core -lpthread 
MODULE_LDFLAGS += $(ZK_LDFLAGS)

## test the endianness of current machine
LITTLE_ENDIAN := $(shell python -c "from struct import pack;import sys;sys.exit(int(pack('@h',1)==pack('<h',1)))"; echo $$?)

MODULE_CFLAGS += -fno-omit-frame-pointer

## tell pigeon runtime the endianness
ifeq ($(LITTLE_ENDIAN),0)
	MODULE_CFLAGS += -DWORDS_BIGENDIAN 
endif

MOLA_DEBUG_BUILD ?= 1

## setting the debug or release 
ifeq ($(MOLA_DEBUG_BUILD),1)
	MODULE_CFLAGS += -g3 
else
	MODULE_CFLAGS += -O2 -fno-omit-frame-pointer
endif
