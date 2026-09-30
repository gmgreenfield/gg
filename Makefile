# Short commands for the CMake presets; CMake remains the build system.
CMAKE ?= cmake
CTEST ?= ctest
JOBS ?= 2
INSTALL_PREFIX ?= $(HOME)/.local

.PHONY: all release debug test test-debug clean clean-debug format format-check install help configure-release configure-debug

all: release

configure-release:
	$(CMAKE) --preset release

configure-debug:
	$(CMAKE) --preset debug

release: configure-release
	$(CMAKE) --build --preset release --parallel $(JOBS)

debug: configure-debug
	$(CMAKE) --build --preset debug --parallel $(JOBS)

test: release
	$(CTEST) --preset release

test-debug: debug
	$(CTEST) --preset debug

clean: configure-release
	$(CMAKE) --build --preset clean

clean-debug: configure-debug
	$(CMAKE) --build build/debug --target clean

format-check: configure-release
	$(CMAKE) --build --preset format-check

format: configure-release
	$(CMAKE) --build --preset format

install: release
	$(CMAKE) --install build/release --prefix "$(INSTALL_PREFIX)"

help:
	@printf '%s\n' \
		'make              Build Release' \
		'make debug        Build Debug' \
		'make test         Build and test Release' \
		'make test-debug   Build and test Debug' \
		'make clean        Clean Release build' \
		'make clean-debug  Clean Debug build' \
		'make format-check Check formatting without changes' \
		'make format       Format C source and header files' \
		'make install      Install to ~/.local/bin' \
		'make help         Show these commands'
