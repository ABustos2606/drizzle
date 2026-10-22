<!-- SPDX-License-Identifier: MIT -->
# Milestone 2 validation

**Done offline; the Pi check is to do.** Recorded 21 October 2026. Done: Drizzle Synth, Mono, auto and manual input gain, the Assign CV, the grain output and its Gate out choice, and their offline tests. To do: the Pi CPU and xrun check, together with milestone 1's (below), then `v0.2.0`.

| Check | Result |
| --- | --- |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS; GCC 14.2; no warnings |
| `make test SAN=1` | PASS |
| `make test SAN=thread` (under `setarch -R`) | PASS |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"` | PASS; Debian 12 root, GCC 12.2, QEMU user emulation; `match_beads` and `match_synth` bit for bit there too |
| `lv2lint` 0.17.151 | No errors; author-email warning as in milestones 0 and 1 |
| `make test-hosts`, jalv and Ingen 1 and 4 threads | PASS: Drizzle Synth plays in jalv; two Drizzles and a Drizzle Synth in Ingen; Mono, Auto gain, Input gain, CV assign to and Gate out save and restore with the patch |
| **Pi 5: CPU and xruns** | **Not done** |

## Offline results

| Test | Result |
| --- | --- |
| `match_beads` | The nine settings below, each at its processor rate, equal `beads::Processor` driven directly as `beads.cc` drives it, bit for bit, audio **and** grain gate, late by the reported latency. Milestone 1's five settings now run with Auto gain on (the default), so the gain stage is in the path; four new ones: digital with Mono and Assign to Feedback; clean tape with 12 dB manual gain (dynamic soft clipping) and Assign to Dry/wet; dirty tape at 24 kHz with Mono, 24 dB manual gain (soft clipping) and Assign to All; vintage digital at 32 kHz in delay mode with Assign to Reverb |
| `match_synth` | Drizzle Synth equals `beads::Synth` driven directly at 48 kHz, bit for bit, audio and grain gate: 16, 4 and 7 bit; free-running, gate-clocked and Seed-button grains; Freeze; Assign to Wavetable and to All. Latency 12 frames (one block) |
| `controls` | 30 cases (25 in milestone 1): Assign to each destination with `beads.cc`'s formula (the CV's 0.1 one-pole, the 0.002 offset, the clamp), All with 255, 0 and 128 of 256, Gate out; Mono and gain leave the processor's parameters alone (the glue reads them) |
| `gain` | Auto gain is 1.8 while it ignores the first second, then brings a 0.2 sine's peak to 0.9; a quality change keeps what it learnt; a very quiet input stops at 40 times. Manual gain reaches its setting after its glide and clips at full scale in the digital qualities, soft-clips in dirty tape, and stays clean in clean tape while below the learnt gain. Mono copies In L to the right. Turning Auto back on learns again |
| `gain` (plugin) | On the dry path: Input gain 0 dB gives the input exactly, 12 dB gives 3.98 times, 32 dB clips at full scale; Auto gain brings a 0.1 sine up 9.03 times (to 0.9) |
| `mono` | With Mono on the output is the same whether In R carries noise or silence; off, In R is heard. Switching Mono while running on a sine: largest step 0.0128, the sine's own; sound back within 50 ms |
| `grain_out` | Trigger mode: pulses of exactly 10 ms (480 frames at 48 kHz, 441 at 44.1), one per rising edge of the gate; Gate mode differs and rises wherever a trigger starts; Grain out goes on while bypassed |
| `synth` | Plays with its defaults and no input; Wavetable, Osc/grains, Quality, Pitch and Reverb each change the sound; Seed unlocked and off with Osc/grains at grains only is silent; identical output for whole, random and 37-frame periods at 48 and 44.1 kHz; activate restarts it |
| `slicing`, `instances`, `qualities`, `freeze_seed`, `bypass` | as in milestone 1, now with the Assign CV moving (to All) and Mono on in one slicing case |
| `nonfinite` | NaN in Input gain, CV assign to, Mono, Auto gain and Gate out, and NaN or infinity on the Assign CV, leave the output finite; it recovers |
| `allocation` | no allocation in `run()` on a new thread, across quality and Mono changes and Auto gain off; Drizzle Synth's `run()` on a new thread too |
| `rt_timing` (`make test-timing`), x86-64 under WSL, 48 kHz, 128 frames, 10 simulated minutes of random controls (now also Assign, Auto gain, Input gain and Mono) | mean 30 us per period (1.1% load), 99.9% 114 us, worst 1494 us: as in milestone 1. WSL scheduling; says nothing about the Pi |

## Found and decided during the milestone

- **The grain gate only reaches the outside on the hardware's right audio output** (gate output mode). Drizzle has a CV output for it, so the vendored `Processor::Process` and `Synth::Render` gained an optional argument that hands the gate out (`tools/patches/beads-gate-output.patch`); the audio stays stereo and `output_gate` is always off.
- **Gate out's two choices** were not the hardware's (which has only the gate): Trigger is a 10 ms pulse on each rising edge, Gate the gate itself. 10 ms so that a module reading its CV once per period sees it (**tune**).
- **Mono** is the hardware's "one input unplugged" (mono buffer, In L copied across in the gain stage), and switches through the same fade and clear as Quality; the gain learns again after it, as on the hardware.
- **Assign "All"** uses `settings.cc`'s default amounts; the 0.002 offset of `beads.cc` is kept.
- **The gain stage runs per processor block at the processor's rate**, as on the hardware, so its times (1 s ignored, 5 s learning) hold in every quality.
- **Manual gain starts at its setting** rather than gliding up from 0 as the hardware's does at power-on, as the pots already do.
- **Drizzle Synth runs at 48 kHz in every quality**, as `beads.cc` does in synth mode; at other host rates it is resampled like Drizzle.

## Pi check (to do)

As in [milestone 1](milestone-1-validation.md), at the commit to be tagged `v0.2.0`, with also a Drizzle Synth in the patch, Mono on for one run and Auto gain on guitar. Record JACK xruns, DSP load and RSS in Peepo's `docs/evidence/`.
