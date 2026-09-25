# Shortcuts over CMake. `make help` lists the targets.

BUILD_DIR := build
CMAKE := cmake
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
SOURCES = find src include tests examples benchmarks \( -name '*.cpp' -o -name '*.hpp' \) -print0

.PHONY: all build debug configure configure-debug test test-consumers fixtures-check lint \
	lint-md format format-md tidy bench coverage pre-commit install-hooks clean help

all: build

configure:
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)

configure-debug:
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug $(CMAKE_ARGS)

build: configure
	@$(CMAKE) --build $(BUILD_DIR) -j$(NPROC)

debug: configure-debug
	@$(CMAKE) --build $(BUILD_DIR) -j$(NPROC)

test: build
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

test-consumers:
	@./tools/test_consumers.sh

fixtures-check:
	@python3 tools/verify_fixture_checksums.py

lint:
	@command -v clang-format >/dev/null || { echo "clang-format is not installed"; exit 1; }
	@$(SOURCES) | xargs -0 clang-format --dry-run --Werror
	@python3 tools/cpp_auto_audit.py

lint-md:
	npx markdownlint-cli2 "**/*.md"

format:
	@command -v clang-format >/dev/null || { echo "clang-format is not installed"; exit 1; }
	@$(SOURCES) | xargs -0 clang-format -i

format-md:
	npx markdownlint-cli2 --fix "**/*.md"

# Compile the libraries with clang-tidy. On macOS, see CONTRIBUTING.md.
tidy:
	@$(CMAKE) -S . -B build-tidy -DCMAKE_BUILD_TYPE=Debug -DSPC_ENABLE_CLANG_TIDY=ON \
		-DSPC_BUILD_TESTS=OFF -DSPC_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)
	@$(CMAKE) --build build-tidy -j$(NPROC)

# Release build in build-bench, without tests and examples.
bench:
	@$(CMAKE) -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DSPC_BUILD_BENCHMARKS=ON \
		-DSPC_BUILD_TESTS=OFF -DSPC_BUILD_EXAMPLES=OFF $(CMAKE_ARGS)
	@$(CMAKE) --build build-bench -j$(NPROC)
	@./build-bench/benchmarks/spc_benchmarks $(BENCH_ARGS)

coverage:
	@$(CMAKE) -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DSPC_ENABLE_COVERAGE=ON
	@$(CMAKE) --build build-coverage -j$(NPROC)
	@ctest --test-dir build-coverage --output-on-failure
	@lcov --capture --directory build-coverage --output-file build-coverage/coverage.info \
		--ignore-errors mismatch
	@lcov --remove build-coverage/coverage.info '/usr/*' '*/_deps/*' \
		--output-file build-coverage/coverage.info --ignore-errors unused
	@genhtml build-coverage/coverage.info --output-directory build-coverage/report
	@echo "Report: build-coverage/report/index.html"

pre-commit: format lint

install-hooks:
	@hook="$$(git rev-parse --git-path hooks)/pre-commit"; \
	if [ -f "$$hook" ] && ! grep -q 'make pre-commit' "$$hook"; then \
		echo "$$hook already exists; not replacing it"; exit 1; \
	fi; \
	mkdir -p "$$(dirname "$$hook")" && printf '#!/bin/sh\nexec make pre-commit\n' > "$$hook" && \
	chmod +x "$$hook" && echo "Installed $$hook (runs make pre-commit)"

# Run an example: make run-static_feed, run-arcgis, run-fire_weather,
# run-archive, run-watches, or run-parse_outlook.
run-%: build
	@./$(BUILD_DIR)/examples/example_$*

clean:
	@rm -rf $(BUILD_DIR) build-*

help:
	@echo "make build           Release build (CMAKE_ARGS=... adds CMake options)"
	@echo "make debug           Debug build"
	@echo "make test            Build, then run the unit tests"
	@echo "make lint            clang-format check and the auto audit"
	@echo "make lint-md         Markdown lint"
	@echo "make fixtures-check  Verify tests/fixtures/SHA256SUMS"
	@echo "make test-consumers  Build installed and FetchContent consumers"
	@echo "make tidy            Compile the libraries with clang-tidy"
	@echo "make bench           Run the benchmarks (BENCH_ARGS=... passes flags)"
	@echo "make format          Format C++ in place"
	@echo "make format-md       Fix Markdown lint findings in place"
	@echo "make coverage        HTML coverage report (needs lcov)"
	@echo "make install-hooks   Run format and lint before each commit"
	@echo "make run-<example>   Run an example, e.g. make run-arcgis"
	@echo "make clean           Remove build directories"
