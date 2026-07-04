# Top-level Makefile for cxadc-linux
#
# Layout:
#   src/kernel/          kernel module sources (built in place by Kbuild)
#   src/tools/common/    shared userland library (libcxadc)
#   src/tools/<tool>/    individual userland tools
#   src/scripts/         convenience shell scripts
#   config/              modprobe / udev / systemd files
#   build/               all userland build artifacts (git-ignored)
#
# The kernel build system (Kbuild) cannot emit objects outside the module
# source directory, so kernel artifacts live under src/kernel/ (git-ignored)
# and the resulting cxadc.ko is copied into build/ for convenience.

CONFIG_MODULE_SIG ?= n
KDIR    ?= /lib/modules/$(shell uname -r)/build
CC      ?= cc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=
INSTALL ?= install

# udev/modprobe locations (overridable for packaging)
MODPROBE_DIR ?= /etc/modprobe.d
UDEV_DIR     ?= /etc/udev/rules.d

# Directories
SRC_KERNEL  := src/kernel
SRC_TOOLS   := src/tools
SRC_SCRIPTS := src/scripts
CONFIG_DIR  := config
BUILD_DIR   := build
BUILD_BIN   := $(BUILD_DIR)/bin
BUILD_OBJ   := $(BUILD_DIR)/obj

# Userland compile flags (kept separate from kernel Kbuild flags)
WARNINGS    := -Wall -Wextra
OPT         ?= -O2
TOOL_CFLAGS := $(OPT) $(WARNINGS) -pthread -I$(SRC_TOOLS)/common $(CFLAGS)
TOOL_LDLIBS := -pthread -lm

# Shared library sources
COMMON_SRCS := \
	$(SRC_TOOLS)/common/utils.c \
	$(SRC_TOOLS)/common/cx_analyze.c \
	$(SRC_TOOLS)/common/cx_clockgen.c
COMMON_OBJS := $(patsubst %.c,$(BUILD_OBJ)/%.o,$(COMMON_SRCS))

# Userland tools (C)
TOOLS := leveladj levelmon cx-capture

.PHONY: all module tools clean distclean \
        install install-module install-tools install-scripts install-config \
        uninstall modules_install help

all: module tools

help:
	@echo "cxadc-linux build targets:"
	@echo "  make                 build kernel module + userland tools"
	@echo "  make module          build the kernel module (.ko copied to build/)"
	@echo "  make tools           build userland tools into build/bin/"
	@echo "  make clean           remove all build artifacts"
	@echo "  make install         install tools + scripts (honours PREFIX/DESTDIR)"
	@echo "  make install-module  install the kernel module via modules_install"
	@echo "  make install-config  install modprobe + udev config files"
	@echo "  make uninstall       remove installed userland tools + scripts"

## Kernel module -------------------------------------------------------------
module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(SRC_KERNEL) modules
	@mkdir -p $(BUILD_DIR)
	@cp -f $(SRC_KERNEL)/cxadc.ko $(BUILD_DIR)/cxadc.ko 2>/dev/null || true
	@echo "Kernel module built: $(BUILD_DIR)/cxadc.ko"

modules_install install-module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(SRC_KERNEL) modules_install
	depmod -a

## Userland tools ------------------------------------------------------------
tools: $(addprefix $(BUILD_BIN)/,$(TOOLS))

# Generic object rule: build/obj/<relative-source-path>.o
$(BUILD_OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(TOOL_CFLAGS) -c $< -o $@

$(BUILD_BIN)/leveladj: $(BUILD_OBJ)/$(SRC_TOOLS)/leveladj/leveladj.o $(COMMON_OBJS)
	@mkdir -p $(BUILD_BIN)
	$(CC) $(TOOL_CFLAGS) -o $@ $^ $(TOOL_LDLIBS)

$(BUILD_BIN)/levelmon: $(BUILD_OBJ)/$(SRC_TOOLS)/levelmon/levelmon.o $(COMMON_OBJS)
	@mkdir -p $(BUILD_BIN)
	$(CC) $(TOOL_CFLAGS) -o $@ $^ $(TOOL_LDLIBS)

$(BUILD_BIN)/cx-capture: $(BUILD_OBJ)/$(SRC_TOOLS)/cx-capture/cx-capture.o $(COMMON_OBJS)
	@mkdir -p $(BUILD_BIN)
	$(CC) $(TOOL_CFLAGS) -o $@ $^ $(TOOL_LDLIBS)

## Install / uninstall -------------------------------------------------------
install: install-tools install-scripts

install-tools: tools
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(addprefix $(BUILD_BIN)/,$(TOOLS)) $(DESTDIR)$(BINDIR)/
	$(INSTALL) -m 0755 $(SRC_TOOLS)/cxadc-status/cxadc-status $(DESTDIR)$(BINDIR)/cxadc-status

install-scripts:
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	@for s in $(SRC_SCRIPTS)/cx*; do \
		[ -f "$$s" ] || continue; \
		echo "install $$s"; \
		$(INSTALL) -m 0755 "$$s" $(DESTDIR)$(BINDIR)/$$(basename "$$s"); \
	done

install-config:
	$(INSTALL) -d $(DESTDIR)$(MODPROBE_DIR) $(DESTDIR)$(UDEV_DIR)
	$(INSTALL) -m 0644 $(CONFIG_DIR)/cxadc.conf  $(DESTDIR)$(MODPROBE_DIR)/cxadc.conf
	$(INSTALL) -m 0644 $(CONFIG_DIR)/cxadc.rules $(DESTDIR)$(UDEV_DIR)/cxadc.rules

uninstall:
	rm -f $(addprefix $(DESTDIR)$(BINDIR)/,$(TOOLS)) $(DESTDIR)$(BINDIR)/cxadc-status
	@for s in $(SRC_SCRIPTS)/cx*; do \
		[ -f "$$s" ] || continue; \
		rm -f $(DESTDIR)$(BINDIR)/$$(basename "$$s"); \
	done

## Clean ---------------------------------------------------------------------
clean:
	-$(MAKE) -C $(KDIR) M=$(CURDIR)/$(SRC_KERNEL) clean
	rm -rf $(BUILD_DIR)

distclean: clean
