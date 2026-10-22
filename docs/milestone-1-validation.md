<!-- SPDX-License-Identifier: MIT -->
# Milestone 1 validation

**Done offline; the Pi check is to do.** Recorded 21 October 2026. Done: every control and CV input of Drizzle except the milestone 2 ones (Mono, Gain, Assign, the gate output), the four qualities at their hardware rates, Freeze, Seed and Seed lock, bypass, and their offline tests. To do: the Pi CPU and xrun check (below), then `v0.1.0`.

| Check | Result |
| --- | --- |
| x86-64 `make -j8 test`, Debian 13 under WSL | PASS; GCC 14.2; no warnings |
| `make test SAN=1` | PASS |
| `make test SAN=thread` (under `setarch -R`) | PASS |
| arm64 `make -j8 test BUILD_DIR=build/arm64 OPT="-O2 -mcpu=cortex-a76"` | PASS; Debian 12 root, GCC 12.2, QEMU user emulation; `match_beads` bit for bit there too |
| `lv2lint` 0.17.151 | No errors; author-email warning as in milestone 0 |
| `make test-hosts`, jalv and Ingen 1 and 4 threads, 1024- and 128-frame periods | PASS. At 128 frames JACK logged one xrun while jalv ran (`drizzle_jalv was not finished`); WSL runs JACK without realtime scheduling, so this says nothing either way. Watch for it on the Pi |
| **Pi 5: CPU and xruns** | **Not done** |

## Offline results

| Test | Result |
| --- | --- |
| `match_beads` | At each processor rate (48 kHz digital and clean tape, 32 kHz vintage digital, 24 kHz dirty tape) the plugin's output equals `beads::Processor` driven directly as `beads.cc` and `beads_test.cc` drive it, bit for bit, late by the reported latency: 5 settings of 6 s each, with Time and Pitch CVs moving (uses CV on), all four Mod knobs, the Freeze gate, the Seed button (unlocked) and the Seed gate clocking grains (locked, uses gate), and the delay mode (Size 1, Feedback 0.6) |
| `controls` | 25 cases: every knob, Mod knob, switch and CV moves only its own processor field, with `cv_reader.cc`'s laws (attenuverter dead zone and curve), CV scales (Time, Size, Shape 1.6, Pitch 60 semitones, Density 1 per 5 V), CV filters, the pots' 5% glide and Time's hysteresis; Seed while locked changes nothing; the milestone 2 ports change nothing yet; unconnected controls read their defaults |
| `slicing` | identical output for whole periods, random slices and a 37-frame period, at 48 and 44.1 kHz, in digital, vintage digital and dirty tape (the resampled paths included) |
| `qualities` | the four qualities sound and differ and report the same latency (95 frames at 48 kHz). Switching through all of them on a sine: the largest step is 0.0149, against 0.0128 for the sine itself (an unfaded switch jumps by up to 0.375); sound returns within 50 ms |
| `freeze_seed` | Freeze (and the Freeze gate) frozen on silence: later noise is never heard. Unlocked: silence with Seed off, grains while held, they stop on release. Density at noon: one grain per press, none while held. Locked with Seed uses gate: grains on gate pulses, silence without pulses |
| `bypass` | On off from the start: the output is the input exactly. Turned off while playing: the tails ring on |
| `nonfinite` | NaN, infinite and huge audio, CV and control values leave the output finite; it recovers |
| `allocation` | no allocation in `run()` on a new thread, across a quality change |
| `resampler` | 24, 32 and 48 kHz processors at 48, 44.1 and 96 kHz: passband within 0.0014 dB, aliases and images below -82 dB, stopband below -86 dB; latency at 48 kHz 95 frames (24 kHz path) and 76 (32 kHz path), exact; at 44.1 kHz rounded |
| `rt_timing` (`make test-timing`), x86-64 under WSL, 48 kHz, 128 frames, 10 simulated minutes of random controls, every quality, Density often at maximum | mean 29 us per period (1.1% load), 99.9% 135 us, worst 1570 us. The worst varies run to run and is not at the switches; WSL scheduling. Says nothing about the Pi |

## Found and fixed during the milestone

- **Dirty tape runs at 24 kHz on the hardware** (`ENABLE_HARDWARE_24K`), not 48: the spec and plan said only vintage digital changed rate. Dirty tape is resampled like vintage digital.
- **`Configure` clears the whole 768 KB buffer in one call.** Patched to take `clear = false`; the glue fades out, clears 1 KB per host frame, configures, fades in (plan section 4.4, `tools/patches/beads-configure-without-clear.patch`).
- The first fade-in started before the new path's output arrived (a step of 0.15 when the sound appeared). It now waits for the latency plus 10 ms, which also covers dirty tape's wow and flutter delay.
- **Seed needs to know whether its jack is patched**, as the CV inputs do: the trigger generator and delay controller take `NULL` for an unpatched Seed input, and free-run only then. Added the `Seed uses gate` switch.
- **Seed must start locked** to play grains (the hardware does): Seed lock moved from milestone 2 to 1 and defaults to on.
- **Drizzle Synth's draft port list was wrong**: Beads' synth uses Feedback (wavetable bank), Dry/wet (oscillator against grains) and Freeze. Corrected before any patch was saved.
- A synth `printf` under `-DTEST` would have printed from the audio thread (`tools/patches/beads-no-printf.patch`).
- Under WSL, the first block of a quality not yet played took about 450 us when the bundle sat on `/mnt/c` (code pages over 9p); from native storage it took 14 us. `make test-hosts` copies the bundle to `/tmp` for that reason.

## Pi check (to do)

On the Pi, at the commit to be tagged:

```sh
make OPT="-O2 -mcpu=cortex-a76" && make test OPT="-O2 -mcpu=cortex-a76"
make test-timing OPT="-O2 -mcpu=cortex-a76"
sudo make install LV2DIR=/opt/peepo/lib/lv2 OPT="-O2 -mcpu=cortex-a76"
```

Then a typical patch in Peepo (input -> amp -> Drizzle -> output, plus a Mist) for 30 minutes with guitar or a looped DI, Density at maximum, each quality in turn, Quality changed now and then while playing. Record JACK xruns (0 beyond the same patch without Drizzle), DSP load and RSS in Peepo's `docs/evidence/` (plan section 6.4).

## Listening (milestone 3)

`tools/render` (`make render`): the defaults (Density 0.7, Quality digital, Seed locked), the CV scales against a hardware Beads if one is at hand, and the bypass fade.
