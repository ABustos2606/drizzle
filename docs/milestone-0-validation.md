<!-- SPDX-License-Identifier: MIT -->
# Milestone 0 validation

Recorded 21 October 2026. The scaffold is built: vendored code, Makefile, both descriptors in one `drizzle.so`, Turtle with the full port lists (Drizzle 42 ports, Drizzle Synth 36), `ports.hpp` and `ttl_check`. Drizzle runs `beads::Processor` at its default settings; Drizzle Synth loads and is silent until milestone 2. Milestone 1 was built in the same session, so the results below were taken on the milestone 1 code, which is a superset (see [milestone 1](milestone-1-validation.md)).

| Check | Result |
| --- | --- |
| `tools/vendor.sh` | eurorack `f4cfa769`, stmlib `e3bd7c9c` (eurorack's submodule at that commit); 52 files; three local patches; LF; a second run gives identical files |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS; GCC 14.2, LV2 1.18.10; no warnings from Drizzle's code |
| `make test SAN=1` | PASS; AddressSanitizer and UndefinedBehaviorSanitizer |
| `make test SAN=thread` | PASS (run under `setarch -R`: TSan cannot map memory with WSL's default ASLR) |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"` | PASS; Debian 12 root `/opt/peepo-arm64`, GCC 12.2, QEMU user emulation |
| `make install DESTDIR=build/stage PREFIX=/opt/peepo` | PASS; `drizzle.so`, the three Turtle files, `LICENSE`, `THIRD_PARTY_NOTICES` |
| `lv2info` on the staged bundle | PASS; Drizzle 42 ports, Drizzle Synth 36 |
| `nm -D --defined-only drizzle.so` | Only `lv2_descriptor` |
| `lv2lint` 0.17.151 (`9661902c`, online, ELF and X11 tests disabled), both plugins | No errors; one warning, below |
| `make test-hosts`, jalv 1.6.8 | Drizzle loads, silent with silent input, a tone comes through; Drizzle Synth loads |
| `make test-hosts`, Peepo's Ingen, 1 and 4 threads, 1024- and 128-frame periods | Two Drizzles and a Drizzle Synth in one graph; a tone comes through both Drizzles (one in digital, one in vintage digital); Quality, Time, Density, Freeze, Seed lock, Seed uses gate, Output and On save and restore; no LV2 state saved |

`lv2lint` warns **Plugin Author Email**: no `foaf:mbox` is given. The name and GitHub homepage are there; an email address is not invented or published (as Discipline and Chamber).

## What the offline tests cover

- `ttl_check`: for both plugins, indices, symbols and kinds match `src/ports.hpp`; every symbol is frozen in order; names ASCII and at most 16 characters, with no upstream names; bounds and defaults; the Quality and CV-assign labels; toggles; Pitch as whole semitones; the `enabled` and latency designations; CV ranges; license; no required features. The C++ fallbacks for an unconnected control and the test host's starting values agree with the Turtle defaults.
- `scaffold` (milestone 0 parts): two descriptors; invalid rates rejected; 24, 32, 44.1, 48 and 96 kHz accepted; unconnected ports, unknown indices and zero-frame runs; default settings play grains; Drizzle Synth silent; identical output for whole periods, random slices and a 37-frame period; instances keep their own random sequence alone, interleaved on one thread and on two threads; `activate` restarts the processor exactly; no `malloc` or `calloc` inside `run()` on a new thread.

## Decisions taken in this milestone

Recorded in the spec (Decisions, 21 October 2026): any host rate is accepted and resampled; dirty tape runs at 24 kHz; an `lv2:enabled` port (bypass); four ports appended to the plan's table (`seed_uses_gate`, `auto_gain`, `enabled`, `latency`); Drizzle Synth keeps Freeze and calls Feedback and Dry/wet Wavetable and Osc/grains; Seed lock starts on and Density at 0.7.

## Release

To tag `v0.0.1` on the commit that adds this record, once the work is committed. Not pushed.
