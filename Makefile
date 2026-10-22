# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos

PREFIX ?= /usr/local
LV2DIR ?= $(PREFIX)/lib/lv2
DESTDIR ?=
OPT ?= -O2
PYTHON ?= python3
PKG_CONFIG ?= pkg-config
SAN ?= 0
BUILD_DIR ?= build
ifeq ($(SAN),1)
override BUILD_DIR := $(BUILD_DIR)/san
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
endif
ifeq ($(SAN),thread)
override BUILD_DIR := $(BUILD_DIR)/tsan
SAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g
endif

URIS := https://github.com/ABustos2606/drizzle\#drizzle https://github.com/ABustos2606/drizzle\#synth
BUNDLE := $(BUILD_DIR)/drizzle.lv2
VENDOR := third_party/eurorack
SOURCES := src/lv2_entry.cpp src/drizzle.cpp src/drizzle_synth.cpp src/controls.cpp
VENDOR_SOURCES := $(addprefix $(VENDOR)/,beads/dsp/delay_clock.cc beads/dsp/delay_controller.cc \
	beads/dsp/mu_law.cc beads/dsp/processor.cc beads/dsp/synth.cc beads/dsp/trigger_generator.cc \
	beads/resources.cc stmlib/dsp/units.cc stmlib/utils/random.cc)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/obj/%.o,$(SOURCES))
VENDOR_OBJECTS := $(patsubst $(VENDOR)/%.cc,$(BUILD_DIR)/obj/vendor/%.o,$(VENDOR_SOURCES))
METADATA := manifest.ttl drizzle.ttl drizzle_synth.ttl
TTL_TARGETS := $(addprefix $(BUNDLE)/,$(METADATA))
LV2_CFLAGS = $(shell $(PKG_CONFIG) --cflags lv2)
# TEST selects stmlib's portable C++ instead of Cortex-M assembly, as
# upstream's own desktop test build does. -fno-fast-math comes after OPT: the
# non-finite guards need IEEE arithmetic even if a host build passes
# -ffast-math in OPT. initial-exec puts stmlib::Random's thread_local state
# (third_party/eurorack/README.md) in the static TLS block set up at dlopen,
# so the first run() on a new host thread does not allocate it.
BASE_FLAGS = $(CPPFLAGS) $(CXXFLAGS) $(OPT) -std=c++17 -fPIC -fvisibility=hidden -DTEST \
	-ftls-model=initial-exec -fno-fast-math -fno-math-errno -fno-trapping-math $(SAN_FLAGS)
COMPILE_FLAGS = $(BASE_FLAGS) $(LV2_CFLAGS) -isystem $(VENDOR) -Wall -Wextra
# Vendored code is kept as upstream wrote it; its warnings are not ours.
VENDOR_FLAGS = $(BASE_FLAGS) -I$(VENDOR) -w
TEST_LIBS = -ldl -pthread
TESTS := test_scaffold test_match test_controls test_resampler

.PHONY: all install test test-timing test-hosts render lint clean
all: $(BUNDLE)/drizzle.so $(TTL_TARGETS)

$(BUILD_DIR)/obj/%.o: src/%.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/vendor/%.o: $(VENDOR)/%.cc
	@mkdir -p "$(@D)"
	$(CXX) $(VENDOR_FLAGS) -MMD -MP -c $< -o $@

$(BUNDLE)/drizzle.so: $(OBJECTS) $(VENDOR_OBJECTS) src/exports.map
	@mkdir -p "$(@D)"
	$(CXX) $(OBJECTS) $(VENDOR_OBJECTS) $(LDFLAGS) $(SAN_FLAGS) -shared -Wl,--no-undefined \
		-Wl,--version-script=src/exports.map -o $@

$(BUNDLE)/%.ttl: drizzle.lv2/%.ttl
	@mkdir -p "$(@D)"
	install -m 644 $< $@

$(BUILD_DIR)/test_scaffold: tests/test_scaffold.cpp tests/host.cpp tests/host.hpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_scaffold.cpp tests/host.cpp $(LDFLAGS) $(TEST_LIBS) -o $@

$(BUILD_DIR)/test_resampler: tests/test_resampler.cpp src/rate_bridge.hpp src/resampler.hpp src/block_buffer.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_resampler.cpp $(LDFLAGS) -o $@

# Drive the vendored Processor and the control code directly, beside the
# plugin, so they link them too.
$(BUILD_DIR)/test_match: tests/test_match.cpp tests/host.cpp tests/host.hpp $(BUILD_DIR)/obj/controls.o $(VENDOR_OBJECTS)
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_match.cpp tests/host.cpp $(BUILD_DIR)/obj/controls.o $(VENDOR_OBJECTS) \
		$(LDFLAGS) $(TEST_LIBS) -o $@

$(BUILD_DIR)/test_controls: tests/test_controls.cpp $(BUILD_DIR)/obj/controls.o $(VENDOR_OBJECTS)
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_controls.cpp $(BUILD_DIR)/obj/controls.o $(VENDOR_OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/render: tools/render.cpp tests/host.cpp tests/host.hpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tools/render.cpp tests/host.cpp $(LDFLAGS) $(TEST_LIBS) -o $@

render: all $(BUILD_DIR)/render

test: all $(addprefix $(BUILD_DIR)/,$(TESTS)) $(BUILD_DIR)/render
	$(PYTHON) tests/ttl_check.py $(BUNDLE)
	$(BUILD_DIR)/test_scaffold $(BUNDLE)/drizzle.so
	$(BUILD_DIR)/test_match $(BUNDLE)/drizzle.so
	$(BUILD_DIR)/test_controls
	$(BUILD_DIR)/test_resampler
	$(BUILD_DIR)/render $(BUNDLE)/drizzle.so $(BUILD_DIR)/render-check.wav --seconds 1 dry_wet=0.8

# rt_timing: 10 simulated minutes of random controls.
test-timing: all $(BUILD_DIR)/test_scaffold
	$(BUILD_DIR)/test_scaffold $(BUNDLE)/drizzle.so --timing

# jalv and Ingen with a private dummy JACK server; Ingen must be on PATH
# (Peepo's tools/ingen-spike/env.sh sets it up).
test-hosts: all
	$(PYTHON) tests/host_smoke.py $(BUNDLE)

install: all
	install -d "$(DESTDIR)$(LV2DIR)/drizzle.lv2"
	install -m 755 $(BUNDLE)/drizzle.so "$(DESTDIR)$(LV2DIR)/drizzle.lv2/"
	install -m 644 $(TTL_TARGETS) LICENSE THIRD_PARTY_NOTICES "$(DESTDIR)$(LV2DIR)/drizzle.lv2/"

lint:
	@if command -v lv2lint >/dev/null 2>&1; then \
		for uri in $(URIS); do \
			LV2_PATH="$(DESTDIR)$(LV2DIR):$${LV2_PATH:-/usr/local/lib/lv2:/usr/lib/lv2}" lv2lint "$$uri" || exit 1; \
		done; \
	else echo 'SKIP lv2lint: not installed (install the bundle with make install first)'; fi

clean:
	$(RM) -r $(BUILD_DIR)/obj $(BUNDLE) $(addprefix $(BUILD_DIR)/,$(TESTS)) $(BUILD_DIR)/render $(BUILD_DIR)/render-check.wav

-include $(OBJECTS:.o=.d) $(VENDOR_OBJECTS:.o=.d)
