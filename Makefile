# SPDX-License-Identifier: AGPL-3.0-only
CMAKE ?= cmake
JOBS ?= 4

.PHONY: all setup clean
all:
	"$(CMAKE)" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
	"$(CMAKE)" --build build --parallel $(JOBS)

setup: all
	"$(CMAKE)" --build build --target package

clean:
	"$(CMAKE)" --build build --target clean
