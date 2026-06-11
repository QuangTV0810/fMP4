# ═══════════════════════════════════════════════════════════════════════
# Toolchain & Paths
# ═══════════════════════════════════════════════════════════════════════
ifeq ($(filter clean help,$(MAKECMDGOALS)),)
ifndef SYSROOT
    $(error [ERROR] SYSROOT is not set. Run: export SYSROOT=<path-to-staging>)
endif
ifndef CROSS_COMPILE
    $(error [ERROR] CROSS_COMPILE is not set. Run: export CROSS_COMPILE=<path-to-toolchain-prefix>)
endif
ifndef OSAL_INSTALL_DIR
    $(error [ERROR] OSAL_INSTALL_DIR is not set. Run: export OSAL_INSTALL_DIR=<path-to-osal-install>)
endif
endif

SOC             ?= rts3917n

CC              := $(CROSS_COMPILE)gcc
CXX             := $(CROSS_COMPILE)g++
AR              := $(CROSS_COMPILE)gcc-ar
STRIP           := $(CROSS_COMPILE)strip
RANLIB          := $(CROSS_COMPILE)gcc-ranlib

SDK_INC_DIR     := $(SYSROOT)/usr/include
SDK_LIB_DIR     := $(SYSROOT)/usr/lib

PROJ_ROOT          := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SRCDIR             := sources

# FDK-AAC for AAC encoding (required by libhls for fMP4 segmenter)
FDK_AAC_DIR        := $(PROJ_ROOT)3rd/fdk-aac/build/install
FDK_AAC_INC_DIR    := $(FDK_AAC_DIR)/include
FDK_AAC_LIB_DIR    := $(FDK_AAC_DIR)/lib
FDK_AAC_STATIC_LIB := $(FDK_AAC_LIB_DIR)/libfdk-aac.a

# OSAL OS
OSAL_INC_DIR       := $(OSAL_INSTALL_DIR)/include/osal
OSAL_LIB_DIR       := $(OSAL_INSTALL_DIR)/lib

# media-server (libhls + libmov)
MEDIA_SERVER_DIR       := $(PROJ_ROOT)3rd/media-server
MEDIA_SERVER_BUILD_DIR := $(MEDIA_SERVER_DIR)/build/install
LIBHLS_INC_DIR         := $(MEDIA_SERVER_BUILD_DIR)/include/libhls
LIBMOV_INC_DIR         := $(MEDIA_SERVER_BUILD_DIR)/include/libmov
MEDIA_SERVER_LIB_DIR   := $(MEDIA_SERVER_BUILD_DIR)/lib
LIBHLS_STATIC_LIB      := $(MEDIA_SERVER_LIB_DIR)/libhls.a
LIBMOV_STATIC_LIB      := $(MEDIA_SERVER_LIB_DIR)/libmov.a

# ═══════════════════════════════════════════════════════════════════════
# Flags
# ═══════════════════════════════════════════════════════════════════════
INCLUDES  := -I$(FDK_AAC_INC_DIR) -I$(SDK_INC_DIR) -I$(OSAL_INC_DIR) -I./sources \
             -I$(LIBHLS_INC_DIR) -I$(LIBMOV_INC_DIR)
DEFINES   := -D_GNU_SOURCE
OPT_FLAGS := -Os -ffunction-sections -fdata-sections
DEPFLAGS  := -MMD -MP

CSTD      := -std=c11
CXXSTD    := -std=c++17
WARNINGS  := -Wall -Wextra -Wformat=2

CFLAGS    := $(CSTD) $(WARNINGS) $(INCLUDES) $(DEFINES) $(OPT_FLAGS) $(DEPFLAGS)
CXXFLAGS  := $(CXXSTD) $(WARNINGS) $(INCLUDES) $(DEFINES) $(OPT_FLAGS) $(DEPFLAGS)

LDFLAGS   := -L$(SDK_LIB_DIR) -Wl,-rpath-link,$(SDK_LIB_DIR)
LDFLAGS   += -L$(FDK_AAC_LIB_DIR) -Wl,-rpath-link,$(FDK_AAC_LIB_DIR)
LDFLAGS   += -L$(OSAL_LIB_DIR) -Wl,-rpath-link,$(OSAL_LIB_DIR)
LDFLAGS   += -L$(MEDIA_SERVER_LIB_DIR)
LDFLAGS   += -Wl,--gc-sections -flto -Wno-lto-type-mismatch

LIBS      := -lssl -lcrypto -lcurl -lpthread -lstdc++ -lubus -lubox -losal -losal_bsp $(FDK_AAC_STATIC_LIB)

# ═══════════════════════════════════════════════════════════════════════
# Files & Directories
# ═══════════════════════════════════════════════════════════════════════
BUILD    := build
BINDIR   := $(BUILD)/bin

SDK_SRC_C = \
	$(wildcard $(SRCDIR)/platform/utils/*.c) \
	$(wildcard $(SRCDIR)/services/media/*.c) \
	$(wildcard $(SRCDIR)/hal/$(SOC)/*.c) \

SDK_SRC_CPP = \
	$(wildcard $(SRCDIR)/services/record/fmp4_recorder.cpp) \
	$(wildcard $(SRCDIR)/services/record/m3u8_recorder.cpp)

MEDIASERVER_LIBS := $(LIBHLS_STATIC_LIB) $(LIBMOV_STATIC_LIB)

APP_SRC := $(SRCDIR)/apps/app.cpp
APP_TASK_SRC := $(wildcard $(SRCDIR)/apps/task/*.cpp)

SDK_OBJ := \
    $(patsubst $(SRCDIR)/%.c,  $(BUILD)/%.o, $(SDK_SRC_C)) \
    $(patsubst $(SRCDIR)/%.cpp,$(BUILD)/%.o, $(SDK_SRC_CPP))

APP_OBJ := \
	$(patsubst $(SRCDIR)/%.cpp,$(BUILD)/%.o,$(APP_SRC)) \
	$(patsubst $(SRCDIR)/%.cpp,$(BUILD)/%.o,$(APP_TASK_SRC))

OBJ  := $(SDK_OBJ) $(APP_OBJ)
DEPS := $(OBJ:.o=.d)

# ═══════════════════════════════════════════════════════════════════════
# Targets
# ═══════════════════════════════════════════════════════════════════════
.PHONY: all clean help

BINS := $(BINDIR)/media-tasking

all: directories $(BINS)
	@echo ">>> Build complete:"
	@for bin in $(BINS); do echo "    $$bin"; done

# ─── Directories ─────────────────────────────────────────────────────
directories:
	@mkdir -p $(BINDIR)
	@mkdir -p $(sort $(dir $(OBJ)))
	@mkdir -p $(BUILD)

# ─── Compile ─────────────────────────────────────────────────────────
$(BUILD)/%.o: $(SRCDIR)/%.c
	@echo "  CC  $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRCDIR)/%.cpp
	@echo "  CXX $<"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# ─── Link & Strip (per-binary) ────────────────────────────────────────
$(BINDIR)/media-tasking: $(APP_OBJ) $(SDK_OBJ) $(MEDIASERVER_LIBS)
	@echo "  LD  $@"
	@$(CXX) $(APP_OBJ) $(SDK_OBJ) -o $@ $(LDFLAGS) $(LIBS) $(MEDIASERVER_LIBS)
	@$(STRIP) -s $@

# ─── Clean ───────────────────────────────────────────────────────────
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD)

# ─── Help ────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "  all      Build media-tasking (default)"
	@echo "  clean    Remove all build artifacts"
	@echo "  help     Show this message"
	@echo ""
	@echo "Environment:"
	@echo "  SOC              = $(SOC)"
	@echo "  CROSS_COMPILE    = $(CROSS_COMPILE)"
	@echo "  SYSROOT          = $(SYSROOT)"
	@echo "  OSAL_INSTALL_DIR = $(OSAL_INSTALL_DIR)"
	@echo ""

# ─── Auto dependency tracking ────────────────────────────────────────
-include $(DEPS)
