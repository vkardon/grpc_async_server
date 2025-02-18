# Top-level Makefile
SUBDIRS = basic \
          complete \
          router 

# Default target: build all subdirectories
all: $(SUBDIRS)

# Recursive rule to enter each subdirectory and run make
$(SUBDIRS):
	$(MAKE) -C $@  # -C changes directory before running make

# Clean rule to clean all subdirectories
clean:
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done

# Phony targets to prevent issues with files named like targets
.PHONY: all clean $(SUBDIRS)

