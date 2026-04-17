PROJECT_NAME := routing_genetic_astar
CXX ?= c++
CXXSTD ?= c++23
BUILD_MODE ?= debug
OS_NAME ?= $(shell if [ "$(shell uname -s)" = "Darwin" ]; then echo macos; else echo linux; fi)
LINUX_DISTRO ?= $(shell if [ -f /etc/os-release ]; then . /etc/os-release; echo $$ID; else echo unknown; fi)

BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj
TARGET := $(BIN_DIR)/$(PROJECT_NAME)
INCLUDE_DIR := include

SRC_FILES := $(wildcard src/*.cpp)
OBJ_FILES := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))

CXXFLAGS := -std=$(CXXSTD) -I$(INCLUDE_DIR) -Wall -Wextra -Wpedantic
LDFLAGS :=

ifeq ($(BUILD_MODE),release)
CXXFLAGS += -O2
else
CXXFLAGS += -O0 -g
endif

.PHONY: all clean docs python test info

all: $(TARGET)

info:
	@echo "PROJECT_NAME=$(PROJECT_NAME)"
	@echo "OS_NAME=$(OS_NAME)"
	@echo "LINUX_DISTRO=$(LINUX_DISTRO)"
	@echo "CXX=$(CXX)"
	@echo "CXXSTD=$(CXXSTD)"

$(TARGET): $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(OBJ_FILES) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TARGET)
	bash tests/smoke_test.sh $(TARGET)

docs:
	@echo "Open docs/architecture.md and docs/implementation_spec.md for design details."

python:
	@echo "Python bindings are planned under python/."

clean:
	rm -rf $(BUILD_DIR)
