# Pebblebol host test suite (spec §50). Plain g++, no external framework.
# Populated in P2-C2; until then `check` is an honest no-op so CI has a target.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -DNT_HOST
SRC_ROOT ?= ../Pebblebol/src
BIN      ?= bin

TESTS := $(patsubst %.cpp,$(BIN)/%,$(wildcard test_*.cpp))

.PHONY: all check clean
all: $(TESTS)

check: all
ifeq ($(strip $(TESTS)),)
	@echo "tests: no test_*.cpp yet (P2-C2) — nothing to run"
else
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t $(ARGS); done; echo "ALL PASS $(words $(TESTS))/$(words $(TESTS))"
endif

clean:
	rm -rf $(BIN)
