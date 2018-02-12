# warning options for GCC
WARNINGS = -Wall -Wno-unused-result -Wc++-compat -Wshadow -Winit-self -Wextra -Wunused -Wunreachable-code -Wredundant-decls -Wswitch-default
# source files for compiling
SOURCE = src/*.c src/gfx/*.c
# executable file name
TARGETNAME = protracker
# executable file path
TARGET = release/$(TARGETNAME)
# temporary files (for deleting)
CLEAN = src/*.o src/gfx/*.o
# install path
INSTALL = /usr/local/bin/
# name of the user running the make
USER = $(shell whoami)

.PHONY: clean cleanall uninstall

$(TARGET): $(SOURCE)
	@echo "Compiling, please wait..."
	gcc  $(SOURCE) -lSDL2 -lm $(WARNINGS) -march=native -mtune=native -O3 -o $(TARGET)
	@echo "Done! The binary (protracker) is in the folder named 'release'."
	@echo "To run it, type ./protracker in the release folder (or type make run)."

run: $(TARGET)
	$(TARGET)

clean:
	@echo "Deleting temporary files..."
	rm $(CLEAN) 2> /dev/null || true

cleanall: clean
	@echo "Deleting executable..."
	rm $(TARGET) 2> /dev/null || true

install: $(TARGET)
	@if [ "$(USER)" = "root" ]; then \
		echo "Copying '$(TARGETNAME)' to $(INSTALL)"; \
		install -m 775 $(TARGET) "$(INSTALL)/$(TARGETNAME)"; \
	else echo "Please run 'make install' as root"; fi

uninstall:
	@if [ "$(USER)" = "root" ]; then \
		echo "Deleting '$(TARGETNAME)' from $(INSTALL)"; \
		rm "$(INSTALL)/$(TARGETNAME)"; \
	else echo "Please run 'make uninstall' as root"; fi

