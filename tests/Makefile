# Pebblebol host test suite (plan §4). Plain g++, no external framework, no
# Arduino include path: every sketch source compiled here must be pure C++.
#
#   make -C tests check      build and run every test_*.cpp (stops on first failure)
#   make -C tests golden     re-record tests/golden/* (only from a `retune:` commit)
#   make -C tests fixtures   rebuild tests/fixtures/*.bin from legacy/mkfixtures.cpp
#
# Layout: the sketch lives under ../Pebblebol/src/<layer>/ (plan section 1.2).
# SRC_ROOT is that src/ directory, so a test includes "game/sim.h" exactly as
# the firmware does, and the pattern rules below find the .cpp in its layer.
CXX      ?= g++
# -MMD -MP make the compiler emit a .d file listing every header each object
# depends on. Without them a header-only edit leaves stale objects in place and
# `make check` reports ALL PASS over code it did not rebuild — a gate that lies.
# Proven: editing HOME_METER_Y0 in ui/screen_home.h passed 14/14 before this,
# and fails the HOME snapshots after it.
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -MMD -MP
SRC_ROOT ?= ../Pebblebol/src
BIN      ?= bin
OBJ      := $(BIN)/obj

# -DNT_HOST marks a host build; NT_TESTS_DIR lets a binary find fixtures/ and
# golden/ from any working directory.
CPPFLAGS := -DNT_HOST -I$(SRC_ROOT) -I. -DNT_TESTS_DIR=\"$(CURDIR)\"

TESTS := $(patsubst %.cpp,$(BIN)/%,$(sort $(wildcard test_*.cpp)))

.PHONY: all check clean golden screens fixtures
all: $(TESTS)

check: all
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t $(ARGS); done; \
	echo "ALL PASS $(words $(TESTS))/$(words $(TESTS))"

clean:
	rm -rf $(BIN)

$(BIN) $(OBJ):
	mkdir -p $@

# --- sketch sources compiled for the host -----------------------------------
# One pattern rule per layer that holds a host-compilable .cpp; make picks the
# rule whose prerequisite exists, so objects keep their flat basenames.
# Every .d emitted above; the wildcard keeps a clean tree working.
-include $(wildcard $(OBJ)/*.d) $(wildcard $(BIN)/*.d)

$(OBJ)/%.o: $(SRC_ROOT)/core/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC_ROOT)/game/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC_ROOT)/hardware/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC_ROOT)/ui/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC_ROOT)/app/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC_ROOT)/minigames/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Both halves of every game. games/*_logic.cpp is pure; games/*_draw.cpp draws,
# but only through the gfx.h seam, so it compiles here against tests/fakes/gfx_fb
# exactly as a migrated screen does - which is how test_screens.cpp snapshots a
# run frame at the real 128x64 (spec section 63). THE INVARIANT IS NOT "no draw
# object anywhere", it is that test_minigames links none: see its line below.
$(OBJ)/%.o: $(SRC_ROOT)/minigames/games/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC_ROOT)/persistence/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Host fakes for the device seams (kv_store, later gfx).
$(OBJ)/%.o: fakes/%.cpp fakes/%.h Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# gametime pinned to the estimated-clock path so test_clock can drive the
# millis() wrap extension across 2^32 on a machine whose wall clock is right.
$(OBJ)/gametime_nevervalid.o: $(SRC_ROOT)/hardware/gametime.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DGT_HOST_NEVER_VALID -c $< -o $@

$(OBJ)/host_shims.o: host_shims.cpp host_shims.h | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# --- what each test links (nt_test.h + the objects it needs) -----------------
$(BIN)/test_crc16:      $(OBJ)/crc16.o
$(BIN)/test_rng:        $(OBJ)/rng.o
$(BIN)/test_genome:     $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_care_golden: $(OBJ)/sim.o $(OBJ)/daylight.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o
# box.o comes along for the "seven days stored comes back full" case: box_recover
# is the OTHER half of the care model (spec section 9) and P3-C1 tunes both.
$(BIN)/test_care:       $(OBJ)/sim.o $(OBJ)/daylight.o $(OBJ)/box.o \
                        $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
# daylight.cpp is pure arithmetic over the balance.h table: no clock, no state.
$(BIN)/test_daylight:   $(OBJ)/daylight.o
$(BIN)/test_box:        $(OBJ)/box.o
# xp.cpp is pure: the curve, the level carry and the anti-farm ledger, with
# species_table.h (header-only) for the base_hp the hp rescale needs.
# evolution.o comes with it because the two call each other: xp_add() asks
# evolution_level_ready() whether to raise EVO_STATE_PENDING, and
# evolution_apply() calls back into xp_hp_rescale() so there is ONE rescale.
$(BIN)/test_xp:         $(OBJ)/xp.o $(OBJ)/evolution.o
# The other half of that pair. Both objects, for the same reason.
$(BIN)/test_evolution:  $(OBJ)/evolution.o $(OBJ)/xp.o
# The Box <-> sim seam: the real box.cpp and the real sim.cpp together, which
# is the only way to see a swap move sim's raw pointer off its creature.
$(BIN)/test_box_sim:    $(OBJ)/box_sim.o $(OBJ)/box.o $(OBJ)/sim.o \
                        $(OBJ)/daylight.o $(OBJ)/genome.o $(OBJ)/crc16.o \
                        $(OBJ)/rng.o
$(BIN)/test_input:      $(OBJ)/input.o $(OBJ)/host_shims.o
# The minigame contract and the PURE logic of every game. No draw objects and
# no ui/: if this link line ever needs one, a game has leaked drawing into its
# logic and the determinism argument has gone with it.
$(BIN)/test_minigames: $(OBJ)/minigame.o $(OBJ)/manager.o $(OBJ)/ping_logic.o \
                        $(OBJ)/sequence_logic.o $(OBJ)/packet_flood_logic.o \
                        $(OBJ)/firewall_logic.o $(OBJ)/buffer_logic.o \
                        $(OBJ)/delete_logic.o $(OBJ)/rng.o
$(BIN)/test_qr:         $(OBJ)/qr.o
$(BIN)/test_clock:      $(OBJ)/gametime_nevervalid.o $(OBJ)/host_shims.o
$(BIN)/test_overflow:   $(OBJ)/gametime_nevervalid.o $(OBJ)/host_shims.o \
                        $(OBJ)/sim.o $(OBJ)/daylight.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_fixtures:   $(OBJ)/crc16.o
$(BIN)/test_persistence: $(OBJ)/save_manager.o $(OBJ)/migration.o \
                        $(OBJ)/kv_mem.o $(OBJ)/crc16.o
$(BIN)/test_game_state: $(OBJ)/game_state.o $(OBJ)/save_manager.o $(OBJ)/migration.o \
                        $(OBJ)/kv_mem.o $(OBJ)/boot_host.o $(OBJ)/crc16.o $(OBJ)/rng.o
# The screen table's migrated rows are pure translation units, so they compile
# here exactly as they do in the firmware; gfx_fb.o is the host end of gfx.h
# and gfx_widgets.o is the SHARED half of it, linked into both.
# Every ScreenId has a row since P2-C11d, so the table drags in every screen
# module - which is exactly what makes "the table is complete" testable.
SCREEN_OBJS := $(OBJ)/screen_table.o $(OBJ)/screen_boot.o \
               $(OBJ)/screen_box.o $(OBJ)/screen_soon.o \
               $(OBJ)/screen_error.o $(OBJ)/screen_home.o \
               $(OBJ)/screen_menu.o $(OBJ)/screen_care.o \
               $(OBJ)/screen_status.o $(OBJ)/screen_settings.o \
               $(OBJ)/screen_time.o $(OBJ)/screen_view.o \
               $(OBJ)/screen_link.o $(OBJ)/screen_diag.o \
               $(OBJ)/screen_evolution.o $(OBJ)/screen_creator.o \
               $(OBJ)/dialog.o $(OBJ)/qr.o $(OBJ)/box.o \
               $(OBJ)/gfx_widgets.o $(OBJ)/gfx_fb.o \
               $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

# The minigame run frames, snapshotted by test_screens.cpp. Each game needs BOTH
# halves: the draw half calls accessors the logic half owns, which is the whole
# point of the split - neither side re-derives a rule. minigame.o drives them;
# rng.o is already in SCREEN_OBJS.
# ALL SIX GAMES, since P3-C5. P3-C4b shipped four: ping_draw.cpp and
# sequence_draw.cpp were compiled only by the firmware build and never rendered
# on the host, so the P3-C4 claim "snapshots for each game" was true of four of
# six. They are here now, and the same argument that put the other four here
# applies - a frozen BUFFER zone passed all 45 logic cases and was caught only
# by a pixel golden.
MG_FRAME_OBJS := $(OBJ)/minigame.o \
                 $(OBJ)/ping_logic.o         $(OBJ)/ping_draw.o \
                 $(OBJ)/sequence_logic.o     $(OBJ)/sequence_draw.o \
                 $(OBJ)/packet_flood_logic.o $(OBJ)/packet_flood_draw.o \
                 $(OBJ)/firewall_logic.o     $(OBJ)/firewall_draw.o \
                 $(OBJ)/buffer_logic.o       $(OBJ)/buffer_draw.o \
                 $(OBJ)/delete_logic.o       $(OBJ)/delete_draw.o

$(BIN)/test_screens:    $(SCREEN_OBJS) $(MG_FRAME_OBJS)

# The navigation machine and the section 7 router, over the REAL table.
$(BIN)/test_statemachine: $(SCREEN_OBJS) $(OBJ)/state_machine.o \
                        $(OBJ)/input_router.o $(OBJ)/host_shims.o
# pet_view.cpp is the model->view seam petfx and actfx read (P2-C11c). It is
# pure, so it compiles here; sim.o comes with it because the live fill reads
# sim_stat_pct() / sim_mood_score().
# evolution.o + xp.o as well: the view is where "the body changed" is provable,
# so the P3-C3 case evolves a Pebble and re-fills its PetView.
$(BIN)/test_pet_view:   $(OBJ)/pet_view.o $(OBJ)/sim.o $(OBJ)/daylight.o \
                        $(OBJ)/box.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o

$(BIN)/test_%: test_%.cpp nt_test.h | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(filter %.o,$^) -o $@

# --- goldens ----------------------------------------------------------------
golden: $(BIN)/test_care_golden
	./$(BIN)/test_care_golden --record

# Screen snapshots (golden/screens/*.pbm). Same rule as above: only from a
# commit that means to change what a screen looks like, and read the diff.
screens: $(BIN)/test_screens
	./$(BIN)/test_screens --record

# --- legacy fixtures (v1 PetSave / Config / GainSave blobs, plan P2-C2) ------
$(BIN)/mkfixtures: legacy/mkfixtures.cpp $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(filter %.o,$^) -o $@

fixtures: $(BIN)/mkfixtures
	./$(BIN)/mkfixtures fixtures
