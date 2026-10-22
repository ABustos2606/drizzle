#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos
#
# Copy the upstream files Drizzle needs into third_party/eurorack/, at the
# pinned commits, and apply Drizzle's local changes from tools/patches/. Run by
# hand (it needs network access); the build never runs it.
# Usage: tools/vendor.sh [scratch-dir]
set -eu

EURORACK_URL=https://github.com/pichenettes/eurorack.git
EURORACK_COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
# The stmlib submodule commit recorded in eurorack's tree at EURORACK_COMMIT.
STMLIB_URL=https://github.com/pichenettes/stmlib.git
STMLIB_COMMIT=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec

# All of Beads' dsp/ (the processor and the synth), its resources, and the
# stmlib files they include (found with g++ -MM). Upstream's LICENSE file is
# stmlib's; the Beads files carry their notice at the top.
BEADS_FILES="
beads/dsp/delay_clock.cc
beads/dsp/delay_clock.h
beads/dsp/delay_controller.cc
beads/dsp/delay_controller.h
beads/dsp/delay_grain.h
beads/dsp/delay_granular_player.h
beads/dsp/delay_line.h
beads/dsp/frame.h
beads/dsp/fx/fx_engine.h
beads/dsp/fx/reverb.h
beads/dsp/fx/wow_and_flutter.h
beads/dsp/grain.h
beads/dsp/grain_envelope.h
beads/dsp/granular_player.h
beads/dsp/granular_processor.h
beads/dsp/limiter.h
beads/dsp/modulated_parameter.h
beads/dsp/mu_law.cc
beads/dsp/mu_law.h
beads/dsp/multitap_delay.h
beads/dsp/processor.cc
beads/dsp/processor.h
beads/dsp/randomized_parameter.h
beads/dsp/sample_rate_conversion_filters.h
beads/dsp/sine_oscillator.h
beads/dsp/synth.cc
beads/dsp/synth.h
beads/dsp/tap.h
beads/dsp/trigger_generator.cc
beads/dsp/trigger_generator.h
beads/dsp/voice_stealing_envelope.h
beads/dsp/wavetable_grain.h
beads/dsp/wavetable_granular_player.h
beads/dsp/wavetable_voice.h
beads/resources.cc
beads/resources.h
"
STMLIB_FILES="
LICENSE
stmlib.h
dsp/cosine_oscillator.h
dsp/delay_line.h
dsp/dsp.h
dsp/filter.h
dsp/hysteresis_filter.h
dsp/hysteresis_quantizer.h
dsp/parameter_interpolator.h
dsp/sample_rate_converter.h
dsp/units.cc
dsp/units.h
utils/dsp.h
utils/gate_flags.h
utils/random.cc
utils/random.h
"

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=${1:-$ROOT/tmp/vendor}
DEST=$ROOT/third_party/eurorack

fetch() { # url commit dir
    rm -rf "$3"; mkdir -p "$3"
    git -C "$3" init -q
    git -C "$3" fetch -q --depth 1 "$1" "$2"
    git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

fetch "$EURORACK_URL" "$EURORACK_COMMIT" "$SCRATCH/eurorack"
fetch "$STMLIB_URL" "$STMLIB_COMMIT" "$SCRATCH/stmlib"
test "$(git -C "$SCRATCH/eurorack" ls-tree HEAD stmlib | awk '{print $3}')" = "$STMLIB_COMMIT" ||
    { echo "stmlib commit does not match eurorack's submodule" >&2; exit 1; }

rm -rf "$DEST/beads" "$DEST/stmlib"
for f in $BEADS_FILES; do
    mkdir -p "$DEST/$(dirname "$f")"; cp "$SCRATCH/eurorack/$f" "$DEST/$f"
done
for f in $STMLIB_FILES; do
    mkdir -p "$DEST/stmlib/$(dirname "$f")"; cp "$SCRATCH/stmlib/$f" "$DEST/stmlib/$f"
done
# Drizzle's local changes, listed in third_party/eurorack/README.md.
for p in "$ROOT"/tools/patches/*.patch; do
    [ -e "$p" ] || continue
    patch -s -p1 -d "$DEST" < "$p"
done
echo "vendored eurorack $EURORACK_COMMIT and stmlib $STMLIB_COMMIT into $DEST"
