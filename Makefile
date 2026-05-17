# spc-cpp - Root Makefile
# Wraps CMake for convenient day-to-day workflow

BUILD_DIR := build
CPP_AUTO_AUDIT := python3 tools/cpp_auto_audit.py
CMAKE := cmake
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.performancecores 2>/dev/null || echo 4)

.PHONY: all build debug test lint clean configure configure-debug help format pre-commit \
	install-hooks coverage lint-md format-md \
	run-static_feed run-arcgis run-archive

all: build

configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

configure-debug:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: configure
	@$(CMAKE) --build $(BUILD_DIR) -j$(NPROC)

debug: configure-debug
	@$(CMAKE) --build $(BUILD_DIR) -j$(NPROC)

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

lint:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Checking code formatting..."; \
		find src include tests examples \( -name '*.cpp' -o -name '*.hpp' \) -print0 | \
			xargs -0 clang-format --dry-run --Werror && \
		echo "Format check passed."; \
	else \
		echo "clang-format not found. Install clang-format to run lint."; \
		exit 1; \
	fi
	$(CPP_AUTO_AUDIT)

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting code..."; \
		find src include tests examples \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i; \
		echo "Done"; \
	else \
		echo "clang-format not found. Install clang-format to format code."; \
		exit 1; \
	fi

coverage:
	@mkdir -p build-coverage
	@cd build-coverage && $(CMAKE) .. -DCMAKE_BUILD_TYPE=Debug -DSPC_ENABLE_COVERAGE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(CMAKE) --build build-coverage -j$(NPROC)
	@cd build-coverage && ctest --output-on-failure
	@lcov --capture --directory build-coverage --output-file build-coverage/coverage.info --ignore-errors mismatch
	@lcov --remove build-coverage/coverage.info '/usr/*' '*/build-coverage/_deps/*' --output-file build-coverage/coverage_filtered.info --ignore-errors unused
	@genhtml build-coverage/coverage_filtered.info --output-directory build-coverage/coverage-report
	@echo "Coverage report: build-coverage/coverage-report/index.html"

# pre-commit: auto-format, then lint. Idempotent.
pre-commit: format lint

install-hooks:
	@mkdir -p .git/hooks
	@if [ -f .git/hooks/pre-commit ] && grep -q 'make pre-commit' .git/hooks/pre-commit 2>/dev/null; then \
		echo "pre-commit hook already installed"; \
	else \
		printf '#!/bin/sh\nexec make pre-commit\n' > .git/hooks/pre-commit; \
		chmod +x .git/hooks/pre-commit; \
		echo "Installed .git/hooks/pre-commit -> make pre-commit"; \
	fi

clean:
	@rm -rf $(BUILD_DIR) build-coverage
	@echo "Cleaned build directory"

# Examples (added in PR-2; targets declared here so the surface is stable)
run-static_feed: build
	@./$(BUILD_DIR)/examples/example_static_feed

run-arcgis: build
	@./$(BUILD_DIR)/examples/example_arcgis

run-archive: build
	@./$(BUILD_DIR)/examples/example_archive

lint-md:
	npx markdownlint-cli2 "**/*.md"

format-md:
	npx markdownlint-cli2 --fix "**/*.md"

help:
	@echo "spc-cpp C++ SDK Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make build           - Configure and build the SDK (Release)"
	@echo "  make debug           - Configure and build the SDK (Debug)"
	@echo "  make test            - Run tests"
	@echo "  make lint            - Check formatting + cpp_auto_audit"
	@echo "  make format          - Format code in place"
	@echo "  make coverage        - Generate code coverage report (requires lcov)"
	@echo "  make clean           - Remove build artifacts"
	@echo "  make help            - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  make run-static_feed - Static-feed client example"
	@echo "  make run-arcgis      - ArcGIS MapServer client example"
	@echo "  make run-archive     - IEM archive client example"
