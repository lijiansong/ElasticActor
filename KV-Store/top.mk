-include common.mk

MESSAGE_HEADERS := $(patsubst %.pg,%-pg.h, $(MESSAGE_IDLS))
MESSAGE_C_FILES := $(patsubst %.pg,%-pg.c, $(MESSAGE_IDLS))

MODULE_DIRS := $(patsubst %,$(APP_SUBDIR)/%, $(MODULE_NAMES))
MODULES := $(patsubst %,$(APP_SUBDIR)/%/%.so, $(MODULE_NAMES)) 

all: module $(MESSAGE_HEADERS)

%-pg.h: %.pg 
	$(PIGEON) --c_out=. $<
	@sed -i '1s/^/#pragma GCC system_header\n/' $@

.PHONY: module_install module_clean clean 

module: $(MODULE_DIRS) $(MESSAGE_HEADERS)
	@for dir in $(MODULE_DIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

module_clean: $(MODULE_DIRS)
	@for dir in $(MODULE_DIRS); do \
		$(MAKE) -C $$dir clean || exit 1; \
	done

module_install: $(MODULE_DIRS)
	@for dir in $(MODULE_DIRS); do \
		$(MAKE) -C $$dir install || exit 1; \
	done
	@cp config/*.so ~/Test
	
clean: module_clean
	rm -f $(MESSAGE_HEADERS) $(MESSAGE_C_FILES)

