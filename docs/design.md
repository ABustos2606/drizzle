# Drizzle design

Drizzle is a granular processor and delay with a built-in reverb, for Peepo, a pedal built on a Raspberry Pi 5 running the Ingen modular host. It is an LV2 plugin made from the open-source code of Beads, a eurorack module by Emilie Gillet and the successor of Clouds. It must also work as a normal LV2 plugin in Carla or Ardour.

**Written:** 20 October 2026, from Peepo's `docs/38-eurorack-ports-roadmap.md` and a reading of Beads' source at `pichenettes/eurorack` commit `f4cfa7697095a28d292bff758beb51deb49b5a86` (12 October 2026, the first public commit of Beads' code). **Status, 21 October 2026:** milestone 0 done; milestone 1 done offline, its Pi check still to do. Decisions made while building them are in **Decisions** below, dated 21 October.

## Why Beads

- Peepo already has Mist, Poly's port of Clouds with the Parasites firmware. Beads is Clouds rebuilt: one granular engine with no separate modes, better grain scheduling, a delay mode, tape and digital quality settings, and a reverb.
- Its source was published under the MIT license in October 2026, so a port can be shipped with Peepo.
- No LV2 port exists. Poly's `lenticular_lv2` predates it.
- Its `dsp/` code has no hardware-specific instructions (no CMSIS, no inline assembly) and already runs on a desktop in its own `test/beads_test.cc`.

## Where it lives

Drizzle has its own repository (https://github.com/ABustos2606/drizzle), outside Peepo:

- Peepo builds every plugin from another repository at a pinned commit. Drizzle is built the same way.
- It is a normal LV2 bundle, so it can be tested on a desktop without Ingen or the Pi.
- Its build is a plain Makefile.
- Its license is **MIT**, the same as Beads' code. Peepo (GPL-3.0-or-later) can build and ship it.
- Tagged releases let Peepo update to a new version on purpose.

What stays in Peepo:

- its entries in `config/catalogue/overlay.json` and `config/catalogue/inventory.txt`;
- the build step that fetches and builds it at a pinned commit;
- patches that use it, xrun measurements and test notes.

The plugin URIs are `https://github.com/ABustos2606/drizzle#...`, not Peepo's namespace. Fix them before the first saved patch, because changing them later breaks patches.

## Name

"Drizzle", in the same weather family as Mist (Peepo's Clouds). Mutable Instruments' guidelines ask derivative works not to use the trademark and not to keep the module's name (Peepo `docs/37-module-names.md`). "Beads" appears only in credits.

## What Beads does

From the source (`beads.cc`, `ui.cc`, `dsp/processor.h`):

- **One granular engine.** Grains are read from a recording buffer that always records the input unless frozen. Time (where in the buffer), Size (grain length), Shape (grain envelope) and Pitch set each grain; Density sets how many grains are triggered, with a free-running, clocked or gated behaviour depending on the Seed input and button. With Size above 0.99 the engine crossfades into a multitap delay (`delay_controller.cc`, `multitap_delay.h`): Time and Shape then set the taps, and Density and Seed clock them.
- **Quality ("media").** Four settings: dirty tape, clean tape, vintage digital and digital. They change the buffer's bit depth and length and the soft clipping on the input. **On the hardware the codec runs at the quality's rate** (`Processor::sample_rate()`, with `ENABLE_HARDWARE_24K` defined): dirty tape at 24 kHz, vintage digital at 32 kHz, clean tape and digital at 48 kHz; clean tape then processes at half rate inside (`ratio()`). The processor's block is always 1/4000 s: 6, 8 or 12 frames.
- **Attenurandomizers.** Time, Size, Shape and Pitch each have one bipolar amount knob, and what it does depends on whether a CV cable is in (`dsp/randomized_parameter.h`): with no cable, right of noon is a smooth random walk and left of noon is a new random value per grain; with a cable, right of noon scales the CV and left of noon adds random modulation to it.
- **Mix.** Feedback, Dry/wet and Reverb, plus an assignable CV input routed to one of them or all three.
- **Freeze** (button and gate input) stops recording. **Seed** (button and gate input) triggers grains. The button starts **locked** at power-on (`Ui::Init`): locked with nothing in the Seed jack, grains run free at the Density rate; locked with a cable in, the gate clocks them (Density then divides or thins the clock); unlocked, grains play only while the button or gate is high, and at Density noon each rising edge plays one grain. Pressing Seed unlocks it; Seed + Freeze locks it again. The Seed gate is read 2.5 ms late (`event_delay` in `beads.cc`).
- **Synth mode.** With no audio input for a while, Beads becomes a wavetable synthesizer (`dsp/synth.cc`), played by the same controls.
- **Mono.** With one audio input unplugged the hardware copies the other across and records a mono buffer, twice as long (`normalization_detector.mono()`, `Configure(media, mono)`). A separate **monophonic mode** (Media + Seed, one grain at a time) exists only with `ENABLE_ADVANCED_FEATURES`, which Beads' makefile does not define. **Gate output mode** (Assign + Seed) is a hidden setting.
- **Gain.** Automatic input gain, or a manual gain setting.

## Plugins

Two plugins, one bundle, one binary:

| Plugin | URI | What it is |
| --- | --- | --- |
| Drizzle | `https://github.com/ABustos2606/drizzle#drizzle` | The granular processor and delay. Stereo in, stereo out |
| Drizzle Synth | `https://github.com/ABustos2606/drizzle#synth` | Beads' synth mode as a sound source. No audio input. Loads and is silent until milestone 2 |

A plugin can't tell whether its input is cabled, so Beads' automatic switch to synth mode becomes a second plugin rather than a guess (decided 20 October 2026).

## Controls

Symbols were fixed in milestone 0 (`src/ports.hpp`, `drizzle.lv2/*.ttl`) and never change. Defaults marked **tune** are settled by listening in milestone 3.

| Control | Kind | Notes |
| --- | --- | --- |
| Time | 0-1 | Position in the buffer |
| Size | 0-1 | Grain size; delay at the top |
| Shape | 0-1 | Grain envelope |
| Pitch | semitones, -24 to +24 | Whole semitones as in Mist; fine pitch from CV |
| Density | 0-1, default 0.7 (**tune**) | Grain rate; noon (0.48 to 0.52) is the hardware's zero point, where grains play only on Seed taps. A default of 0.5 would be silent |
| Time mod, Size mod, Shape mod, Pitch mod | -1 to 1, 0 | The hardware's attenurandomizer, with the same meaning on each side of noon |
| Time uses CV, Size uses CV, Shape uses CV, Pitch uses CV | switch, off | Stands in for "a cable is in" (`has_cv`), which a plugin can't detect. Off: the mod knob randomises. On: it scales the CV input |
| Feedback, Dry/wet, Reverb | 0-1 | |
| Freeze | switch | Also a gate input |
| Seed | momentary | Also a gate input. Unlocked: grains while held, or one per press at Density noon. Locked: no effect (pressing the hardware button unlocks it; here the lock is its own switch) |
| Seed lock | switch, **on** | The hardware's Seed + Freeze combo. On by default because the hardware starts locked: free-running grains |
| Seed uses gate | switch, off | Stands in for "a cable is in the Seed jack". Locked and on: the Seed gate clocks the grains. Locked and off: they run free |
| Quality | Dirty tape, Clean tape, Vintage digital, Digital | `Processor::Configure` switches it while running |
| Mono | switch | The hardware's monophonic mode |
| Auto gain, Input gain | switch, on; 0 to 32 dB | The hardware's gain setting (0 = auto, else 1 to 40 times). Milestone 2; until then the input is clipped at full scale with no gain |
| Output | dB | Peepo's output level control (`"output"` in the overlay) |
| On | switch, on | `lv2:enabled`: off passes the input through while the processor's tails ring out (see Bypass) |

CV inputs: Time, Size, Shape, Pitch (V/oct, Poly's scale: 1.0 = 5 V, so 0.2 per octave), Density, Assign (with a destination control: Feedback, Dry/wet, Reverb or all), Freeze gate, Seed gate. They span the hardware's +-5 V (`settings.cc` calibration): Time, Size and Shape reach +-1.6 at +-5 V, Density +-1, Pitch +-60 semitones. Gates are high from 0.4. CV output: the grain trigger or gate (the hardware's gate output mode becomes a choice control). Drizzle also reports its latency on a `Latency` output.

Drizzle Synth has no audio input, Mono, gain or On. In synth mode the hardware's Feedback knob picks the wavetable bank, Dry/wet balances the oscillator against the grains, and Freeze still holds the voice (`dsp/synth.cc`), so Drizzle Synth names them **Wavetable** (`wavetable`) and **Osc/grains** (`osc_grains`) and keeps **Freeze** (corrected 21 October 2026; the draft dropped all three).

## Sample rate

Peepo runs at 48 kHz. Beads' digital and clean tape qualities run at 48 kHz on the hardware, so they run unchanged. Vintage digital runs at 32 kHz on the hardware, and dirty tape at 24 kHz (found 21 October 2026: `ENABLE_HARDWARE_24K` is defined in `processor.h`, and the dirty tape processor, trigger generator, limiters and reverb are all set up for 24 kHz, so fed at 48 kHz it would play an octave up and twice as fast). In those qualities Drizzle resamples 48 -> 32 -> 48 kHz or 48 -> 24 -> 48 kHz around the processor, so it sounds like the hardware (decided with the user on 20 October 2026 for vintage digital; the same reasoning for dirty tape). Changing Quality fades out, clears the buffer and fades back in (implementation plan, section 4.4). Checked against the processor driven directly at 32 and 24 kHz (`match_beads`).

Other host rates (decided 21 October 2026): Drizzle accepts any rate from 8 to 768 kHz and resamples to the quality's rate with the same resampler. At 44.1 or 96 kHz every quality is resampled.

## Memory

About 0.8 MB per instance (768 KB delay buffers, 64 KB reverb, 26 KB of processor state), allocated and touched in `instantiate`. The buffer is not saved with the patch. Saving it as LV2 state is possible later (about 1 MB per instance).

## Bypass

Beads has no bypass. If Peepo's engine bypass is used, a frozen buffer stops and tails cut. **Decided 21 October 2026:** Drizzle has an `lv2:enabled` port, **On** (`enabled`), as Discipline does, and Peepo's overlay should use `"bypass": "port"`. Off crossfades over 10 ms to the input passed straight through and stops feeding the processor, which keeps playing what it has: grains of the buffer (a frozen buffer goes on playing) and the reverb tail. Drizzle Synth has no On port: a sound source has nothing to pass through.

## Decisions

Settled with the user on 20 October 2026:

- **Drizzle Synth is a separate plugin** in the same bundle, not a switch on Drizzle.
- **Vintage digital runs at 32 kHz**, with resampling, as on the hardware.

Made while building milestones 0 and 1, 21 October 2026 (the user asked for both milestones to be built without stopping for questions; each can be revisited until a patch is saved):

- **Dirty tape runs at 24 kHz**, resampled like vintage digital: that is the hardware's rate for it.
- **Every host rate is accepted** and resampled.
- **Bypass:** an `lv2:enabled` port; tails ring out.
- **Seed uses gate** is a switch, like the four "uses CV" switches: the trigger generator and the delay controller behave differently when the Seed jack is patched, which a plugin can't see.
- **Seed lock** starts on and **Density** starts at 0.7, so a newly added Drizzle plays grains (both **tune**).
- **Gain** is two ports, **Auto gain** and **Input gain**, instead of one control with an Auto position.
- **Drizzle Synth's ports** keep Freeze and rename Feedback and Dry/wet to Wavetable and Osc/grains.
- **Seed lock** moves from milestone 2 to milestone 1: without it Seed can't run free, so milestone 1's Seed would be incomplete.

## Open questions

| Question | Why it matters | When to settle |
| --- | --- | --- |
| Mono | The hardware's Mono is "one input unplugged" (mono buffer, input copied across), not the monophonic grain mode; the switch should do the former | Milestone 2 |
| Assign "All" | The hardware's amounts for All are hidden knob settings (255, 0 and 128 of 256 by default); Drizzle has no ports for them | Milestone 2 |
| Saving the buffer | Lost on reload, like Mist | After milestone 3 |
| CPU on the Pi | Grain count at high Density, the reverb | Milestone 1, measured |

## Milestones

- [x] Milestone 0: vendored code, Makefile, LV2 glue and `.ttl` with the full port list. Drizzle passes audio through the processor at default settings. Loads in jalv and in Ingen under WSL ([record](milestone-0-validation.md)).
- [ ] Milestone 1: every control and CV input, the four qualities, Freeze and Seed. Renders match `beads_test` at the same settings. Pi CPU and xrun check. Done offline; the Pi check is to do ([record](milestone-1-validation.md)).
- [ ] Milestone 2: Drizzle Synth, Mono, Gain, the gate output, the Assign input.
- [ ] Milestone 3: Peepo integration: overlay entry, pinned build, listening with guitar on the Pi. Tag `v0.1.0`.
