<!-- SPDX-License-Identifier: MIT -->
# Vendored upstream code

Beads' DSP code by Emilie Gillet, and the parts of her `stmlib` it needs. MIT licensed: the notice is at the top of every Beads file, and in `stmlib/LICENSE`. Copied by `tools/vendor.sh`; do not edit by hand.

| Source | Commit |
| --- | --- |
| https://github.com/pichenettes/eurorack | `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026) |
| https://github.com/pichenettes/stmlib (eurorack's `stmlib` submodule at that commit) | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` |

## Files

- `beads/dsp/`: all of it, including `fx/`: the processor (granular player, multitap delay, reverb, wow and flutter) and the synth.
- `beads/resources.cc`, `beads/resources.h`: lookup tables and wavetables.
- `stmlib/`: `LICENSE`, `stmlib.h`, `dsp/{cosine_oscillator,delay_line,dsp,filter,hysteresis_filter,hysteresis_quantizer,parameter_interpolator,sample_rate_converter,units}.h`, `dsp/units.cc`, `utils/{dsp,gate_flags,random}.h`, `utils/random.cc`: the files `beads/dsp/` includes (found with `g++ -MM`).

Not vendored: `beads.cc`, `ui.cc`, `cv_reader.*`, `gain_control.*` and `settings.*`, whose rules are ported into `src/` (each port names its source), and the drivers, bootloader, calibration, factory test, LEDs and hardware design.

Drizzle compiles these with `-DTEST`, as upstream's own `beads/test/makefile` does, which selects the portable C++ paths in `stmlib/dsp/dsp.h` instead of the Cortex-M assembly.

## Local changes

Four, applied by `tools/vendor.sh` from `tools/patches/`, each commented in place:

- `stmlib-random-thread-local.patch`: `stmlib::Random` keeps its state in one static variable, drawn from by the grain scheduler and the random modulations. On the hardware there is one processor; in Ingen two Drizzle instances can run at the same moment on different threads and would race on it. The state is made `thread_local`, and Drizzle saves and restores each instance's own state around every `Processor::Process` call, so every instance draws its own repeatable sequence. The Makefile builds with `-ftls-model=initial-exec`, so the state lives in the static TLS block set up when the library is loaded: otherwise glibc allocates it with `malloc` the first time each host thread touches it, inside `run()` (the `allocation` test checks it). Values drawn are unchanged: a single instance starting from upstream's seed (`0x21`) produces the same numbers as the hardware. Same change as Chamber's.
- `beads-configure-without-clear.patch`: `Processor::Configure` resets the delay lines of the new quality, which fills the whole 768 KB buffer at once. On the hardware the codec is stopped while it does; in a plugin it would be one long `run()`. `Configure` and `GranularProcessor::Reset` take a `clear` argument, `true` by default (upstream's behaviour). When Quality changes, Drizzle fades out, writes the same bytes the clear would (the new quality's empty item, and float zeros in the freeze tail) 1 KB per host frame, then calls `Configure(media, mono, false)`.
- `beads-gate-output.patch`: `Processor::Process` and `Synth::Render` take an optional last argument, `bool* gate_output` (`NULL` by default, upstream's behaviour), which receives the grain gate of each frame. The hardware sends that gate out only in its gate output mode, in place of the right audio output; Drizzle puts it on its Grain out CV port and keeps the audio in stereo. The gate itself is computed as upstream computes it; only the copy is added.
- `beads-no-printf.patch`: `dsp/wavetable_granular_player.h` prints a line each time a grain is stolen when built with `-DTEST`, which Drizzle needs for `stmlib/dsp/dsp.h`. Console output on the audio thread is not allowed; the print is removed. Nothing else in that block.
