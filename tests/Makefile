# Pebblebol host test suite (plan §4). Plain g++, no external framework, no
# Arduino include path: every sketch source compiled here must be pure C++.
#
#   make -C tests check      build and run every test_*.cpp (stops on first failure)
#   make -C tests golden     re-record tests/golden/* (only from a `retune:` commit)
#   make -C tests fixtures   rebuild tests/fixtures/*.bin from legacy/mkfixtures.cpp
#
# Layout: the sketch is flat in ../Pebblebol until P2-C8 moves it under
# ../Pebblebol/src/**; only SRC_ROOT and the object paths below change then.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1
SRC_ROOT ?= ../Pebblebol
BIN      ?= bin
OBJ      := $(BIN)/obj

# -DNT_HOST marks a host build; NT_TESTS_DIR lets a binary find fixtures/ and
# golden/ from any working directory.
CPPFLAGS := -DNT_HOST -I$(SRC_ROOT) -I. -DNT_TESTS_DIR=\"$(CURDIR)\"

TESTS := $(patsubst %.cpp,$(BIN)/%,$(sort $(wildcard test_*.cpp)))

.PHONY: all check clean golden fixtures
all: $(TESTS)

check: all
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t $(ARGS); done; \
	echo "ALL PASS $(words $(TESTS))/$(words $(TESTS))"

clean:
	rm -rf $(BIN)

$(BIN) $(OBJ):
	mkdir -p $@

# --- sketch sources compiled for the host -----------------------------------
$(OBJ)/%.o: $(SRC_ROOT)/%.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# gametime pinned to the estimated-clock path so test_clock can drive the
# millis() wrap extension across 2^32 on a machine whose wall clock is right.
$(OBJ)/gametime_nevervalid.o: $(SRC_ROOT)/gametime.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DGT_HOST_NEVER_VALID -c $< -o $@

$(OBJ)/host_shims.o: host_shims.cpp host_shims.h | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# --- what each test links (nt_test.h + the objects it needs) -----------------
$(BIN)/test_crc16:      $(OBJ)/crc16.o
$(BIN)/test_rng:        $(OBJ)/rng.o
$(BIN)/test_genome:     $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_sim_golden: $(OBJ)/sim.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_care:       $(OBJ)/sim.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_input:      $(OBJ)/input.o $(OBJ)/host_shims.o
$(BIN)/test_qr:         $(OBJ)/qr.o
$(BIN)/test_clock:      $(OBJ)/gametime_nevervalid.o $(OBJ)/host_shims.o
$(BIN)/test_overflow:   $(OBJ)/gametime_nevervalid.o $(OBJ)/host_shims.o \
                        $(OBJ)/sim.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_fixtures:   $(OBJ)/crc16.o

$(BIN)/test_%: test_%.cpp nt_test.h | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(filter %.o,$^) -o $@

# --- goldens ----------------------------------------------------------------
golden: $(BIN)/test_sim_golden
	./$(BIN)/test_sim_golden --record

# --- legacy fixtures (v1 PetSave / Config / GainSave blobs, plan P2-C2) ------
$(BIN)/mkfixtures: legacy/mkfixtures.cpp $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(filter %.o,$^) -o $@

fixtures: $(BIN)/mkfixtures
	./$(BIN)/mkfixtures fixtures
