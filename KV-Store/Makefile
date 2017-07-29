## Comment this line if build as debug target!!
MOLA_DEBUG_BUILD := 0

-include common.mk

## Edit this to add all message IDL files here!!
MESSAGE_IDLS := $(shell ls $(IDL_SUBDIR)/*.pg)

## Edit this to add all modules' names here!!
## NOTE: These are modules' names, not module spec files' name!!
## 	     All desc and implement files for each module 
##		 MUTST located in a subdir of './app/' 
##		 with the same name with its moudle name !!
MODULE_SUBDIRS := $(shell ls app/)

MODULE_NAMES := $(patsubst app/%,%, $(MODULE_SUBDIRS))

-include top.mk

