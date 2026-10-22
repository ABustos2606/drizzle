# Drizzle implementation plan

**Written:** 20 October 2026. **Follows:** [design.md](design.md), the spec. Where this plan and the spec disagree, the spec wins and this plan is fixed. **Checked against:** Beads' `beads.cc`, `ui.cc`, `cv_reader.h`, `settings.h`, `drivers/cv_adc.h`, `drivers/pots_adc.h`, `dsp/processor.h` and `dsp/granular_player.h` at upstream commit `f4cfa7697095a28d292bff758beb51deb49b5a86`; Poly's `polyclouds.cpp` in Peepo's `reference/upstream/lenticular_lv2/`; Discipline's implementation plan, whose Ingen findings apply unchanged.

**Updated 21 October 2026** with what milestones 0 and 1 found: dirty tape runs at 24 kHz (section 4.2), `Configure` clears the whole buffer (4.4, 7), four ports were added (3), and Seed lock moved to milestone 1 (8). Records: [milestone 0](milestone-0-validation.md), [milestone 1](milestone-1-validation.md).

This plan says how to build Drizzle: code layout, ports, the glue around Beads' processor, real-time rules, tests and milestones. Values that only listening can settle are given a starting value and marked **tune**.

## 1. What Ingen's source changes

From Discipline's plan (section 1), unchanged:

1. **Engine bypass does not run the plugin.** A bypassed Drizzle stops dead. If Drizzle gets an `lv2:enabled` port, Peepo must use `"bypass": "port"`.
2. **`run()` is called in slices.** Ingen splits a period at every control change. The glue keeps a 12-frame staging buffer and calls the processor only on whole blocks (section 4.2).
3. **Blocks run on several threads.** Instances share nothing.
4. **Block length** comes from `bufsz:nominalBlockLength` / `maxBlockLength` through `options:options`. Drizzle needs it only to size scratch buffers.

## 2. Technology and build

- **Plain LV2 C API in C++17**, no DPF. Both plugins in **one shared object**, `drizzle.so`, in `drizzle.lv2/` beside `manifest.ttl`, `drizzle.ttl` and `drizzle_synth.ttl`.
- **Flags as built:** `-DTEST` (stmlib's portable C++, as upstream's desktop test), `-fno-fast-math` after `OPT` (Beads' code is not checked under fast-math, and the non-finite guards need IEEE arithmetic), and `-ftls-model=initial-exec` for the `thread_local` Random state (section 7).
- **License:** MIT. **Dependencies:** `lv2-dev` headers only.
- **Makefile**, no configure step, no network: `make`, `make install` (`DESTDIR`, `PREFIX`, `LV2DIR`), `make test`, `make lint`, `OPT` and `SAN=1`, as in Discipline. Arch flags are never hard-coded; the Pi build passes `OPT="-O2 -mcpu=cortex-a76"`.
- **Flags:** `-std=c++17 -fPIC -fvisibility=hidden -Wall -Wextra`. Beads' code was written for `-ffast-math`-free GCC on ARM; check whether its NaN-free assumptions hold before allowing fast-math. The vendored code's warnings are silenced for `third_party/` only. Only `lv2_descriptor` is exported.
- **Releases** are git tags (`v0.1.0` ...). Peepo pins a commit.

### Repository layout

```
Makefile
LICENSE                      MIT
README.md                    what it is, build, install, credit to Beads and Emilie Gillet
drizzle.lv2/
    manifest.ttl
    drizzle.ttl              ports of Drizzle
    drizzle_synth.ttl        ports of Drizzle Synth
third_party/eurorack/        vendored at f4cfa769, unchanged unless section 7 says
    README.md                upstream commit, file list, local changes
    beads/dsp/...
    beads/resources.cc/.h    lookup tables and wavetables
    stmlib/...               only the files beads/dsp includes
src/
    lv2_entry.cpp            lv2_descriptor(): index 0 Drizzle, 1 Synth
    ports.hpp                port index enums, the single source in C++
    drizzle.hpp/.cpp         instantiate, activate, run, cleanup; Quality switching, bypass
    drizzle_synth.cpp        Drizzle Synth (silent until milestone 2)
    controls.hpp/.cpp        ports -> beads Parameters, MixParameters (from cv_reader.cc, beads.cc Process)
    block_buffer.hpp         1/4000 s staging between host slices and the processor
    rate_bridge.hpp          host rate -> processor rate -> host rate around the blocks
    resampler.hpp            Chamber's polyphase resampler, copied
    gain.hpp/.cpp            input gain and soft clipping (from gain_control.cc), milestone 2
    exports.map              only lv2_descriptor is exported
tools/
    vendor.sh                copies the listed upstream files at the pinned commit (run by hand, not by make)
    patches/                 the local changes to vendored files (section 7)
    render.cpp               WAV in, settings, WAV out, for listening
tests/
    host.hpp/.cpp            in-process LV2 host with random slicing (from Discipline and Chamber)
    test_*.cpp               scaffold, match, controls, resampler
    ttl_check.py             .ttl indices and symbols against ports.hpp
    host_smoke.py            jalv and Ingen with a private dummy JACK server (make test-hosts)
docs/
    design.md, implementation-plan.md
```

## 3. Ports

Port **symbols and URIs are permanent** once a patch has been saved with them. All ports are declared in milestone 0; until a port does something it is inert. Ports added later are only appended, with `lv2:minorVersion` raised. Names are short plain ASCII, at most 16 characters, and say what the control does.

### Drizzle: `https://github.com/ABustos2606/drizzle#drizzle`

| # | Symbol | Name | Kind | Range, default |
| --- | --- | --- | --- | --- |
| 0, 1 | `in_l`, `in_r` | In L, In R | audio in | |
| 2, 3 | `out_l`, `out_r` | Out L, Out R | audio out | |
| 4 | `time` | Time | control | 0-1, 0.5 |
| 5 | `size` | Size | control | 0-1, 0.5 |
| 6 | `shape` | Shape | control | 0-1, 0.5 |
| 7 | `pitch` | Pitch | control, integer | -24 to 24 st, 0 |
| 8 | `density` | Density | control | 0-1, 0.7 **tune** (0.5 is silent) |
| 9-12 | `time_mod` ... `pitch_mod` | Time mod ... | control | -1 to 1, 0 |
| 13-16 | `time_uses_cv` ... `pitch_uses_cv` | Time uses CV ... | toggle | 0 |
| 17 | `feedback` | Feedback | control | 0-1, 0 |
| 18 | `dry_wet` | Dry/wet | control | 0-1, 0.5 |
| 19 | `reverb` | Reverb | control | 0-1, 0 |
| 20 | `freeze` | Freeze | toggle | 0 |
| 21 | `seed` | Seed | momentary | 0 |
| 22 | `seed_lock` | Seed lock | toggle | 1 (the hardware starts locked) |
| 23 | `quality` | Quality | enumeration | Dirty tape, Clean tape, Vintage digital, Digital; 3 (the hardware's default) **tune** |
| 24 | `mono` | Mono | toggle | 0 |
| 25 | `gain` | Input gain | control | 0-32 dB, 0: the hardware's manual gain, 1 to 40 times (used when Auto gain is off) |
| 26 | `assign_to` | CV assign to | enumeration | Feedback, Dry/wet, Reverb, All |
| 27 | `gate_mode` | Gate out | enumeration | Trigger, Gate |
| 28 | `output` | Output | control | dB, -24 to 12, 0 |
| 29-36 | `time_cv`, `size_cv`, `shape_cv`, `pitch_cv`, `density_cv`, `assign_cv`, `freeze_gate`, `seed_gate` | | CV in | |
| 37 | `grain_out` | Grain out | CV out | |
| 38 | `seed_uses_gate` | Seed uses gate | toggle | 0: "a cable is in the Seed jack" |
| 39 | `auto_gain` | Auto gain | toggle | 1 (the hardware's default) |
| 40 | `enabled` | On | toggle, `lv2:enabled` | 1 |
| 41 | `latency` | Latency | control out, `lv2:reportsLatency` | |

Ports 38 to 41 were added in milestone 0 (21 October 2026), after the table above was drafted, and appended so its indices stand.

Drizzle Synth (`#synth`, a separate plugin, decided 20 October 2026) has its own indices (`SynthPort` in `ports.hpp`): the same symbols minus `in_l`, `in_r`, `mono`, `gain`, `auto_gain` and `enabled`; `feedback` and `dry_wet` become `wavetable` (Wavetable) and `osc_grains` (Osc/grains), which is what `dsp/synth.cc` does with those knobs, and it keeps `freeze` and `freeze_gate`, which hold the synth's voice. 36 ports. The draft list above dropped Freeze, Feedback and Dry/wet; `synth.cc` uses all three.

## 4. The glue

### 4.1 Instantiate

Allocate the two delay buffers (512 KB and 256 KB, as `beads.cc`) and the reverb buffer (32,768 `uint16_t`), touch their pages, call `Processor::Init(buffers, reverb_buffer)` and `Configure(media, mono)`. The `Processor` (26 KB) lives in zeroed memory, as the hardware's global does, and `activate` rebuilds it. Any rate from 8 to 768 kHz is accepted and resampled (spec, Sample rate).

### 4.2 Blocks

In vintage digital the processor runs at 32 kHz, as on the hardware (decided 20 October 2026): the host's 48 kHz input is resampled 3:2 down, processed in 8-frame blocks (4 kHz block rate at 32 kHz), and resampled back up. Dirty tape runs at 24 kHz the same way, in 6-frame blocks (found in milestone 1: `ENABLE_HARDWARE_24K`). The resampler is the same polyphase filter as Chamber's (copied, not shared). Its latency is reported through an `lv2:reportsLatency` port; the other qualities are delayed to match, so switching quality doesn't shift the timing. As built, at 48 kHz: 95 frames (2 ms) in every quality, set by the 24 kHz path; 76 frames for the 32 kHz path, 12 (one block) for the 48 kHz path, each padded to 95. One `RateBridge` per processor rate is built in `instantiate`.

Beads runs at a 4 kHz block rate: 12 frames at 48 kHz. `block_buffer` collects input from host slices of any length, calls `Processor::Process` on each full block and plays out the previous block's output, so the processor's own latency is one block (0.25 ms). The alternative (calling `Process` with the slice length) was dropped: `Process` takes its controls once per call and its block rate sets its timing, and the resampled qualities need staging anyway. The control ports and CVs are read at the host frame where a block completes.

### 4.3 Controls

`controls.cpp` copies `cv_reader.cc` (each pot's law, its 0.05 one-pole per block and Time's 0.001 hysteresis; each CV's one-pole and range) and `beads.cc`'s `Process` (lines about 175 to 245): each `RandomizedParameter` gets `base` from its knob, `attenuverter` from its mod knob, `cv` from its CV input, and `has_cv` from its "uses CV" switch. That keeps `randomized_parameter.h`'s rules exactly (no cable: random walk or per-grain random; cable: attenuverter or CV plus random) and replaces only the hardware's normalization detector, which a plugin can't have. Density's CV is used as is; the hardware zeroes it when unplugged, and an unplugged LV2 input already reads 0.

CV scale: 1.0 = 5 V, as Poly's ports. From `settings.cc`'s default calibration the hardware's ADC spans +-5 V, so Time, Size and Shape CVs are `cv * 1.6`, Density `cv`, Pitch `cv * 60` semitones. The Pitch knob is whole semitones (the hardware's `LAW_DETUNE` snaps its pot to intervals; here the control is already semitones). The Mod knobs read -1 to 1 and go through `LAW_ATTENUVERTER`, dead zone included. Pots start where the patch has them (the hardware's start from 0). Assign (milestone 2) adds nothing yet.

### 4.4 Quality

`Configure` is called when `quality` (and from milestone 2 `mono`) changes. Checked in milestone 1: it **does** bulk work. It resets the new quality's delay lines, which fill the whole shared 768 KB buffer with that quality's empty item; on the hardware the codec is stopped meanwhile and the output muted for 0.25 s. Drizzle fades out over 5 ms, writes the same bytes 1 KB per host frame (16 ms at 48 kHz; mu-law's zero in dirty tape, 0 otherwise, float zeros in the freeze tail), calls `Configure(media, mono, false)` (section 7), waits for the new path's latency plus 10 ms (dirty tape's wow and flutter delay fills after the latency), and fades in over 5 ms. About 40 ms of silence; the steps at the fades are no larger than the signal's own. The rest of `Configure` takes about 1 us. A Quality set before the first `run()` is applied without the fade out.

### 4.5 Gain

`gain_control.cc` applies automatic gain and soft clipping that depend on the quality. It is outside `dsp/`, so it is ported into `gain.cpp`.

### 4.6 Seed and Freeze

As built: `seed_lock` gives `SEED_BUTTON_STATE_LOCKED`, else `seed` gives `ON` or `OFF` (the trigger generator merges the button with the gate itself). The Seed gate becomes `stmlib::GateFlags` per processor frame, 10 blocks (2.5 ms) late through a 256-entry delay, as `beads.cc` does, and is passed to `Process` only when `seed_uses_gate` is on (the hardware passes `NULL` when the jack is unpatched, which changes both the trigger generator and the delay controller). `freeze` OR `freeze_gate` gives the `freeze` array. Gates are high from 0.4.

## 5. Real-time rules, checked

- No allocation, lock or I/O in `run()` (checked by the in-process host with an allocation hook).
- `Configure` and any buffer trim never do bulk work in one call.
- Output finite: NaN or infinity at the input is zeroed per channel and the input is clipped at full scale, as the codec does; the output is clamped to [-1, 0.999999] as `beads.cc` does, and a non-finite output sample is zeroed.
- Bypass (`enabled` off) crossfades over 10 ms; the processor keeps running on silence so its tails ring out.

## 6. Tests

### 6.1 Offline (`make test`)

| Test | Check |
| --- | --- |
| `ttl_check` | every port matches `ports.hpp`: index, symbol, kind; names ASCII <= 16 characters |
| `slicing` | the same input and settings give the same output with random host slice lengths |
| `match_beads` | Drizzle's output matches `beads_test`-style rendering of the vendored processor, bit for bit, at fixed settings and seed |
| `quality` | switching quality while running: no click above a threshold, no NaN |
| `controls` | each knob, amount, random and CV changes the matching `Parameters` field and nothing else |
| `freeze_seed` | freeze stops recording; seed triggers a grain on a rising edge only |
| `nonfinite` | NaN input: output finite, recovers |
| `rt_timing` | 10 simulated minutes of random controls: worst and 99.9th-percentile `run()` time (`make test-timing`) |
| `bypass`, `allocation`, `instances`, `resampler` | as Chamber's: bypass exact and tails ring; no `malloc` in `run()` on a new thread; instances keep their own Random sequence alone, interleaved and threaded; each resampler's level, aliasing and latency |

### 6.2 Desktop hosts

`lv2lint`, `jalv`, Carla. `tools/render` for listening comparisons.

### 6.3 Ingen under WSL

With Peepo's `tools/ingen-spike/env.sh`: load both plugins, play a test signal through Drizzle, save and reload a patch (controls come back, the buffer doesn't), one and four threads.

### 6.4 On the Pi

Typical patch (input -> amp -> Drizzle -> output, plus a Mist) for 30 minutes with guitar or a looped DI, Density at maximum, every quality. Record JACK xruns (0 beyond the same patch without Drizzle), DSP load and RSS into Peepo's `docs/evidence/`.

## 7. Changes to vendored files

Three, each a patch in `tools/patches/`, listed in `third_party/eurorack/README.md` and commented in place:

1. `stmlib::Random`'s state is `thread_local` (as Chamber); the glue keeps each instance's state.
2. `Processor::Configure` and `GranularProcessor::Reset` take `clear = true`; Drizzle passes `false` after clearing the buffer itself (section 4.4).
3. The `-DTEST` `printf` in `dsp/wavetable_granular_player.h` (Drizzle Synth's grain player) is removed.

## 8. Milestones

Each has an end check. "Done" means the offline tests pass on x86-64 and arm64, plus the stated host checks.

- **Milestone 0, scaffolding.** `tools/vendor.sh`, vendored files, Makefile, both descriptors, both `.ttl` files with the full port list, `ports.hpp`, `ttl_check`. Drizzle runs the processor at default settings. **Done when** `make`, `make install DESTDIR=...` and `lv2lint` are clean and both plugins load in jalv and in Ingen under WSL. Tag `v0.0.1`. **Done 21 October 2026.**
- **Milestone 1, the processor.** Every control and CV input of Drizzle, the qualities, Freeze, Seed and Seed lock, `match_beads`, `slicing`, `rt_timing`. Pi CPU and xrun check. Tag `v0.1.0`; Peepo adds its overlay entry and pinned build. **Done offline 21 October 2026; the Pi check is to do.**
- **Milestone 2, the rest.** Drizzle Synth, Mono, Gain, the gate output, the Assign input. Tag `v0.2.0`.
- **Milestone 3, listening.** Guitar on the Pi; default values tuned (quality, density, gain). `v0.2.x` tags of tuning.

## 9. Peepo-side work

In Peepo's repository, not here:

1. Build step in `tools/ingen-spike/14-more-plugins.sh`: `third drizzle https://github.com/ABustos2606/drizzle.git <commit> "make" make -j"$JOBS" OPT=... LV2DIR="$LV2DIR" install`.
2. URIs in `config/catalogue/inventory.txt` and `tools/catalogue-inventory.py`.
3. Overlay entry: names "Drizzle" and "Drizzle Synth", categories Texture and Sound source, descriptions that say what it does, `"output": "output"`, `"bypass": "port"` for Drizzle (its `enabled` port), groups (**Grain**: Time, Size, Shape, Pitch; **Density and mix**: Density, Feedback, Dry/wet, Reverb; **Modulation**: the four mod knobs; **CV use**: the four switches; **Play**: Freeze, Seed, Seed lock, Seed uses gate, Quality; **More**).
4. `docs/37-module-names.md` and `docs/33-module-list.md` regenerated.

## 10. Risks

| Item | Why it matters |
| --- | --- |
| CPU at high Density | Grain count and the reverb on one Pi core; measure in milestone 1 |
| Resampling in vintage digital | Adds CPU and latency in that quality; check aliasing on guitar |
| `Configure` cost | Found: it clears 768 KB in one call. Spread over 16 ms (section 4.4) |
| First run of a quality's code | Under WSL with the bundle on `/mnt/c` the first block of a quality not yet played took about 450 us (code pages faulted in over 9p); from native storage, 14 us. Watch on the Pi |
