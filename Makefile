
ifeq "$(MAKECMDGOALS)" ""
TARGET = all 
else
TARGET = $(MAKECMDGOALS)
endif

.PHONY: $(TARGET)
$(TARGET):
	@echo ">>> Building '$(TARGET)'..."
	@cd example && $(MAKE) $(TARGET)


