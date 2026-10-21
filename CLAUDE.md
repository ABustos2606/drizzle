# Drizzle

Drizzle is an LV2 granular processor and delay with a built-in reverb, ported from the open-source code of Beads, a eurorack module by Emilie Gillet. It is a plugin written for **Peepo**, and Peepo is the host that matters. Read `docs/design.md` before changing behaviour or ports: it is the spec. `docs/implementation-plan.md` says how to build it.

## Peepo

Peepo is a guitar pedal built on a Raspberry Pi 5 (2 GB, Debian 12 arm64) running the **Ingen** modular host on JACK, with an LVGL touch UI. Users patch LV2 modules together, including CV modules (LFOs, envelopes, random sources). It has two encoders and three momentary stomps. It was called "Pedal PI" before; old names (`pedalpi`, `urn:pedalpi:ns#`) still turn up.

- Repository: `C:\Pedal PI` (https://github.com/ABustos2606/Peepo). It is readable from here (see `.claude/settings.json`).
- Docs worth reading there:
    - `docs/38-eurorack-ports-roadmap.md`: why Drizzle, Chamber, Slope and Reel exist, their order and the Peepo-side steps;
    - `docs/37-module-names.md`: why the original module names are not used;
    - `docs/20-clouds.md`: Mist, Poly's port of Clouds (Beads' predecessor): the closest existing plugin, its controls and what was learned;
    - `docs/22-plugin-catalogue.md` and `config/catalogue/overlay.json`: how plugins are named, grouped and described in the UI;
    - `docs/29-parameters-files-tempo.md`: patch tempo, BPM ports and times in beats;
    - `reference/upstream/lenticular_lv2/src/polyclouds.cpp`: how Poly wrapped Clouds (block splitting, CV scaling);
    - `reference/upstream/ingen/`: Ingen's source.
- `C:\LV2-own-plugins\discipline` and `C:\LV2-own-plugins\loo-pi` are sister projects: same host limits, same kind of build, tests and milestones. Loo-pi is also made from someone else's code. Copy their patterns (LV2 glue, `ttl_check`, in-process test host, timing test, renderer) rather than reinventing them. `chamber`, `slope` and `reel` are the other three Gillet ports, built the same way.
- Peepo builds every third-party plugin from its own repository at a pinned commit (`tools/ingen-spike/14-more-plugins.sh`, `reference/manifest.json`). Drizzle will be built the same way, so tag releases and keep the build plain (`make`, no network access, works natively on arm64).

## What is kept and what is not

- **Kept, unchanged:** Beads' `dsp/` and the `stmlib` files it needs, vendored from `pichenettes/eurorack` at commit `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026) into `third_party/eurorack/`, with their MIT notices.
- **Ported, not vendored:** the parts of `beads.cc`, `ui.cc`, `cv_reader.*` and `gain_control.*` that turn knobs, buttons and CV into the processor's parameters. Copy their rules exactly, naming the source function; don't reinvent them.
- **Not used:** drivers, bootloader, calibration, factory test, settings storage, LEDs and the hardware design.
- New code lives in `src/` (LV2 glue, parameter mapping, port enums), `tools/` and `drizzle.lv2/` (Turtle). Change vendored files only where the plan says, and keep those changes small and commented.

## Host limits that shape the code

- **xruns are the limit, not average CPU.** Ingen runs the whole patch as one JACK client. In `run()`: no allocation, no locks, no file or console I/O, no bulk work such as clearing a whole buffer at once. Allocate the delay buffers (768 KB) and the reverb buffer (64 KB) in `instantiate`.
- **`run()` arrives in slices.** Ingen splits a period at every control change, so `n_samples` varies. Beads processes in blocks of 12 frames at 48 kHz (its block rate is 4 kHz); the glue buffers its way to whole blocks and must not assume one `run()` per period.
- **Blocks run on several threads.** Two Drizzle instances may run at the same moment; they share nothing.
- **Controls arrive once per audio block.** Beads smooths its own parameters; anything the glue adds (gains, mix) is smoothed per sample.
- **Unpatched inputs read silence.** A plugin can't tell whether a port is connected. Beads changes behaviour when a CV input is unplugged (its amount knobs become random amounts) and when the audio input is unplugged (it becomes a synth). Drizzle does neither by guessing: each is an explicit control or a separate plugin (see the spec).
- **No LV2 state for buffers.** The captured audio is lost when a patch reloads, as with Mist. That is accepted for now.
- **Bypass.** Ingen's own block bypass never calls `run()`, so a frozen buffer or a tail stops dead. Decide in the spec whether Drizzle needs an `lv2:enabled` port and `"bypass": "port"` in Peepo's overlay.
- Everything a user sets (quality, modes, switches) is a control port, so patches save it.

## Rules

- Plugin URIs: `https://github.com/ABustos2606/drizzle#...`. Never use Peepo's namespace. Don't change a URI or port symbol once a patch has been saved with it.
- Never use "Mutable Instruments" or "Beads" in plugin names, port names, descriptions, URIs or new code identifiers. Vendored upstream code keeps its own names (`namespace beads`). Credit Emilie Gillet and Beads in the README and `doap:` metadata only.
- Port names and descriptions appear on Peepo's small screen: short, plain ASCII (its font draws other characters as boxes), and they say what the control does, never who made it.
- License: MIT, `Copyright (c) 2026 Alejandro Bustos`. Every new source file, header, Makefile and `.ttl` file starts with `SPDX-License-Identifier: MIT`; plugins declare `doap:license <https://spdx.org/licenses/MIT>`. Vendored files keep Emilie Gillet's copyright and MIT notice unchanged.
- Line endings are LF. Files go to Linux and the Pi.
- Use Peepo's name, never "Pedal PI", in new text.

## Testing

- Desktop: any LV2 host (Carla, Ardour, `jalv`), and `lv2lint` for the `.ttl` files.
- Beads' own `test/beads_test.cc` renders WAV files from the processor; compare Drizzle's renders against it at the same settings.
- Peepo's development engine runs under WSL (Debian) with Ingen in `/opt/peepo`; Peepo's `tools/ingen-spike/env.sh` starts it.
- On the Pi: measure xruns and DSP load with Drizzle in a typical patch before calling a milestone done.
