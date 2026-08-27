# Static release build (Windows MinGW / MSYS2)
#   make release
#   make clean

CXX      ?= g++
CC       ?= gcc

SRC_DIR  := src
TP_DIR   := third_party/miniz
BUILD    := build
OUT      := $(BUILD)/tt-unlock.exe
RELEASE  := release/tt-unlock.exe

CXXFLAGS := -std=c++17 -O2 -DNDEBUG -D_WIN32_WINNT=0x0A00 \
            -I$(SRC_DIR) -I$(TP_DIR) \
            -Wall -Wextra -Wno-unused-parameter \
            -ffunction-sections -fdata-sections

LDFLAGS  := -static -static-libgcc -static-libstdc++ \
            -Wl,--gc-sections -s \
            -lwinhttp -ladvapi32 -lshell32 -luser32 -lkernel32

.PHONY: all release clean

all: release

release: $(OUT)
	@mkdir -p release
	cp -f $(OUT) $(RELEASE)
	@echo "  => $(RELEASE)"

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/miniz.o: $(TP_DIR)/miniz.c $(TP_DIR)/miniz.h | $(BUILD)
	$(CC) -O2 -DNDEBUG -I$(TP_DIR) -c $(TP_DIR)/miniz.c -o $@

$(OUT): $(SRC_DIR)/main.cpp $(BUILD)/miniz.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(BUILD)/miniz.o $(LDFLAGS)

clean:
	rm -rf $(BUILD)
