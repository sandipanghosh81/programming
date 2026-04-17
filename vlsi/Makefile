SHELL := /bin/bash

# macOS often has no `python` on PATH — use python3 unless overridden.
PYTHON ?= python3

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

EDA_CLI_SRC := $(ROOT_DIR)/vlsi/eda_tools/eda_cli
EDA_CLI_BUILD := $(ROOT_DIR)/vlsi/eda_tools/eda_cli/build
EDA_DAEMON := $(EDA_CLI_BUILD)/eda_daemon

VLSI_AGENT_DIR := $(ROOT_DIR)/vlsi/agent
RUN_STACK_SH := $(ROOT_DIR)/vlsi/scripts/run_daemon_stack.sh

.PHONY: help build build-daemon daemon agent constraints-tool install-agent-deps clean-agent-deps health clean clean-daemon

help:
	@echo "VLSI helper targets:"
	@echo "  make build          Build C++ daemon + pip install -e vlsi/agent"
	@echo "  make build-daemon   Build C++ eda_daemon (CMake) only"
	@echo "  make install-agent-deps  pip install -e vlsi/agent (also run by: make build)"
	@echo "  make daemon         Start ALL: constraints MCP :18081, agent :8000, eda_daemon :8080"
	@echo "                      (uses vlsi/scripts/run_daemon_stack.sh; logs in TMPDIR, e.g. /tmp/vlsi-*.log)"
	@echo "  make agent          Run Python agent (server.py) on :8000 only"
	@echo "  make constraints-tool Run constraints MCP server on :18081 only"
	@echo "  make health         Curl agent /health endpoint"
	@echo "  make clean          Remove C++ build dir + uninstall vlsi-agent / egg-info"
	@echo "  make clean-daemon   Remove CMake build directory only"
	@echo "  make clean-agent-deps  Uninstall vlsi-agent pip package + egg-info (also run by: make clean)"

build: build-daemon install-agent-deps

build-daemon:
	@cmake -S "$(EDA_CLI_SRC)" -B "$(EDA_CLI_BUILD)" -DCMAKE_BUILD_TYPE=Release
	@cmake --build "$(EDA_CLI_BUILD)" -j
	@echo "Built: $(EDA_DAEMON)"

daemon: build-daemon
	@VLSI_PYTHON="$(PYTHON)" "$(RUN_STACK_SH)"

install-agent-deps:
	@cd "$(VLSI_AGENT_DIR)" && $(PYTHON) -m pip install -e .

# Removes editable install metadata (package name from pyproject: vlsi-agent).
clean-agent-deps:
	@echo "[vlsi] uninstalling Python package vlsi-agent (if present)..."
	@cd "$(VLSI_AGENT_DIR)" && $(PYTHON) -m pip uninstall -y vlsi-agent 2>/dev/null || true
	@find "$(VLSI_AGENT_DIR)" \( -type d -name '*.egg-info' -o -type d -name '__pycache__' \) -exec rm -rf {} + 2>/dev/null || true
	@echo "[vlsi] clean-agent-deps done."

agent:
	@cd "$(VLSI_AGENT_DIR)" && $(PYTHON) server.py

constraints-tool:
	@$(PYTHON) "$(ROOT_DIR)/vlsi/eda_tools/python/constraints_tool/mcp_server.py"

health:
	@curl -sS http://127.0.0.1:8000/health && echo

clean-daemon:
	@rm -rf "$(EDA_CLI_BUILD)"

clean: clean-daemon clean-agent-deps

