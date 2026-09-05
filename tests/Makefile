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

# networking/ (P4-C5). NOTHING under src/networking could link on the host
# before this rule existed - `grep -c networking Makefile` was 0 - so the
# section 15 codec had no way to be tested at all. The rule is generic and the
# LINK LINES are what select: ble_social.cpp, net.cpp and webui.cpp are device
# modules that include Arduino.h and no test asks for them, so they are never
# compiled here. tools/check.sh gates the purity of the pure ones BY FILENAME
# for exactly that reason.
$(OBJ)/%.o: $(SRC_ROOT)/networking/%.cpp Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Host fakes for the device seams (kv_store, later gfx).
$(OBJ)/%.o: fakes/%.cpp fakes/%.h Makefile | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# gametime pinned to the estimated-clock path so test_clock can drive the
# millis() wrap extension across 2^32 on a machine whose wall clock is right.
$(OBJ)/gametime_nevervalid.o: $(SRC_ROOT)/hardware/gametime.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DGT_HOST_NEVER_VALID -c $< -o $@

# THE DEVICE BRANCH OF gametime.cpp, COMPILED ON THE HOST (P7-C6).
# gt_mono_ms() has two bodies and until now only the host one was ever compiled
# here: the DEVICE one - the one that reads esp_rtc_get_time_us() instead of
# millis(), which is the whole point of P6-C3's move off uptime - was guarded
# by a grep in tools/check.sh and by nothing else. -DARDUINO plus the fake
# include directory in FRONT of the system ones builds it, and
# tests/test_clock.cpp drives a deep-sleep wake through it.
#
# fakes/arduino/ IS FOR THIS OBJECT AND NOTHING ELSE. Two headers, one symbol
# each; tools/check.sh fails the build if a second rule reaches for it, because
# a wider fake would become a second Arduino core that can drift from the real
# one in silence.
$(OBJ)/gametime_arduino.o: $(SRC_ROOT)/hardware/gametime.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DARDUINO -Ifakes/arduino -c $< -o $@

$(OBJ)/arduino_clocks.o: fakes/arduino_clocks.cpp fakes/arduino/Arduino.h | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

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
# activity.o joins them in P6-C4. The activity farm is a CROSS-MODULE property -
# the day is game/activity.cpp's and the budget that bounds what a day is worth
# is game/xp.cpp's - and neither file's own test binary could see it, which is
# exactly why it shipped. This is the binary where the two meet.
$(BIN)/test_xp:         $(OBJ)/xp.o $(OBJ)/evolution.o $(OBJ)/activity.o
# The other half of that pair. Both objects, for the same reason.
$(BIN)/test_evolution:  $(OBJ)/evolution.o $(OBJ)/xp.o
# The generated content tables (P4-C1). species.o is the accessor layer AND the
# one translation unit that includes every generated header, so linking it here
# is what makes their compile-time guards fire in this build and not only in the
# firmware one. evolution.o + xp.o come as a pair (they call each other) because
# test_content drives evolution_apply() through a failing CONDITION.
$(BIN)/test_content:    $(OBJ)/species.o $(OBJ)/evolution.o $(OBJ)/xp.o
# Derived stats. genome.o drags crc16.o and rng.o, exactly as test_genome does.
$(BIN)/test_stats:      $(OBJ)/pebble.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o
# The Box <-> sim seam: the real box.cpp and the real sim.cpp together, which
# is the only way to see a swap move sim's raw pointer off its creature.
$(BIN)/test_box_sim:    $(OBJ)/box_sim.o $(OBJ)/box.o $(OBJ)/sim.o \
                        $(OBJ)/daylight.o $(OBJ)/genome.o $(OBJ)/crc16.o \
                        $(OBJ)/rng.o
# THE BATTLE ENGINE (P4-C2). battle.cpp is PURE, so it compiles here exactly as
# it does in the firmware; pebble.o is the one stat derivation it calls, and
# genome.o drags crc16.o and rng.o just as test_stats does. NO ui/ object and no
# species.o: type_mod_of() and species_get() are inline in the generated
# headers, so a round never leaves the data tables.
$(BIN)/test_battle:     $(OBJ)/battle.o $(OBJ)/pebble.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_battle_replay: $(OBJ)/battle.o $(OBJ)/pebble.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_battle_golden: $(OBJ)/battle.o $(OBJ)/pebble.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o
# THE LOCAL OPPONENT (P4-C3). battle_ai.o on top of the SAME objects the engine
# tests link, and no others: the AI reads the battle through game/battle.h's
# const queries and needs no privileged access to anything.
$(BIN)/test_battle_ai:  $(OBJ)/battle_ai.o $(OBJ)/battle.o $(OBJ)/pebble.o \
                        $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

# THE SHARED VALIDATOR (P4-C5, spec section 15). validate.cpp is PURE, so it
# compiles here exactly as it does in the firmware. pebble.o is the one stat
# derivation it calls (hp_max through the SAME function battle.cpp uses),
# evolution.o + xp.o come as the pair they always do (they call each other), and
# genome.o drags crc16.o and rng.o. battle.o is here for the CONTAINMENT case:
# the claim "validate_team accepts => battle_init does not refuse for any reason
# but BR_MEMBER_FAINTED" is only checkable with the real engine in the binary.
# species_custom.o SINCE P8-C3, and it is the point of the creator cases rather
# than a convenience: "a creator Pebble validates" is only a claim about the
# SHIPPING firmware if species_get() really resolves the id through the real
# registry. A fixture that stubbed the resolver would have proved that the stub
# resolves.
$(BIN)/test_validate:   $(OBJ)/validate.o $(OBJ)/pebble.o $(OBJ)/evolution.o \
                        $(OBJ)/xp.o $(OBJ)/genome.o $(OBJ)/crc16.o \
                        $(OBJ)/rng.o $(OBJ)/battle.o $(OBJ)/box.o \
                        $(OBJ)/species_custom.o

# THE SECTION 15 CODEC (P4-C5). protocol.o on top of the validator's objects,
# because pbw_decode() IS a decode in front of validate_pebble(). rng.o is also
# the fuzzer's source: every fuzz frame is a pure function of one printable u32
# drawn through core/rng.h, so a failure reproduces from the seed alone.
$(BIN)/test_protocol:   $(OBJ)/protocol.o $(OBJ)/validate.o $(OBJ)/pebble.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o

# THE SESSION FSM, THE LOCKSTEP AND THE FAULT-INJECTING LOOPBACK (P4-C5, the
# second half). Everything the codec test links, plus the three new pure
# networking modules and the REAL battle engine and the REAL local AI: the two
# endpoints each run their own game/battle_ai.cpp over their own side, so a
# trial is a whole battle between two engines and not a script. box.o comes
# along for nothing else than the shared fixture builder the validator test
# already uses.
$(BIN)/test_session:    $(OBJ)/session.o $(OBJ)/battle_link.o \
                        $(OBJ)/trade_link.o \
                        $(OBJ)/transport_loopback.o $(OBJ)/protocol.o \
                        $(OBJ)/validate.o $(OBJ)/battle.o $(OBJ)/battle_ai.o \
                        $(OBJ)/pebble.o $(OBJ)/evolution.o $(OBJ)/xp.o \
                        $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

# P5-C1: THE SCANNER, THE CLASSIFIER AND THE SALTED IDENTITY. Two objects and
# NO OTHERS - net_classify.cpp and wifi_scanner.cpp are pure translation units,
# and the scan job reaches the radio through a struct of function pointers, so
# this binary drives every path a real scan can take with no radio and no
# net.cpp (which is a device module tests/Makefile never compiles). If this link
# line ever needs a third object, something has leaked into the pure half.
$(BIN)/test_exploration_hash: $(OBJ)/net_classify.o $(OBJ)/wifi_scanner.o

# P5-C2: THE COOLDOWN TABLE. cooldowns.cpp is pure and takes its clock as a
# struct, so the uncalibrated path and a calibration landing mid-session are
# driven directly. The persistence objects are here for ONE claim the module
# cannot make on its own - that an armed cooldown survives a reboot - and they
# are the same set tests/test_persistence.cpp links, for the same reason:
# save_manager.cpp ends in the shared validator.
$(BIN)/test_cooldowns:  $(OBJ)/cooldowns.o $(OBJ)/save_manager.o \
                        $(OBJ)/migration.o $(OBJ)/kv_mem.o $(OBJ)/crc16.o \
                        $(OBJ)/genome.o $(OBJ)/rng.o $(OBJ)/species.o \
                        $(OBJ)/validate.o $(OBJ)/pebble.o $(OBJ)/evolution.o \
                        $(OBJ)/xp.o $(OBJ)/species_custom.o

# P5-C3: THE ENCOUNTER ROLL. encounters.cpp is pure and deterministic from its
# inputs - no clock, no global RNG stream - so this binary is the roll and the
# generated tables and nothing else. species.o is the accessor layer the WILD
# and ITEM pickers go through; rng.o is the local Rng the seed drives.
$(BIN)/test_encounters: $(OBJ)/encounters.o $(OBJ)/species.o $(OBJ)/rng.o \
                        $(OBJ)/corruption.o
# corruption.o because the SPECIAL outcome's second kind IS the corruption
# status: the encounter is the only writer of PBS_CORRUPTED in the tree, so
# the status and its 24 h deadline are tested where they are produced.

# P5-C4: CAPTURE. The REAL box.cpp, because the whole point of the case set is
# that a captured Pebble comes out of the tree's one constructor and passes the
# tree's one validator - a fake Box would prove neither. validate.o drags the
# pair it always drags (evolution.o + xp.o call each other) and genome.o drags
# crc16.o and rng.o, exactly as tests/test_validate.cpp does.
$(BIN)/test_capture:    $(OBJ)/capture.o $(OBJ)/encounters.o $(OBJ)/box.o \
                        $(OBJ)/validate.o $(OBJ)/species.o $(OBJ)/pebble.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o

# The Box-full branches of the same path, kept in their own binary for the
# reason tests/test_battle_screen.cpp is: it links what it drives and nothing
# else, so it cannot start passing because another module happened to be there.
$(BIN)/test_box_full_capture: $(OBJ)/capture.o $(OBJ)/encounters.o $(OBJ)/box.o \
                        $(OBJ)/validate.o $(OBJ)/species.o $(OBJ)/pebble.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o

# P5-C4: THE INVENTORY AND WHAT AN ITEM DOES. xp.o + evolution.o are the two
# consumers inv_use() routes to (a candy really levels a Pebble, the evolution
# key really asks game/evolution.h), corruption.o is the status half a CARE
# item's `clears` column reaches, and species.o is the accessor layer.
$(BIN)/test_inventory:  $(OBJ)/inventory.o $(OBJ)/corruption.o $(OBJ)/xp.o \
                        $(OBJ)/evolution.o $(OBJ)/species.o $(OBJ)/pebble.o \
                        $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

# P6-C1: THE TONE ENGINE AND THE MOTION CAPABILITY. audio.cpp keeps its LEDC
# half behind `#if defined(ARDUINO)` exactly as input.cpp keeps its
# digitalRead(), so ONE object and no others is the whole binary: the queue, the
# step clock and both halves of CF_MUTE are decided above the sink, and the test
# binds a recorder in place of a piezo. If this link line ever needs a second
# object, something device-shaped has leaked below the seam.
$(BIN)/test_audio:      $(OBJ)/audio.o
$(BIN)/test_motion:     $(OBJ)/motion_null.o

# P6-C2: THE DAILY ACTIVITY SCORE. activity.cpp is pure and takes its clock as a
# struct, exactly as cooldowns.cpp does, so the uncalibrated path, a clock rolled
# backwards and a device power-cycled thirty times are all driven directly with
# no device and no save manager. ONE OBJECT AND NO OTHERS is the point of this
# line: the score reaches no content table, no RNG and no persistence code, so
# if it ever needs a second object something has leaked into the pure half.
$(BIN)/test_activity:   $(OBJ)/activity.o

# P6-C3: THE IDLE LADDER. power.cpp is host-compilable by construction (every
# ESP-IDF call is behind #if defined(ARDUINO)), and wifi_scanner.o comes with it
# for ONE reason worth naming: the carried radio debt is only a real property if
# the case holds a REAL WifiScanJob while the ladder drops the radio. A fake job
# would have proved that the fake behaves, which is the shape of test this
# project keeps finding.
$(BIN)/test_power:      $(OBJ)/power.o $(OBJ)/wifi_scanner.o

# P7-C1: THE BEACON CODEC, THE PEER TABLE AND THE DISCOVERY JOB. discovery.cpp
# is pure and reaches the radio through a LinkRadioDriver of function pointers,
# so the fake driver at the bottom of the test drives every path a real link can
# take - including the two that are hardest to provoke on hardware, the section
# 47 ceiling and a bring-up that does not take.
#
# activity.o IS THE POINT OF THIS LINK LINE AND NOT A CONVENIENCE. The rule
# "act_note_peer() once per peer that passes the gate, never once per beacon"
# is a property of the peer table and the activity score TOGETHER, and P6-C2
# learned - from an XP farm that shipped - that neither module's own binary can
# see a cross-module property. crc16.o is what the beacon is sealed with.
$(BIN)/test_discovery:  $(OBJ)/discovery.o $(OBJ)/activity.o $(OBJ)/crc16.o

# P7-C1: THE RING THE RADIO CALLBACK POSTS INTO, AND THE SEAM OVER IT.
# Two halves in one binary, deliberately. The first drives rxring.cpp directly:
# overflow, oversize, wrap and the invariant that a pop returns 0 only when the
# ring is empty. The second runs a WHOLE BATTLE between two real sessions over
# a Transport whose recv() is rxring_pop() and whose send() is rxring_push() -
# the same ring code networking/transport_espnow.cpp fills from the Wi-Fi task -
# so the claim "the session runs over the radio's ring with NO change to
# session.cpp" is a thing this binary demonstrates rather than a sentence in a
# header. That is why the session's whole object set is here.
$(BIN)/test_link_transport: $(OBJ)/rxring.o $(OBJ)/session.o $(OBJ)/battle_link.o \
                        $(OBJ)/trade_link.o \
                        $(OBJ)/protocol.o $(OBJ)/validate.o $(OBJ)/battle.o \
                        $(OBJ)/battle_ai.o $(OBJ)/pebble.o $(OBJ)/evolution.o \
                        $(OBJ)/xp.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

# P7-C5: BREEDING. The REAL game/genome.cpp (breeding is a thin policy over it),
# the REAL game/box.cpp (the child is filed by the tree's one constructor) and
# the REAL game/validate.cpp (it is run on the child before the slot is kept).
# species.o is the accessor layer species_base_of_family() lives in, and it is
# also the translation unit that instantiates every generated table's guards.
# NO networking object: game/breeding.cpp is pure and the shared seed arrives as
# a uint32_t, which is the whole reason the determinism case can run two
# "devices" in one process.
$(BIN)/test_breeding:   $(OBJ)/breeding.o $(OBJ)/box.o $(OBJ)/validate.o \
                        $(OBJ)/species.o $(OBJ)/pebble.o $(OBJ)/evolution.o \
                        $(OBJ)/xp.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

# P7-C4: THE ATOMIC TRADE. The whole binary is REAL SHIPPING CODE and one fake:
# game/trade.cpp drives the journal, persistence/save_manager.cpp performs the
# writes, tests/fakes/kv_mem.cpp is the store that can be killed at write k, and
# game/box.cpp is the Box the invariant is asserted on. The networking half -
# session.o, trade_link.o, protocol.o and the loopback - is here because the
# five WIRE steps and the three JOURNAL phases are one property, and the only
# way to see the pair invariant is to run both endpoints over a real link.
#
# migration.o comes with save_manager.o as it always does; validate.o drags the
# pair it always drags (evolution.o + xp.o call each other); species.o is the
# accessor layer AND the translation unit that instantiates every generated
# table's compile-time guards.
$(BIN)/test_trade:      $(OBJ)/trade.o $(OBJ)/box.o $(OBJ)/save_manager.o \
                        $(OBJ)/migration.o $(OBJ)/kv_mem.o $(OBJ)/species_custom.o \
                        $(OBJ)/trade_link.o $(OBJ)/session.o \
                        $(OBJ)/battle_link.o $(OBJ)/battle.o $(OBJ)/battle_ai.o \
                        $(OBJ)/transport_loopback.o $(OBJ)/protocol.o \
                        $(OBJ)/validate.o $(OBJ)/species.o $(OBJ)/pebble.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o

# P8-C1/C2: THE CREATOR PIN AND THE PORTAL IDLE TIMER. creator_gate.cpp is pure
# and caller-owned - the clock, the persisted failure count and the candidate
# PIN all arrive as arguments - so ONE OBJECT AND NO OTHERS is the whole binary.
# That is the same argument test_activity's line makes, and it has the same
# tripwire: if this line ever needs a second object, something device-shaped has
# leaked into the rules and webui.cpp has started deciding things again.
$(BIN)/test_creator_gate: $(OBJ)/creator_gate.o

# P8-C3: THE RAW-BODY CAP, THE FIXED-SCHEMA READER AND THE CREATOR PIPELINE.
#
# THREE PURE MODULES AND THE REAL VALIDATOR, AND NO DEVICE OBJECT AT ALL.
# networking/creator_server.cpp is where the sockets are and tests/Makefile
# never compiles it (it includes Arduino.h and WebServer.h), so everything this
# binary drives is the half that DECIDES: creator_body.o is the cap, with the
# core's HTTP_RAW_BUFLEN chunking reproduced by the fixture; creator_parse.o is
# the document reader; creator_gate.o is the PIN parser the reader shares;
# validate.o + species_custom.o + box.o are the rules and the pipeline the
# accepted document runs through, so the end-to-end case files a REAL Pebble in
# a REAL Box through the tree's one constructor and runs the tree's one
# validator on it.
#
# THE FIXTURE STATES WHERE IT DIFFERS FROM app_setup(), in its own banner: there
# is no socket, so the chunk sizes and the RAW_* sequence are reproduced from
# the pinned core rather than delivered by it, and the response codes are
# creator_server.cpp's one-line mapping of the states this binary asserts.
$(BIN)/test_creator_api: $(OBJ)/creator_body.o $(OBJ)/creator_parse.o \
                        $(OBJ)/creator_gate.o $(OBJ)/validate.o \
                        $(OBJ)/species_custom.o $(OBJ)/box.o $(OBJ)/species.o \
                        $(OBJ)/pebble.o $(OBJ)/evolution.o $(OBJ)/xp.o \
                        $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o

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
# THE SAME MODULE, COMPILED FOR THE DEVICE (P7-C6). A separate binary and not a
# second object in test_clock: two gametime.o would be two definitions of every
# symbol in it. game_state.o + save_manager.o + kv_mem.o are here because
# gt_load_seed()'s device branch really reads save_last_seen(), gs_boot_cal()
# and gs_boot_tz() - stubbing them would be testing a gametime the board does
# not have.
$(BIN)/test_clock_device: $(OBJ)/gametime_arduino.o $(OBJ)/arduino_clocks.o \
                        $(OBJ)/cooldowns.o $(OBJ)/game_state.o \
                        $(OBJ)/save_manager.o $(OBJ)/migration.o $(OBJ)/kv_mem.o \
                        $(OBJ)/boot_host.o $(OBJ)/crc16.o $(OBJ)/genome.o $(OBJ)/rng.o \
                        $(OBJ)/species.o $(OBJ)/validate.o $(OBJ)/pebble.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o $(OBJ)/species_custom.o
$(BIN)/test_overflow:   $(OBJ)/gametime_nevervalid.o $(OBJ)/host_shims.o \
                        $(OBJ)/sim.o $(OBJ)/daylight.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o
$(BIN)/test_fixtures:   $(OBJ)/crc16.o
# genome.o + species.o since P4-C1: the legacy family map case drives
# gene_set_species() and reads back through species_base_of_family(), which is
# the only way to prove that every v1 family lands on a BASE-stage creature
# rather than on nothing. genome.o drags rng.o.
# validate.o + its pair since P4-C5: save_load_all() ends in the shared
# validator (the quarantine), so the load path drags the section 15 rules the
# same way the wire path does.
$(BIN)/test_persistence: $(OBJ)/save_manager.o $(OBJ)/migration.o \
                        $(OBJ)/kv_mem.o $(OBJ)/crc16.o \
                        $(OBJ)/genome.o $(OBJ)/rng.o $(OBJ)/species.o \
                        $(OBJ)/validate.o $(OBJ)/pebble.o $(OBJ)/evolution.o \
                        $(OBJ)/xp.o $(OBJ)/species_custom.o
$(BIN)/test_game_state: $(OBJ)/game_state.o $(OBJ)/save_manager.o $(OBJ)/migration.o \
                        $(OBJ)/kv_mem.o $(OBJ)/boot_host.o $(OBJ)/crc16.o $(OBJ)/rng.o \
                        $(OBJ)/validate.o $(OBJ)/pebble.o $(OBJ)/evolution.o \
                        $(OBJ)/xp.o $(OBJ)/genome.o $(OBJ)/species_custom.o
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
               $(OBJ)/discovery.o $(OBJ)/session.o $(OBJ)/battle_link.o \
               $(OBJ)/trade_link.o $(OBJ)/trade.o \
               $(OBJ)/protocol.o $(OBJ)/link_fake.o \
               $(OBJ)/screen_evolution.o $(OBJ)/screen_creator.o \
               $(OBJ)/screen_network.o $(OBJ)/screen_encounter.o \
               $(OBJ)/wifi_scanner.o $(OBJ)/net_classify.o \
               $(OBJ)/cooldowns.o $(OBJ)/activity.o \
               $(OBJ)/encounters.o $(OBJ)/capture.o \
               $(OBJ)/inventory.o $(OBJ)/corruption.o $(OBJ)/species.o \
               $(OBJ)/validate.o $(OBJ)/evolution.o $(OBJ)/xp.o \
               $(OBJ)/dialog.o $(OBJ)/qr.o $(OBJ)/box.o \
               $(OBJ)/gfx_widgets.o $(OBJ)/gfx_fb.o \
               $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o \
               $(OBJ)/audio.o \
               $(OBJ)/screen_battle.o $(OBJ)/battle_renderer.o \
               $(OBJ)/xbm_mirror.o \
               $(OBJ)/battle.o $(OBJ)/battle_ai.o $(OBJ)/pebble.o

# P7-C2 added the peer link's four: ui/screen_link.cpp owns a REAL LinkJob and a
# REAL Session over a REAL codec, and the screen binaries link all of it because
# a screen test against a fake session would prove nothing about the thing that
# ships. The RADIO is still a fake - net.cpp is never compiled here - and so is
# the transport: the screen reaches both through ui.h seams that
# tests/test_screens.cpp and tests/test_link_screen.cpp fill in themselves.
#
# P6-C2 added activity.o: ui/screen_network.cpp hands the WHOLE scan array to
# the daily score before it walks the cooldowns, and reads the rare-encounter
# permille back out of it for EncounterInput. activity.cpp is pure and takes its
# clock as a struct, so the screen binaries link the REAL score with no clock
# bound but the one the screen assembles from ui_explore_clock() - which is how
# tests/test_screens.cpp can drive an uncalibrated scan at all.
#
# P6-C1 added audio.o: ui/screen_battle.cpp arms one cue per battle beat, and
# hardware/audio.cpp is a pure translation unit (its LEDC half is behind
# `#if defined(ARDUINO)`), so the screen binaries link the REAL engine with no
# sink bound - which is the silent, side-effect-free default and needs no stub.
#
# P5-C3/C4 added the exploration half. ui/screen_network.cpp and
# ui/screen_encounter.cpp are pure translation units like every other migrated
# screen, and they drag the real modules they drive - the scan job, the
# classifier, the cooldown table, the encounter roll, capture and the inventory
# - because a screen test against fakes of all six would prove nothing about the
# thing that ships. The RADIO is still a fake: net.cpp is never compiled here,
# and the screen reaches it through ui_scan_driver(), which test_screens.cpp
# stubs with a driver of its own.
#
# P4-C4 added the six before them. ui/screen_battle.cpp is a PURE translation unit that
# runs the real engine and the real AI, so the host renders a real battle at
# 128x64 rather than a mock of one: battle.o and battle_ai.o are the same
# objects tests/test_battle.cpp links, pebble.o is the stat derivation they
# call, and xbm_mirror.o is ui/petfx.cpp's own horizontal flip - which nothing
# in this repository compiled or executed until it moved out of that device-only
# translation unit.

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

# P7-C2/C3: THE LINK SCREEN, ITS CONSENT GATE AND THE BATTLE OVER THE LINK.
#
# ONE SCREEN AND ONE REAL PEER, IN ONE PROCESS, and that shape is the whole
# design of this binary. A screen is a file-scope singleton, so two LINK screens
# cannot coexist here - but a Session is CALLER-OWNED (networking/session.h says
# why), so the far end is a real Session with a real BattleState and a real
# game/battle_ai.cpp driving it over the other end of a real
# transport_loopback.cpp. Nothing in this binary is a mock of the protocol: what
# is faked is the RADIO (a LinkRadioDriver of function pointers, exactly as
# tests/test_discovery.cpp fakes it) and the ui.h seams.
#
# screen_battle.o is here because P7-C3's battle IS that screen, and box.o
# because the team the screen offers comes out of the tree's one Box.
$(BIN)/test_link_screen: $(OBJ)/screen_link.o $(OBJ)/screen_battle.o \
                        $(OBJ)/battle_renderer.o $(OBJ)/xbm_mirror.o \
                        $(OBJ)/discovery.o $(OBJ)/session.o $(OBJ)/battle_link.o \
                        $(OBJ)/trade_link.o $(OBJ)/trade.o \
                        $(OBJ)/transport_loopback.o $(OBJ)/protocol.o \
                        $(OBJ)/validate.o $(OBJ)/battle.o $(OBJ)/battle_ai.o \
                        $(OBJ)/box.o $(OBJ)/species.o $(OBJ)/pebble.o \
                        $(OBJ)/cooldowns.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o $(OBJ)/activity.o \
                        $(OBJ)/inventory.o $(OBJ)/corruption.o \
                        $(OBJ)/audio.o $(OBJ)/genome.o $(OBJ)/crc16.o \
                        $(OBJ)/rng.o $(OBJ)/gfx_widgets.o $(OBJ)/gfx_fb.o \
                        $(OBJ)/link_fake.o

# THE BATTLE SCREEN'S BEHAVIOUR (P4-C4). Deliberately NOT $(SCREEN_OBJS): this
# binary links the battle screen and the things it actually calls, and nothing
# else, so it cannot start passing because some other screen happens to define
# a seam it needs. gfx_widgets.o + gfx_fb.o come along because the screen draws
# and because the mirror case reads pixels back.
# inventory.o + its two consumers since P5-C4: the battle screen takes the armed
# BATTLE_MOD from it, which is where a capture-menu item finally reaches a fight.
$(BIN)/test_battle_screen: $(OBJ)/screen_battle.o $(OBJ)/battle_renderer.o \
                        $(OBJ)/audio.o \
                        $(OBJ)/xbm_mirror.o $(OBJ)/battle.o $(OBJ)/battle_ai.o \
                        $(OBJ)/pebble.o $(OBJ)/box.o $(OBJ)/genome.o \
                        $(OBJ)/crc16.o $(OBJ)/rng.o \
                        $(OBJ)/inventory.o $(OBJ)/corruption.o $(OBJ)/xp.o \
                        $(OBJ)/evolution.o $(OBJ)/species.o \
                        $(OBJ)/gfx_widgets.o $(OBJ)/gfx_fb.o

# The navigation machine and the section 7 router, over the REAL table.
$(BIN)/test_statemachine: $(SCREEN_OBJS) $(OBJ)/state_machine.o \
                        $(OBJ)/input_router.o $(OBJ)/host_shims.o
# pet_view.cpp is the model->view seam petfx and actfx read (P2-C11c). It is
# pure, so it compiles here; sim.o comes with it because the live fill reads
# sim_stat_pct() / sim_mood_score() - and since P4-C4a because the care case
# drives a REAL bound simulation through sim_god_set_stat().
# evolution.o + xp.o as well: the evolution cases evolve a Pebble and re-fill
# its PetView through pet_view_fill_sim() + pet_view_attach(), which IS the pair
# ui.cpp:201-203 calls. (Until P4-C4a they drove pet_view_fill(), a function no
# firmware file called; that function is deleted and this comment no longer
# has to explain why the case proved only the model half.)
$(BIN)/test_pet_view:   $(OBJ)/pet_view.o $(OBJ)/sim.o $(OBJ)/daylight.o \
                        $(OBJ)/box.o $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o \
                        $(OBJ)/evolution.o $(OBJ)/xp.o

$(BIN)/test_%: test_%.cpp nt_test.h | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(filter %.o,$^) -o $@

# --- goldens ----------------------------------------------------------------
golden: $(BIN)/test_care_golden $(BIN)/test_battle_golden
	./$(BIN)/test_care_golden --record
	./$(BIN)/test_battle_golden --record

# Screen snapshots (golden/screens/*.pbm). Same rule as above: only from a
# commit that means to change what a screen looks like, and read the diff.
screens: $(BIN)/test_screens
	./$(BIN)/test_screens --record

# --- legacy fixtures (v1 PetSave / Config / GainSave blobs, plan P2-C2) ------
$(BIN)/mkfixtures: legacy/mkfixtures.cpp $(OBJ)/genome.o $(OBJ)/crc16.o $(OBJ)/rng.o | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(filter %.o,$^) -o $@

fixtures: $(BIN)/mkfixtures
	./$(BIN)/mkfixtures fixtures
